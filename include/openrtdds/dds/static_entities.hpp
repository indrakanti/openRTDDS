#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "openrtdds/rtps/data_message.hpp"
#include "openrtdds/rtps/reliability_messages.hpp"
#include "openrtdds/rtps/reliability_state.hpp"
#include "openrtdds/serialization/cdr.hpp"

namespace openrtdds::dds {

// Requirements: ORT-DDS-001, ORT-DDS-002, ORT-DDS-003, ORT-DDS-004,
// Requirements: ORT-DDS-005, ORT-DDS-006

enum class DdsError : std::uint8_t {
  none = 0,
  invalid_participant,
  invalid_topic,
  invalid_endpoint,
  serialization_failed,
  deserialization_failed,
  rtps_data_failed,
  reliability_message_failed,
  reliability_state_failed,
  unexpected_participant,
  unexpected_endpoint,
  invalid_action,
};

[[nodiscard]] const char* to_string(DdsError error) noexcept;

struct DdsResult final {
  DdsError error{DdsError::none};
  serialization::CdrError cdr_error{serialization::CdrError::none};
  rtps::RtpsError rtps_error{rtps::RtpsError::none};
  rtps::ReliabilityMessageError reliability_message_error{
      rtps::ReliabilityMessageError::none};
  rtps::ReliabilityError reliability_error{rtps::ReliabilityError::none};
  std::size_t bytes{0U};
  std::uint64_t sequence_number{0U};

  [[nodiscard]] bool ok() const noexcept { return error == DdsError::none; }
};

struct DomainParticipantConfig final {
  std::uint32_t domain_id{0U};
  rtps::ProtocolVersion version{};
  rtps::VendorId vendor_id{};
  rtps::GuidPrefix guid_prefix{};
};

class DomainParticipant final {
 public:
  explicit DomainParticipant(const DomainParticipantConfig config) noexcept;

  [[nodiscard]] bool valid() const noexcept { return valid_; }
  [[nodiscard]] const DomainParticipantConfig& config() const noexcept {
    return config_;
  }

 private:
  DomainParticipantConfig config_{};
  bool valid_{false};
};

class Publisher final {
 public:
  explicit Publisher(const DomainParticipant& participant) noexcept
      : participant_(participant.config()), valid_(participant.valid()) {}

  [[nodiscard]] bool valid() const noexcept { return valid_; }
  [[nodiscard]] const DomainParticipantConfig& participant() const noexcept {
    return participant_;
  }

 private:
  DomainParticipantConfig participant_{};
  bool valid_{false};
};

class Subscriber final {
 public:
  explicit Subscriber(const DomainParticipant& participant) noexcept
      : participant_(participant.config()), valid_(participant.valid()) {}

  [[nodiscard]] bool valid() const noexcept { return valid_; }
  [[nodiscard]] const DomainParticipantConfig& participant() const noexcept {
    return participant_;
  }

 private:
  DomainParticipantConfig participant_{};
  bool valid_{false};
};

struct TopicConfig final {
  std::uint32_t topic_id{0U};
  std::uint32_t type_id{0U};
};

template <typename Sample, typename TypeSupport>
class Topic final {
 public:
  using SampleType = Sample;
  using TypeSupportType = TypeSupport;

  explicit Topic(const TopicConfig config) noexcept
      : config_(config),
        valid_((config.topic_id != 0U) && (config.type_id != 0U) &&
               (config.type_id == TypeSupport::type_id)) {}

  [[nodiscard]] bool valid() const noexcept { return valid_; }
  [[nodiscard]] const TopicConfig& config() const noexcept { return config_; }

 private:
  TopicConfig config_{};
  bool valid_{false};
};

struct StaticEndpointConfig final {
  rtps::EntityId reader_id{};
  rtps::EntityId writer_id{};
  rtps::GuidPrefix remote_guid_prefix{};
  serialization::ByteOrder byte_order{
      serialization::ByteOrder::little_endian};
};

struct ReliableWriterQos final {
  std::uint8_t max_repair_attempts{2U};
  std::uint64_t repair_window_ns{100'000'000U};
};

namespace detail {

[[nodiscard]] bool valid_endpoint(
    const StaticEndpointConfig& endpoint) noexcept;
[[nodiscard]] bool same_guid_prefix(const rtps::GuidPrefix& left,
                                    const rtps::GuidPrefix& right) noexcept;

[[nodiscard]] inline DdsResult reliability_result(
    const rtps::ReliabilityError error) noexcept {
  DdsResult result{};
  result.error = DdsError::reliability_state_failed;
  result.reliability_error = error;
  return result;
}

[[nodiscard]] inline rtps::ReliableWriterConfig make_writer_config(
    const StaticEndpointConfig& endpoint,
    const ReliableWriterQos qos) noexcept {
  rtps::ReliableWriterConfig config{};
  config.reader_id = endpoint.reader_id;
  config.writer_id = endpoint.writer_id;
  config.max_repair_attempts = qos.max_repair_attempts;
  config.repair_window_ns = qos.repair_window_ns;
  return config;
}

[[nodiscard]] inline rtps::ReliableReaderConfig make_reader_config(
    const StaticEndpointConfig& endpoint) noexcept {
  rtps::ReliableReaderConfig config{};
  config.reader_id = endpoint.reader_id;
  config.writer_id = endpoint.writer_id;
  config.initial_sequence_number = 1U;
  return config;
}

}  // namespace detail

template <typename Sample, typename TypeSupport, std::size_t HistoryDepth,
          std::size_t MaxSerializedBytes>
class DataWriter final {
  static_assert(HistoryDepth > 0U, "writer history must be nonzero");
  static_assert(MaxSerializedBytes >= 4U,
                "serialized capacity must include CDR encapsulation");

 public:
  DataWriter(const Publisher& publisher,
             const Topic<Sample, TypeSupport>& topic,
             const StaticEndpointConfig endpoint,
             const ReliableWriterQos qos = {}) noexcept
      : participant_(publisher.participant()),
        topic_(topic.config()),
        endpoint_(endpoint),
        reliability_(detail::make_writer_config(endpoint, qos)),
        participant_valid_(publisher.valid()),
        topic_valid_(topic.valid()),
        endpoint_valid_(detail::valid_endpoint(endpoint) &&
                        (qos.repair_window_ns != 0U)),
        valid_(participant_valid_ && topic_valid_ && endpoint_valid_) {}

  [[nodiscard]] bool valid() const noexcept { return valid_; }

  [[nodiscard]] DdsResult write(const Sample& sample,
                                const std::uint64_t now_ns,
                                std::uint8_t* const datagram,
                                const std::size_t capacity) noexcept {
    if (!valid_) {
      return invalid_result();
    }

    serialization::CdrWriter cdr(serialized_.data(), serialized_.size());
    if (!cdr.begin(endpoint_.byte_order) ||
        !TypeSupport::serialize(sample, cdr)) {
      DdsResult result{};
      result.error = DdsError::serialization_failed;
      result.cdr_error = cdr.error();
      return result;
    }

    rtps::DataMessageConfig data_config = make_data_config(next_sequence_);
    rtps::DataMessageBuilder builder(datagram, capacity);
    if (!builder.build(data_config, cdr.data(), cdr.size())) {
      DdsResult result{};
      result.error = DdsError::rtps_data_failed;
      result.rtps_error = builder.error();
      return result;
    }

    rtps::ReliabilityActionBuffer<1U> actions{};
    const rtps::ReliabilityError reliability_error = reliability_.write(
        cdr.data(), cdr.size(), next_sequence_, now_ns, actions);
    if (reliability_error != rtps::ReliabilityError::none) {
      return detail::reliability_result(reliability_error);
    }

    DdsResult result{};
    result.bytes = builder.size();
    result.sequence_number = next_sequence_;
    ++next_sequence_;
    return result;
  }

  [[nodiscard]] DdsResult build_data_action(
      const rtps::ReliabilityAction& action, std::uint8_t* const datagram,
      const std::size_t capacity) const noexcept {
    if (!valid_) {
      return invalid_result();
    }
    if (((action.kind != rtps::ReliabilityActionKind::send_data) &&
         (action.kind != rtps::ReliabilityActionKind::retransmit_data)) ||
        (action.data == nullptr) || (action.data_size < 4U)) {
      DdsResult result{};
      result.error = DdsError::invalid_action;
      return result;
    }

    const rtps::DataMessageConfig data_config =
        make_data_config(action.sequence_number);
    rtps::DataMessageBuilder builder(datagram, capacity);
    if (!builder.build(data_config, action.data, action.data_size)) {
      DdsResult result{};
      result.error = DdsError::rtps_data_failed;
      result.rtps_error = builder.error();
      return result;
    }
    DdsResult result{};
    result.bytes = builder.size();
    result.sequence_number = action.sequence_number;
    return result;
  }

  [[nodiscard]] DdsResult build_heartbeat(
      std::uint8_t* const datagram, const std::size_t capacity,
      const bool final_flag = false) noexcept {
    if (!valid_) {
      return invalid_result();
    }
    rtps::HeartbeatConfig heartbeat{};
    heartbeat.header.version = participant_.version;
    heartbeat.header.vendor_id = participant_.vendor_id;
    heartbeat.header.guid_prefix = participant_.guid_prefix;
    heartbeat.header.submessage_byte_order = endpoint_.byte_order;
    heartbeat.final_flag = final_flag;
    const rtps::ReliabilityError reliability_error =
        reliability_.fill_heartbeat(heartbeat);
    if (reliability_error != rtps::ReliabilityError::none) {
      return detail::reliability_result(reliability_error);
    }

    rtps::ReliabilityMessageBuilder builder(datagram, capacity);
    if (!builder.build_heartbeat(heartbeat)) {
      DdsResult result{};
      result.error = DdsError::reliability_message_failed;
      result.reliability_message_error = builder.error();
      return result;
    }
    DdsResult result{};
    result.bytes = builder.size();
    result.sequence_number = heartbeat.last_sequence_number;
    return result;
  }

  template <std::size_t ActionCapacity>
  [[nodiscard]] DdsResult on_acknack(
      const std::uint8_t* const datagram, const std::size_t size,
      const std::uint64_t now_ns,
      rtps::ReliabilityActionBuffer<ActionCapacity>& actions) noexcept {
    if (!valid_) {
      return invalid_result();
    }
    rtps::AckNackView acknack{};
    const rtps::ReliabilityMessageError parse_error =
        rtps::parse_acknack_message(datagram, size, acknack);
    if (parse_error != rtps::ReliabilityMessageError::none) {
      DdsResult result{};
      result.error = DdsError::reliability_message_failed;
      result.reliability_message_error = parse_error;
      return result;
    }
    if (!detail::same_guid_prefix(acknack.header.guid_prefix,
                                  endpoint_.remote_guid_prefix)) {
      DdsResult result{};
      result.error = DdsError::unexpected_participant;
      return result;
    }
    const rtps::ReliabilityError reliability_error =
        reliability_.on_acknack(acknack, now_ns, actions);
    if (reliability_error != rtps::ReliabilityError::none) {
      return detail::reliability_result(reliability_error);
    }
    return {};
  }

  template <std::size_t ActionCapacity>
  [[nodiscard]] DdsResult on_timer(
      const std::uint64_t now_ns,
      rtps::ReliabilityActionBuffer<ActionCapacity>& actions) noexcept {
    if (!valid_) {
      return invalid_result();
    }
    const rtps::ReliabilityError error =
        reliability_.on_timer(now_ns, actions);
    return error == rtps::ReliabilityError::none
               ? DdsResult{}
               : detail::reliability_result(error);
  }

  [[nodiscard]] std::size_t history_size() const noexcept {
    return reliability_.size();
  }
  [[nodiscard]] const TopicConfig& topic() const noexcept { return topic_; }

 private:
  [[nodiscard]] DdsResult invalid_result() const noexcept {
    DdsResult result{};
    if (!endpoint_valid_) {
      result.error = DdsError::invalid_endpoint;
    } else if (!topic_valid_) {
      result.error = DdsError::invalid_topic;
    } else {
      result.error = DdsError::invalid_participant;
    }
    return result;
  }

  [[nodiscard]] rtps::DataMessageConfig make_data_config(
      const std::uint64_t sequence_number) const noexcept {
    rtps::DataMessageConfig config{};
    config.version = participant_.version;
    config.vendor_id = participant_.vendor_id;
    config.guid_prefix = participant_.guid_prefix;
    config.reader_id = endpoint_.reader_id;
    config.writer_id = endpoint_.writer_id;
    config.sequence_number = sequence_number;
    config.submessage_byte_order = endpoint_.byte_order;
    return config;
  }

  DomainParticipantConfig participant_{};
  TopicConfig topic_{};
  StaticEndpointConfig endpoint_{};
  std::array<std::uint8_t, MaxSerializedBytes> serialized_{};
  rtps::ReliableWriter<HistoryDepth, MaxSerializedBytes> reliability_;
  std::uint64_t next_sequence_{1U};
  bool participant_valid_{false};
  bool topic_valid_{false};
  bool endpoint_valid_{false};
  bool valid_{false};
};

template <typename Sample, typename TypeSupport, std::size_t WindowBits>
class DataReader final {
  static_assert(std::is_nothrow_default_constructible<Sample>::value,
                "sample default construction must be noexcept");
  static_assert(std::is_nothrow_copy_assignable<Sample>::value,
                "sample copy assignment must be noexcept");

 public:
  DataReader(const Subscriber& subscriber,
             const Topic<Sample, TypeSupport>& topic,
             const StaticEndpointConfig endpoint) noexcept
      : participant_(subscriber.participant()),
        topic_(topic.config()),
        endpoint_(endpoint),
        reliability_(detail::make_reader_config(endpoint)),
        participant_valid_(subscriber.valid()),
        topic_valid_(topic.valid()),
        endpoint_valid_(detail::valid_endpoint(endpoint)),
        valid_(participant_valid_ && topic_valid_ && endpoint_valid_) {}

  [[nodiscard]] bool valid() const noexcept { return valid_; }

  [[nodiscard]] DdsResult take(const std::uint8_t* const datagram,
                               const std::size_t size,
                               Sample& sample) noexcept {
    if (!valid_) {
      return invalid_result();
    }
    rtps::DataMessageView view{};
    const rtps::RtpsError parse_error =
        rtps::parse_data_message(datagram, size, view);
    if (parse_error != rtps::RtpsError::none) {
      DdsResult result{};
      result.error = DdsError::rtps_data_failed;
      result.rtps_error = parse_error;
      return result;
    }
    if (!detail::same_guid_prefix(view.guid_prefix,
                                  endpoint_.remote_guid_prefix)) {
      DdsResult result{};
      result.error = DdsError::unexpected_participant;
      return result;
    }
    if ((view.reader_id != endpoint_.reader_id) ||
        (view.writer_id != endpoint_.writer_id)) {
      DdsResult result{};
      result.error = DdsError::unexpected_endpoint;
      return result;
    }

    Sample candidate{};
    serialization::CdrReader cdr(view.serialized_payload, view.payload_size);
    if (!cdr.begin() || !TypeSupport::deserialize(cdr, candidate)) {
      DdsResult result{};
      result.error = DdsError::deserialization_failed;
      result.cdr_error = cdr.error();
      return result;
    }

    rtps::ReliabilityActionBuffer<1U> actions{};
    const rtps::ReliabilityError reliability_error =
        reliability_.on_data(view.sequence_number, actions);
    if (reliability_error != rtps::ReliabilityError::none) {
      return detail::reliability_result(reliability_error);
    }
    sample = candidate;
    DdsResult result{};
    result.bytes = size;
    result.sequence_number = view.sequence_number;
    return result;
  }

  [[nodiscard]] DdsResult on_heartbeat(
      const std::uint8_t* const datagram, const std::size_t size,
      std::uint8_t* const response, const std::size_t response_capacity) noexcept {
    if (!valid_) {
      return invalid_result();
    }
    rtps::HeartbeatView heartbeat{};
    const rtps::ReliabilityMessageError parse_error =
        rtps::parse_heartbeat_message(datagram, size, heartbeat);
    if (parse_error != rtps::ReliabilityMessageError::none) {
      DdsResult result{};
      result.error = DdsError::reliability_message_failed;
      result.reliability_message_error = parse_error;
      return result;
    }
    if (!detail::same_guid_prefix(heartbeat.header.guid_prefix,
                                  endpoint_.remote_guid_prefix)) {
      DdsResult result{};
      result.error = DdsError::unexpected_participant;
      return result;
    }

    rtps::ReliabilityActionBuffer<1U> actions{};
    const rtps::ReliabilityError reliability_error =
        reliability_.on_heartbeat(heartbeat, actions);
    if (reliability_error != rtps::ReliabilityError::none) {
      return detail::reliability_result(reliability_error);
    }
    if (actions.size() == 0U) {
      return {};
    }

    rtps::AckNackConfig acknack{};
    acknack.header.version = participant_.version;
    acknack.header.vendor_id = participant_.vendor_id;
    acknack.header.guid_prefix = participant_.guid_prefix;
    acknack.header.submessage_byte_order = endpoint_.byte_order;
    acknack.reader_id = actions[0U].reader_id;
    acknack.writer_id = actions[0U].writer_id;
    acknack.reader_state = actions[0U].reader_state;
    acknack.count = actions[0U].control_count;
    acknack.final_flag = actions[0U].final_flag;

    rtps::ReliabilityMessageBuilder builder(response, response_capacity);
    if (!builder.build_acknack(acknack)) {
      DdsResult result{};
      result.error = DdsError::reliability_message_failed;
      result.reliability_message_error = builder.error();
      return result;
    }
    DdsResult result{};
    result.bytes = builder.size();
    return result;
  }

  [[nodiscard]] std::uint64_t next_expected_sequence() const noexcept {
    return reliability_.next_expected_sequence();
  }
  [[nodiscard]] const TopicConfig& topic() const noexcept { return topic_; }

 private:
  [[nodiscard]] DdsResult invalid_result() const noexcept {
    DdsResult result{};
    if (!endpoint_valid_) {
      result.error = DdsError::invalid_endpoint;
    } else if (!topic_valid_) {
      result.error = DdsError::invalid_topic;
    } else {
      result.error = DdsError::invalid_participant;
    }
    return result;
  }

  DomainParticipantConfig participant_{};
  TopicConfig topic_{};
  StaticEndpointConfig endpoint_{};
  rtps::ReliableReader<WindowBits> reliability_;
  bool participant_valid_{false};
  bool topic_valid_{false};
  bool endpoint_valid_{false};
  bool valid_{false};
};

}  // namespace openrtdds::dds
