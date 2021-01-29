#include <memory>
#include <utility>
#include <vector>

#include "src/vizier/services/agent/manager/manager.h"
#include "src/vizier/services/agent/manager/registration.h"

namespace pl {
namespace vizier {
namespace agent {

using ::pl::event::Dispatcher;
using ::pl::shared::k8s::metadatapb::ResourceUpdate;

Status RegistrationHandler::DispatchRegistration() {
  // Send the registration request.
  messages::VizierMessage req;

  {
    absl::base_internal::SpinLockHolder lock(&registration_lock_);
    // Reuse the same ASID if the agent has registered in the past to preserve UPIDs.
    if (ever_registered_) {
      LOG_IF(FATAL, agent_info()->asid == 0) << "Reregistering agent which is not yet registered";
      req.mutable_register_agent_request()->set_asid(agent_info()->asid);
    } else {
      LOG_IF(FATAL, agent_info()->asid != 0) << "Registering agent has already been registered";
    }
  }

  auto req_info = req.mutable_register_agent_request()->mutable_info();

  ToProto(agent_info()->agent_id, req_info->mutable_agent_id());
  req_info->set_ip_address(agent_info()->address);
  auto host_info = req_info->mutable_host_info();
  host_info->set_hostname(agent_info()->hostname);
  host_info->set_pod_name(agent_info()->pod_name);
  host_info->set_host_ip(agent_info()->host_ip);
  *req_info->mutable_capabilities() = agent_info()->capabilities;

  PL_RETURN_IF_ERROR(nats_conn()->Publish(req));
  return Status::OK();
}

void RegistrationHandler::RegisterAgent(bool reregister) {
  {
    absl::base_internal::SpinLockHolder lock(&registration_lock_);
    if (registration_in_progress_) {
      // Registration already in progress.
      return;
    }
    registration_in_progress_ = true;
    LOG_IF(FATAL, reregister != ever_registered_)
        << (reregister ? "Re-registering agent that does not exist"
                       : "Registering already registered agent");
  }

  // Send the agent info.

  // Wait a random amount of time before registering. This is so the agents don't swarm the
  // metadata service all at the same time when Vizier first starts up.
  std::random_device rnd_device;
  std::mt19937_64 eng{rnd_device()};
  std::uniform_int_distribution<> dist{
      kMinWaitTimeMillis,
      kMaxWaitTimeMillis};  // Wait a random amount of time between 10ms to 1min.

  registration_wait_->EnableTimer(std::chrono::milliseconds{dist(eng)});
}

Status RegistrationHandler::HandleMessage(std::unique_ptr<messages::VizierMessage> msg) {
  LOG_IF(FATAL, !msg->has_register_agent_response())
      << "Did not get register agent response. Got: " << msg->DebugString();

  absl::base_internal::SpinLockHolder lock(&registration_lock_);

  if (!registration_in_progress_) {
    // We may have gotten a duplicate registration response from NATS.
    return Status::OK();
  }
  registration_timeout_->DisableTimer();
  registration_in_progress_ = false;

  if (!ever_registered_) {
    ever_registered_ = true;
    return post_registration_hook_(msg->register_agent_response().asid());
  }
  return post_reregistration_hook_(msg->register_agent_response().asid());
}

}  // namespace agent
}  // namespace vizier
}  // namespace pl
