#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "rmw/rmw.h"
#include "rmw_openrtdds_cpp/qos.hpp"
#include "rmw_openrtdds_cpp/type_support.hpp"
#include "rmw_openrtdds_cpp/visibility_control.h"

namespace openrtdds::rtps {

enum class RtpsError : std::uint8_t;

}  // namespace openrtdds::rtps

namespace rmw_openrtdds_cpp {

// Requirements: ORT-RMW-047, ORT-RMW-048
enum class SubscriptionReceiveError : std::uint8_t {
  none = 0,
  invalid_argument,
  inactive_context,
  invalid_timestamp,
  rtps_parse_failed,
  wrong_reader,
  invalid_writer,
  payload_too_large,
  writer_table_full,
  reception_sequence_exhausted,
};

struct SubscriptionReceiveResult final {
  SubscriptionReceiveError error{SubscriptionReceiveError::none};
  openrtdds::rtps::RtpsError rtps_error{
      static_cast<openrtdds::rtps::RtpsError>(0)};
  bool accepted{false};
  bool ignored_local{false};
  bool duplicate_or_stale{false};
  bool dropped_oldest{false};
  std::uint64_t publication_sequence_number{0U};
  std::uint64_t reception_sequence_number{0U};
};

struct SubscriptionSnapshot final {
  DdsNames names{};
  rmw_qos_profile_t actual_qos{};
  std::array<std::uint8_t, RMW_GID_STORAGE_SIZE> reader_guid{};
  std::size_t maximum_serialized_sample_size{0U};
  std::size_t history_size{0U};
  std::size_t history_capacity{0U};
  std::size_t observed_writer_count{0U};
  std::uint64_t next_reception_sequence_number{0U};
  std::uint64_t accepted_samples{0U};
  std::uint64_t dropped_samples{0U};
  bool ready{false};
  const std::uint8_t* sedp_announcement{nullptr};
  std::size_t sedp_announcement_size{0U};
};

[[nodiscard]] RMW_OPENRTDDS_CPP_PUBLIC const char* to_string(
    SubscriptionReceiveError error) noexcept;

// Transport-ingress boundary for one complete, unfragmented RTPS DATA
// datagram. The caller supplies receive time and may supply source time when
// the transport obtained it outside the DATA message. A zero source time uses
// INFO_TS when present. No allocation or callback occurs.
[[nodiscard]] RMW_OPENRTDDS_CPP_PUBLIC bool receive_data_message(
    const rmw_subscription_t* subscription, const std::uint8_t* message,
    std::size_t message_size, std::uint64_t source_timestamp_ns,
    std::uint64_t received_timestamp_ns,
    SubscriptionReceiveResult& result) noexcept;

[[nodiscard]] RMW_OPENRTDDS_CPP_PUBLIC bool inspect_subscription(
    const rmw_subscription_t* subscription,
    SubscriptionSnapshot& snapshot) noexcept;

}  // namespace rmw_openrtdds_cpp
