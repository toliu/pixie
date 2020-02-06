#include "src/stirling/mysql/parse_utils.h"

#include <string>

#include "src/common/base/byte_utils.h"
#include "src/stirling/mysql/mysql_types.h"

namespace pl {
namespace stirling {
namespace mysql {

StatusOr<int64_t> ProcessLengthEncodedInt(std::string_view s, size_t* offset) {
  // If it is < 0xfb, treat it as a 1-byte integer.
  // If it is 0xfc, it is followed by a 2-byte integer.
  // If it is 0xfd, it is followed by a 3-byte integer.
  // If it is 0xfe, it is followed by a 8-byte integer.

  constexpr uint8_t kLencIntPrefix2b = 0xfc;
  constexpr uint8_t kLencIntPrefix3b = 0xfd;
  constexpr uint8_t kLencIntPrefix8b = 0xfe;

  s = s.substr(*offset);

  if (s.empty()) {
    return error::Internal("Not enough bytes to extract length-encoded int");
  }

#define CHECK_LENGTH(s, len)                                                  \
  if (s.length() < len) {                                                     \
    return error::Internal("Not enough bytes to extract length-encoded int"); \
  }

  int64_t result;
  switch (static_cast<uint8_t>(s[0])) {
    case kLencIntPrefix2b:
      s.remove_prefix(1);
      CHECK_LENGTH(s, 2);
      result = utils::LittleEndianByteStrToInt<int64_t, 2>(s);
      *offset += (1 + 2);
      break;
    case kLencIntPrefix3b:
      s.remove_prefix(1);
      CHECK_LENGTH(s, 3);
      result = utils::LittleEndianByteStrToInt<int64_t, 3>(s);
      *offset += (1 + 3);
      break;
    case kLencIntPrefix8b:
      s.remove_prefix(1);
      CHECK_LENGTH(s, 8);
      result = utils::LittleEndianByteStrToInt<int64_t, 8>(s);
      *offset += (1 + 8);
      break;
    default:
      CHECK_LENGTH(s, 1);
      result = utils::LittleEndianByteStrToInt<int64_t, 1>(s);
      *offset += 1;
      break;
  }

#undef CHECK_LENGTH

  return result;
}

Status DissectStringParam(std::string_view msg, size_t* param_offset, std::string* param) {
  PL_ASSIGN_OR_RETURN(int param_length, ProcessLengthEncodedInt(msg, param_offset));
  if (msg.size() < *param_offset + param_length) {
    return error::Internal("Not enough bytes to dissect string param.");
  }
  *param = msg.substr(*param_offset, param_length);
  *param_offset += param_length;
  return Status::OK();
}

template <size_t length>
Status DissectIntParam(std::string_view msg, size_t* offset, std::string* param) {
  if (msg.size() < *offset + length) {
    return error::Internal("Not enough bytes to dissect int param.");
  }
  *param = std::to_string(utils::LittleEndianByteStrToInt<int64_t, length>(msg.substr(*offset)));
  *offset += length;
  return Status::OK();
}

// Template instantiations to include in the object file.
template Status DissectIntParam<1>(std::string_view msg, size_t* offset, std::string* param);
template Status DissectIntParam<2>(std::string_view msg, size_t* offset, std::string* param);
template Status DissectIntParam<4>(std::string_view msg, size_t* offset, std::string* param);
template Status DissectIntParam<8>(std::string_view msg, size_t* offset, std::string* param);

template <typename TFloatType>
Status DissectFloatParam(std::string_view msg, size_t* offset, std::string* param) {
  size_t length = sizeof(TFloatType);
  if (msg.size() < *offset + length) {
    return error::Internal("Not enough bytes to dissect float param.");
  }
  *param =
      std::to_string(utils::LittleEndianByteStrToFloat<TFloatType>(msg.substr(*offset, length)));
  *offset += length;
  return Status::OK();
}

// Template instantiations to include in the object file.
template Status DissectFloatParam<float>(std::string_view msg, size_t* offset, std::string* param);
template Status DissectFloatParam<double>(std::string_view msg, size_t* offset, std::string* param);

Status DissectDateTimeParam(std::string_view msg, size_t* offset, std::string* param) {
  if (msg.size() < *offset + 1) {
    return error::Internal("Not enough bytes to dissect date/time param.");
  }

  uint8_t length = static_cast<uint8_t>(msg[*offset]);
  ++*offset;

  if (msg.size() < *offset + length) {
    return error::Internal("Not enough bytes to dissect date/time param.");
  }
  *param = "MySQL DateTime rendering not implemented yet";
  *offset += length;
  return Status::OK();
}

}  // namespace mysql
}  // namespace stirling
}  // namespace pl
