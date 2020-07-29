package controllers

import (
	"context"
	"fmt"
	"time"

	types "github.com/gogo/protobuf/types"
	uuid "github.com/satori/go.uuid"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
	"pixielabs.ai/pixielabs/src/carnot/planner/distributedpb"
	statuspb "pixielabs.ai/pixielabs/src/common/base/proto"
	schemapb "pixielabs.ai/pixielabs/src/table_store/proto"
	"pixielabs.ai/pixielabs/src/utils"
	"pixielabs.ai/pixielabs/src/vizier/services/metadata/metadataenv"
	"pixielabs.ai/pixielabs/src/vizier/services/metadata/metadatapb"
	storepb "pixielabs.ai/pixielabs/src/vizier/services/metadata/storepb"
	agentpb "pixielabs.ai/pixielabs/src/vizier/services/shared/agentpb"
)

// UnhealthyAgentThreshold is the amount of time where an agent is considered unhealthy if
// its last heartbeat is greater than this value.
const UnhealthyAgentThreshold = 30 * time.Second

// Server defines an gRPC server type.
type Server struct {
	env               metadataenv.MetadataEnv
	agentManager      AgentManager
	tracepointManager *TracepointManager
	clock             utils.Clock
	mds               MetadataStore
}

// NewServerWithClock creates a new server with a clock and the ability to configure the chunk size and
// update period of the GetAgentUpdates handler.
func NewServerWithClock(env metadataenv.MetadataEnv, agtMgr AgentManager, tracepointManager *TracepointManager,
	mds MetadataStore, clock utils.Clock) (*Server, error) {
	return &Server{
		env:               env,
		agentManager:      agtMgr,
		clock:             clock,
		mds:               mds,
		tracepointManager: tracepointManager,
	}, nil
}

// NewServer creates GRPC handlers.
func NewServer(env metadataenv.MetadataEnv, agtMgr AgentManager, tracepointManager *TracepointManager, mds MetadataStore) (*Server, error) {
	clock := utils.SystemClock{}
	return NewServerWithClock(env, agtMgr, tracepointManager, mds, clock)
}

func convertToRelationMap(computedSchema *storepb.ComputedSchema) (*schemapb.Schema, error) {
	schemas := computedSchema.Tables
	respSchemaPb := &schemapb.Schema{}
	respSchemaPb.RelationMap = make(map[string]*schemapb.Relation)

	for _, schema := range schemas {
		columnPbs := make([]*schemapb.Relation_ColumnInfo, len(schema.Columns))
		for j, column := range schema.Columns {
			columnPbs[j] = &schemapb.Relation_ColumnInfo{
				ColumnName:         column.Name,
				ColumnType:         column.DataType,
				ColumnDesc:         column.Desc,
				ColumnSemanticType: column.SemanticType,
			}
		}
		schemaPb := &schemapb.Relation{
			Columns: columnPbs,
		}
		respSchemaPb.RelationMap[schema.Name] = schemaPb
	}

	return respSchemaPb, nil
}

func convertToSchemaInfo(computedSchema *storepb.ComputedSchema) ([]*distributedpb.SchemaInfo, error) {
	schemaInfo := make([]*distributedpb.SchemaInfo, len(computedSchema.Tables))

	for idx, schema := range computedSchema.Tables {
		columnPbs := make([]*schemapb.Relation_ColumnInfo, len(schema.Columns))
		for j, column := range schema.Columns {
			columnPbs[j] = &schemapb.Relation_ColumnInfo{
				ColumnName:         column.Name,
				ColumnType:         column.DataType,
				ColumnDesc:         column.Desc,
				ColumnSemanticType: column.SemanticType,
			}
		}
		schemaPb := &schemapb.Relation{
			Columns: columnPbs,
		}

		agentIDs, ok := computedSchema.TableNameToAgentIDs[schema.Name]
		if !ok {
			return nil, fmt.Errorf("Can't find agentIDs for schema %s", schema.Name)
		}

		schemaInfo[idx] = &distributedpb.SchemaInfo{
			Name:      schema.Name,
			Relation:  schemaPb,
			AgentList: agentIDs.AgentID,
		}
	}

	return schemaInfo, nil
}

// GetSchemas returns the schemas in the system.
func (s *Server) GetSchemas(ctx context.Context, req *metadatapb.SchemaRequest) (*metadatapb.SchemaResponse, error) {
	computedSchema, err := s.mds.GetComputedSchema()
	if err != nil {
		return nil, err
	}
	respSchemaPb, err := convertToRelationMap(computedSchema)
	if err != nil {
		return nil, err
	}
	resp := &metadatapb.SchemaResponse{
		Schema: respSchemaPb,
	}
	return resp, nil
}

// GetSchemaByAgent returns the schemas in the system.
func (s *Server) GetSchemaByAgent(ctx context.Context, req *metadatapb.SchemaByAgentRequest) (*metadatapb.SchemaByAgentResponse, error) {
	return nil, status.Error(codes.Unimplemented, "Not implemented yet")
}

// GetAgentInfo returns information about registered agents.
func (s *Server) GetAgentInfo(ctx context.Context, req *metadatapb.AgentInfoRequest) (*metadatapb.AgentInfoResponse, error) {
	agents, err := s.agentManager.GetActiveAgents()
	if err != nil {
		return nil, err
	}

	currentTime := s.clock.Now()

	// Populate AgentInfoResponse.
	agentResponses := make([]*metadatapb.AgentMetadata, 0)
	for _, agent := range agents {
		state := agentpb.AGENT_STATE_HEALTHY
		timeSinceLastHb := currentTime.Sub(time.Unix(0, agent.LastHeartbeatNS))
		if timeSinceLastHb > UnhealthyAgentThreshold {
			state = agentpb.AGENT_STATE_UNRESPONSIVE
		}

		resp := metadatapb.AgentMetadata{
			Agent: agent,
			Status: &agentpb.AgentStatus{
				NSSinceLastHeartbeat: timeSinceLastHb.Nanoseconds(),
				State:                state,
			},
		}
		agentResponses = append(agentResponses, &resp)
	}

	resp := metadatapb.AgentInfoResponse{
		Info: agentResponses,
	}

	return &resp, nil
}

// GetAgentTableMetadata returns table metadata for each agent. We currently assume that all agents
// have the same schema, but this code will need to be updated when that assumption no longer holds true.
func (s *Server) GetAgentTableMetadata(ctx context.Context, req *metadatapb.AgentTableMetadataRequest) (*metadatapb.AgentTableMetadataResponse, error) {
	computedSchema, err := s.mds.GetComputedSchema()
	if err != nil {
		return nil, err
	}

	dataInfos, err := s.mds.GetAgentsDataInfo()
	if err != nil {
		return nil, err
	}

	// Populate AgentTableMetadataResponse.
	var agentsMetadata []*metadatapb.AgentTableMetadata
	for agentID, dataInfo := range dataInfos {
		a := &metadatapb.AgentTableMetadata{
			AgentID:  utils.ProtoFromUUID(&agentID),
			DataInfo: dataInfo,
		}
		agentsMetadata = append(agentsMetadata, a)
	}
	schemaInfo, err := convertToSchemaInfo(computedSchema)
	if err != nil {
		return nil, err
	}

	resp := metadatapb.AgentTableMetadataResponse{
		MetadataByAgent: agentsMetadata,
		SchemaInfo:      schemaInfo,
	}

	return &resp, nil
}

// GetAgentUpdates streams agent updates to the requestor periodically as they come in.
// It first sends the complete initial agent state in the beginning of the request, and then deltas after that.
// Note that as it is currently designed, it can only handle one stream at a time (to a single metadata server).
// That is because the agent manager tracks the deltas in the state in a single object, rather than per request.
func (s *Server) GetAgentUpdates(req *metadatapb.AgentUpdatesRequest, srv metadatapb.MetadataService_GetAgentUpdatesServer) error {
	// First read the entire initial state of the agents first, then stream updates after that.
	readInitialState := true
	if req.MaxUpdatesPerResponse == 0 {
		return status.Error(codes.InvalidArgument, "Max updates per agent should be specified in AgentUpdatesRequest")
	}
	agentChunkSize := int(req.MaxUpdatesPerResponse)
	if req.MaxUpdateInterval == nil {
		return status.Error(codes.InvalidArgument, "Max update interval should be specified in AgentUpdatesRequest")
	}
	agentUpdatePeriod, err := types.DurationFromProto(req.MaxUpdateInterval)
	if err != nil {
		return status.Error(codes.Internal, fmt.Sprintf("Failed to parse duration: %+v", err))
	}

	for {
		updates, newComputedSchema, err := s.agentManager.GetAgentUpdates(readInitialState)

		if err != nil {
			return err
		}
		if readInitialState {
			readInitialState = false
		}

		currentIdx := 0
		finishedUpdates := len(updates) == 0
		finishedSchema := newComputedSchema == nil

		if finishedSchema && finishedUpdates {
			// Send an empty update after the duration has passed if we have no new information.
			err := srv.Send(&metadatapb.AgentUpdatesResponse{})
			if err != nil {
				return err
			}
		} else {
			// Chunk up the data so we don't overwhelm the query broker with a lot of data at once.
			for !(finishedUpdates && finishedSchema) {
				select {
				case <-srv.Context().Done():
					return nil
				default:
					response := &metadatapb.AgentUpdatesResponse{}
					if !finishedSchema {
						schemas, err := convertToSchemaInfo(newComputedSchema)
						if err != nil {
							return err
						}
						response.AgentSchemas = schemas
						finishedSchema = true
					}

					for idx := currentIdx; idx < currentIdx+agentChunkSize; idx++ {
						if idx < len(updates) {
							response.AgentUpdates = append(response.AgentUpdates, updates[idx])
						} else {
							finishedUpdates = true
						}
					}

					currentIdx += agentChunkSize
					err := srv.Send(response)
					if err != nil {
						return err
					}
				}
			}
		}

		time.Sleep(agentUpdatePeriod)
	}

	return nil
}

// RegisterTracepoint is a request to register the tracepoints specified in the Program on all agents.
func (s *Server) RegisterTracepoint(ctx context.Context, req *metadatapb.RegisterTracepointRequest) (*metadatapb.RegisterTracepointResponse, error) {
	// TODO(michelle): Some additional work will need to be done
	// in order to transactionalize the creation of multiple tracepoints. The current behavior is temporary just
	// so we can get the updated protos out.
	responses := make([]*metadatapb.RegisterTracepointResponse_TracepointStatus, len(req.Requests))

	// Create tracepoint.
	for i, tp := range req.Requests {
		ttl, err := types.DurationFromProto(tp.TTL)
		if err != nil {
			return nil, err
		}
		tracepointID, err := s.tracepointManager.CreateTracepoint(tp.Name, tp.Program, ttl)
		if err != nil && err != ErrTracepointAlreadyExists {
			return nil, err
		}
		if err == ErrTracepointAlreadyExists {
			responses[i] = &metadatapb.RegisterTracepointResponse_TracepointStatus{
				ID: utils.ProtoFromUUID(tracepointID),
				Status: &statuspb.Status{
					ErrCode: statuspb.ALREADY_EXISTS,
				},
				Name: tp.Name,
			}
			continue
		}

		responses[i] = &metadatapb.RegisterTracepointResponse_TracepointStatus{
			ID: utils.ProtoFromUUID(tracepointID),
			Status: &statuspb.Status{
				ErrCode: statuspb.OK,
			},
			Name: tp.Name,
		}

		// Get all agents currently running.
		agents, err := s.agentManager.GetActiveAgents()
		if err != nil {
			return nil, err
		}
		agentIDs := make([]uuid.UUID, len(agents))
		for i, agent := range agents {
			agentIDs[i] = utils.UUIDFromProtoOrNil(agent.Info.AgentID)
		}

		// Register tracepoint on all agents.
		err = s.tracepointManager.RegisterTracepoint(agentIDs, *tracepointID, tp.Program)
		if err != nil {
			return nil, err
		}
	}

	resp := &metadatapb.RegisterTracepointResponse{
		Tracepoints: responses,
		Status: &statuspb.Status{
			ErrCode: statuspb.OK,
		},
	}

	return resp, nil
}

// GetTracepointInfo is a request to check the status for the given tracepoint.
func (s *Server) GetTracepointInfo(ctx context.Context, req *metadatapb.GetTracepointInfoRequest) (*metadatapb.GetTracepointInfoResponse, error) {
	var tracepointInfos []*storepb.TracepointInfo
	var err error
	if len(req.IDs) > 0 {
		ids := make([]uuid.UUID, len(req.IDs))
		for i, id := range req.IDs {
			ids[i] = utils.UUIDFromProtoOrNil(id)
		}

		tracepointInfos, err = s.tracepointManager.GetTracepointsForIDs(ids)
	} else {
		tracepointInfos, err = s.tracepointManager.GetAllTracepoints()
	}

	if err != nil {
		return nil, err
	}

	tracepointState := make([]*metadatapb.GetTracepointInfoResponse_TracepointState, len(tracepointInfos))

	for i, tracepoint := range tracepointInfos {
		if tracepoint == nil { // Tracepoint does not exist.
			tracepointState[i] = &metadatapb.GetTracepointInfoResponse_TracepointState{
				ID:    req.IDs[i],
				State: statuspb.UNKNOWN_STATE,
				Status: &statuspb.Status{
					ErrCode: statuspb.NOT_FOUND,
				},
			}
			continue
		}
		tUUID := utils.UUIDFromProtoOrNil(tracepoint.ID)

		tracepointStates, err := s.tracepointManager.GetTracepointStates(tUUID)
		if err != nil {
			return nil, err
		}

		state, status := getTracepointStateFromAgentTracepointStates(tracepointStates)

		schemas := make([]string, len(tracepoint.Program.Outputs))
		for i, o := range tracepoint.Program.Outputs {
			schemas[i] = o.Name
		}

		tracepointState[i] = &metadatapb.GetTracepointInfoResponse_TracepointState{
			ID:            tracepoint.ID,
			State:         state,
			Status:        status,
			Name:          tracepoint.Name,
			ExpectedState: tracepoint.ExpectedState,
			SchemaNames:   schemas,
		}
	}

	return &metadatapb.GetTracepointInfoResponse{
		Tracepoints: tracepointState,
	}, nil
}

func getTracepointStateFromAgentTracepointStates(agentStates []*storepb.AgentTracepointStatus) (statuspb.LifeCycleState, *statuspb.Status) {
	if len(agentStates) == 0 {
		return statuspb.PENDING_STATE, nil
	}

	numFailed := 0
	numTerminated := 0
	numPending := 0
	numRunning := 0

	for _, s := range agentStates {
		if s.State == statuspb.TERMINATED_STATE {
			numTerminated++
		} else if s.State == statuspb.FAILED_STATE {
			numFailed++
		} else if s.State == statuspb.PENDING_STATE {
			numPending++
		} else if s.State == statuspb.RUNNING_STATE {
			numRunning++
		}
	}

	if numTerminated > 0 { // If any agentTracepoints are terminated, then we consider the tracepoint in an terminated state.
		return statuspb.TERMINATED_STATE, nil
	}

	if numRunning > 0 { // If a single agentTracepoint is running, then we consider the overall tracepoint as healthy.
		return statuspb.RUNNING_STATE, nil
	}

	if numPending > 0 { // If no agentTracepoints are running, but some are in a pending state, the tracepoint is pending.
		return statuspb.PENDING_STATE, nil
	}

	if numFailed > 0 { // If there are no terminated/running/pending tracepoints, then the tracepoint is failed.
		return statuspb.FAILED_STATE, agentStates[0].Status // Just use the status from the first failed agent for now.
	}

	return statuspb.UNKNOWN_STATE, nil
}

// RemoveTracepoint is a request to evict the given tracepoint on all agents.
func (s *Server) RemoveTracepoint(ctx context.Context, req *metadatapb.RemoveTracepointRequest) (*metadatapb.RemoveTracepointResponse, error) {
	err := s.tracepointManager.RemoveTracepoints(req.Names)
	if err != nil {
		return nil, err
	}

	return &metadatapb.RemoveTracepointResponse{
		Status: &statuspb.Status{
			ErrCode: statuspb.OK,
		},
	}, nil
}
