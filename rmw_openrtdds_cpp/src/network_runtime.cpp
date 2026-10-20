#include "network_runtime.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>
#include <time.h>
#include <unistd.h>

#include "internal.hpp"
#include "openrtdds/rtps/data_message.hpp"

namespace rmw_openrtdds_cpp::detail {
namespace {

using openrtdds::rtps::EndpointKind;
using openrtdds::rtps::GuidPrefix;
using openrtdds::rtps::Locator;
using openrtdds::transport::Ipv4Address;
using openrtdds::transport::UdpEndpoint;
using openrtdds::transport::UdpError;

constexpr Ipv4Address kLoopback{{{127U, 0U, 0U, 1U}}};
constexpr Ipv4Address kSpdpGroup{{{239U, 255U, 0U, 1U}}};
constexpr std::uint64_t kAnnouncementPeriodNs = 250'000'000ULL;
constexpr std::uint64_t kRemoteStateLifetimeNs = 5'000'000'000ULL;
constexpr std::size_t kMaximumReceivesPerSocket = 32U;
constexpr std::size_t kMaximumSamplesPerPublisherPass = 32U;

[[nodiscard]] std::uint64_t monotonic_now_ns() noexcept {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

[[nodiscard]] std::uint64_t realtime_now_ns() noexcept {
  timespec value{};
  if ((::clock_gettime(CLOCK_REALTIME, &value) != 0) ||
      (value.tv_sec < 0)) {
    return 0U;
  }
  constexpr std::uint64_t billion = 1'000'000'000ULL;
  const auto seconds = static_cast<std::uint64_t>(value.tv_sec);
  if (seconds > (std::numeric_limits<std::uint64_t>::max() / billion)) {
    return 0U;
  }
  return (seconds * billion) + static_cast<std::uint64_t>(value.tv_nsec);
}

[[nodiscard]] bool to_endpoint(const Locator& locator,
                               UdpEndpoint& endpoint) noexcept {
  if (!openrtdds::rtps::valid_udp_v4_locator(locator)) {
    return false;
  }
  endpoint.address.octets = {{locator.address[12], locator.address[13],
                              locator.address[14], locator.address[15]}};
  endpoint.port = static_cast<std::uint16_t>(locator.port);
  return true;
}

template <std::size_t Capacity>
[[nodiscard]] bool first_endpoint(
    const openrtdds::rtps::BoundedLocatorList<Capacity>& locators,
    UdpEndpoint& endpoint) noexcept {
  for (std::size_t index = 0U; index < locators.size; ++index) {
    if (to_endpoint(locators[index], endpoint)) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] GuidPrefix packet_prefix(const std::uint8_t* packet,
                                       const std::size_t size) noexcept {
  GuidPrefix prefix{};
  if ((packet != nullptr) && (size >= 20U)) {
    std::memcpy(prefix.value.data(), &packet[8], prefix.value.size());
  }
  return prefix;
}

}  // namespace

NetworkRuntime::~NetworkRuntime() noexcept { shutdown(); }

bool NetworkRuntime::initialize(rmw_context_t* const context) noexcept {
  if ((context == nullptr) || (context->impl == nullptr) ||
      (context_ != nullptr)) {
    return false;
  }
  context_ = context;
  if (!configure_sockets() || !build_spdp()) {
    shutdown();
    context_ = nullptr;
    return false;
  }
  return true;
}

bool NetworkRuntime::start() noexcept {
  if ((context_ == nullptr) || started_.load()) {
    return false;
  }
  stop_.store(false);
  try {
    worker_ = std::thread(&NetworkRuntime::run, this);
  } catch (...) {
    return false;
  }
  started_.store(true);
  return true;
}

void NetworkRuntime::notify() noexcept {
  {
    std::lock_guard<std::mutex> lock(wake_mutex_);
    ++wake_generation_;
    if (wake_generation_ == 0U) {
      wake_generation_ = 1U;
    }
  }
  wake_condition_.notify_one();
}

void NetworkRuntime::shutdown() noexcept {
  stop_.store(true);
  notify();
  if (worker_.joinable()) {
    worker_.join();
  }
  started_.store(false);
  user_socket_.close();
  metadata_socket_.close();
  multicast_socket_.close();
}

bool NetworkRuntime::configure_sockets() noexcept {
  using openrtdds::rtps::SpdpPortConfig;
  std::uint16_t multicast_port = 0U;
  if (!openrtdds::rtps::spdp_multicast_port(
          static_cast<std::uint32_t>(context_->actual_domain_id),
          SpdpPortConfig{}, multicast_port) ||
      !multicast_socket_.open().ok() ||
      !multicast_socket_.enable_address_reuse().ok() ||
      !multicast_socket_.bind({Ipv4Address::any(), multicast_port}).ok() ||
      !multicast_socket_.join_multicast(kSpdpGroup).ok()) {
    return false;
  }

  bool bound = false;
  for (std::uint32_t participant = 0U; participant < 120U; ++participant) {
    std::uint16_t metadata_port = 0U;
    if (!openrtdds::rtps::spdp_unicast_port(
            static_cast<std::uint32_t>(context_->actual_domain_id),
            participant, SpdpPortConfig{}, metadata_port) ||
        (metadata_port == std::numeric_limits<std::uint16_t>::max())) {
      break;
    }
    const auto user_port = static_cast<std::uint16_t>(metadata_port + 1U);
    metadata_socket_.close();
    user_socket_.close();
    if (!metadata_socket_.open().ok() ||
        !metadata_socket_.bind({Ipv4Address::any(), metadata_port}).ok() ||
        !user_socket_.open().ok() ||
        !user_socket_.bind({Ipv4Address::any(), user_port}).ok()) {
      continue;
    }
    context_->impl->participant_id = participant;
    context_->impl->metadata_port = metadata_port;
    context_->impl->user_port = user_port;
    bound = true;
    break;
  }
  if (!bound) {
    return false;
  }

  auto& prefix = context_->impl->participant_prefix.value;
  const auto process = static_cast<std::uint32_t>(::getpid());
  prefix = {{0x4FU, 0x52U, 0x54U, 0x44U, 0x44U, 0x53U,
             static_cast<std::uint8_t>(context_->actual_domain_id),
             static_cast<std::uint8_t>(context_->impl->participant_id),
             static_cast<std::uint8_t>(process >> 24U),
             static_cast<std::uint8_t>(process >> 16U),
             static_cast<std::uint8_t>(process >> 8U),
             static_cast<std::uint8_t>(process)}};
  spdp_destination_ = {kSpdpGroup, multicast_port};
  return true;
}

bool NetworkRuntime::build_spdp() noexcept {
  openrtdds::rtps::Locator metadata_locator{};
  openrtdds::rtps::Locator user_locator{};
  openrtdds::rtps::Locator multicast_locator{};
  if (!openrtdds::rtps::make_udp_v4_locator(
          kLoopback.octets, context_->impl->metadata_port,
          metadata_locator) ||
      !openrtdds::rtps::make_udp_v4_locator(
          kLoopback.octets, context_->impl->user_port, user_locator) ||
      !openrtdds::rtps::make_udp_v4_locator(
          kSpdpGroup.octets, spdp_destination_.port, multicast_locator)) {
    return false;
  }

  openrtdds::rtps::SpdpAnnouncementConfig config{};
  config.participant.protocol_version = {2U, 3U};
  config.participant.vendor_id = {{0x01U, 0x42U}};
  config.participant.guid_prefix = context_->impl->participant_prefix;
  config.participant.domain_id =
      static_cast<std::uint32_t>(context_->actual_domain_id);
  config.participant.available_builtin_endpoints =
      openrtdds::rtps::spdp_endpoint_participant_announcer |
      openrtdds::rtps::spdp_endpoint_participant_detector |
      openrtdds::rtps::spdp_endpoint_publications_announcer |
      openrtdds::rtps::spdp_endpoint_publications_detector |
      openrtdds::rtps::spdp_endpoint_subscriptions_announcer |
      openrtdds::rtps::spdp_endpoint_subscriptions_detector;
  config.participant.lease_duration_ns = kRemoteStateLifetimeNs;
  if (!config.participant.metatraffic_unicast.push_back(metadata_locator) ||
      !config.participant.metatraffic_multicast.push_back(multicast_locator) ||
      !config.participant.default_unicast.push_back(user_locator)) {
    return false;
  }
  constexpr char name[] = "rmw_openrtdds_cpp";
  config.participant.entity_name_size = sizeof(name) - 1U;
  std::memcpy(config.participant.entity_name.data(), name,
              config.participant.entity_name_size);
  openrtdds::rtps::SpdpMessageBuilder builder(spdp_.data(), spdp_.size());
  if (!builder.build(config)) {
    return false;
  }
  spdp_size_ = builder.size();
  return true;
}

void NetworkRuntime::run() noexcept {
  std::uint64_t next_announcement = 0U;
  while (!stop_.load()) {
    const std::uint64_t now = monotonic_now_ns();
    if (now >= next_announcement) {
      announce(now);
      next_announcement = now + kAnnouncementPeriodNs;
    }
    receive_discovery(now);
    receive_user_data(now);
    send_pending_samples();
    expire_remote_state(now);

    std::unique_lock<std::mutex> lock(wake_mutex_);
    const std::uint64_t observed = wake_generation_;
    static_cast<void>(wake_condition_.wait_for(
        lock, std::chrono::milliseconds(5), [this, observed] {
          return stop_.load() || (wake_generation_ != observed);
        }));
  }
}

void NetworkRuntime::announce(const std::uint64_t now_ns) noexcept {
  static_cast<void>(now_ns);
  static_cast<void>(metadata_socket_.send_to(
      spdp_destination_, spdp_.data(), spdp_size_));

  for (const auto& participant : participants_) {
    if (!participant.occupied) {
      continue;
    }
    UdpEndpoint remote{};
    if (!first_endpoint(participant.data.metatraffic_unicast, remote)) {
      continue;
    }
    {
      std::lock_guard<std::mutex> lock(
          context_->impl->publisher_registry_mutex);
      for (const auto& slot : context_->impl->publishers) {
        NetworkEndpointView view{};
        if (slot.occupied && publisher_network_view(slot.data, view)) {
          static_cast<void>(metadata_socket_.send_to(
              remote, view.sedp, view.sedp_size));
        }
      }
    }
    {
      std::lock_guard<std::mutex> lock(
          context_->impl->subscription_registry_mutex);
      for (const auto& slot : context_->impl->subscriptions) {
        NetworkEndpointView view{};
        if (slot.occupied && subscription_network_view(slot.data, view)) {
          static_cast<void>(metadata_socket_.send_to(
              remote, view.sedp, view.sedp_size));
        }
      }
    }
  }
}

void NetworkRuntime::receive_discovery(const std::uint64_t now_ns) noexcept {
  for (auto* socket : {&multicast_socket_, &metadata_socket_}) {
    for (std::size_t attempt = 0U; attempt < kMaximumReceivesPerSocket;
         ++attempt) {
      UdpEndpoint source{};
      const auto received = socket->receive_from(
          receive_buffer_.data(), receive_buffer_.size(), source);
      if (!received.ok()) {
        if (received.error == UdpError::would_block) {
          break;
        }
        continue;
      }
      openrtdds::rtps::SpdpMessageView participant_view{};
      const auto participant_result = openrtdds::rtps::parse_spdp_message(
          receive_buffer_.data(), received.bytes,
          static_cast<std::uint32_t>(context_->actual_domain_id),
          participant_view);
      if (participant_result.ok()) {
        if (participant_view.participant.guid_prefix.value ==
            context_->impl->participant_prefix.value) {
          continue;
        }
        RemoteParticipant* target = find_participant(
            participant_view.participant.guid_prefix);
        if (target == nullptr) {
          for (auto& candidate : participants_) {
            if (!candidate.occupied) {
              target = &candidate;
              break;
            }
          }
        }
        if (target != nullptr) {
          target->data = participant_view.participant;
          target->last_seen_ns = now_ns;
          target->occupied = true;
        }
        continue;
      }

      const GuidPrefix prefix = packet_prefix(receive_buffer_.data(),
                                               received.bytes);
      if (find_participant(prefix) == nullptr) {
        continue;
      }
      openrtdds::rtps::SedpMessageView endpoint_view{};
      if (!openrtdds::rtps::parse_sedp_message(
              receive_buffer_.data(), received.bytes, prefix,
              endpoint_view).ok()) {
        continue;
      }
      RemoteEndpoint* target = nullptr;
      for (auto& candidate : endpoints_) {
        if (candidate.occupied &&
            (candidate.data.participant_guid_prefix.value ==
             endpoint_view.endpoint.participant_guid_prefix.value) &&
            (candidate.data.endpoint_id == endpoint_view.endpoint.endpoint_id)) {
          target = &candidate;
          break;
        }
      }
      if (target == nullptr) {
        for (auto& candidate : endpoints_) {
          if (!candidate.occupied) {
            target = &candidate;
            break;
          }
        }
      }
      if (target != nullptr) {
        target->data = endpoint_view.endpoint;
        target->last_seen_ns = now_ns;
        target->occupied = true;
        recompute_matches();
      }
    }
  }
}

void NetworkRuntime::receive_user_data(const std::uint64_t now_ns) noexcept {
  static_cast<void>(now_ns);
  for (std::size_t attempt = 0U; attempt < kMaximumReceivesPerSocket;
       ++attempt) {
    UdpEndpoint source{};
    const auto received = user_socket_.receive_from(
        receive_buffer_.data(), receive_buffer_.size(), source);
    if (!received.ok()) {
      if (received.error == UdpError::would_block) {
        break;
      }
      continue;
    }
    openrtdds::rtps::DataMessageView data_view{};
    if (openrtdds::rtps::parse_data_message(
            receive_buffer_.data(), received.bytes, data_view) !=
        openrtdds::rtps::RtpsError::none) {
      continue;
    }
    const auto* remote = find_remote_writer(data_view.guid_prefix,
                                             data_view.writer_id);
    if (remote == nullptr) {
      continue;
    }
    const std::uint64_t received_time = realtime_now_ns();
    if (received_time == 0U) {
      continue;
    }
    std::lock_guard<std::mutex> lock(
        context_->impl->subscription_registry_mutex);
    for (const auto& slot : context_->impl->subscriptions) {
      NetworkEndpointView local{};
      if (!slot.occupied || !subscription_network_view(slot.data, local) ||
          (openrtdds::rtps::evaluate_endpoint_match(
               local.descriptor, remote->data) !=
           openrtdds::rtps::MatchStatus::matched)) {
        continue;
      }
      static_cast<void>(subscription_network_receive(
          slot.data, receive_buffer_.data(), received.bytes, received_time));
    }
  }
}

void NetworkRuntime::send_pending_samples() noexcept {
  std::lock_guard<std::mutex> lock(context_->impl->publisher_registry_mutex);
  for (std::size_t slot_index = 0U;
       slot_index < context_->impl->publishers.size(); ++slot_index) {
    const auto& slot = context_->impl->publishers[slot_index];
    if (!slot.occupied) {
      publisher_generations_[slot_index] = slot.generation;
      last_sent_sequence_[slot_index] = 0U;
      continue;
    }
    if (publisher_generations_[slot_index] != slot.generation) {
      publisher_generations_[slot_index] = slot.generation;
      last_sent_sequence_[slot_index] = 0U;
    }
    NetworkEndpointView local{};
    if (!publisher_network_view(slot.data, local)) {
      continue;
    }
    for (std::size_t sample_index = 0U;
         sample_index < kMaximumSamplesPerPublisherPass; ++sample_index) {
      NetworkSample sample{};
      if (!publisher_network_copy_next(
              slot.data, last_sent_sequence_[slot_index],
              transmit_buffer_.data(), transmit_buffer_.size(), sample)) {
        break;
      }
      for (const auto& remote : endpoints_) {
        if (!remote.occupied ||
            (remote.data.kind != EndpointKind::reader) ||
            (openrtdds::rtps::evaluate_endpoint_match(
                 local.descriptor, remote.data) !=
             openrtdds::rtps::MatchStatus::matched)) {
          continue;
        }
        UdpEndpoint destination{};
        if (first_endpoint(remote.data.unicast_locators, destination)) {
          static_cast<void>(user_socket_.send_to(
              destination, transmit_buffer_.data(), sample.packet_size));
        }
      }
      last_sent_sequence_[slot_index] = sample.sequence_number;
    }
  }
}

void NetworkRuntime::expire_remote_state(const std::uint64_t now_ns) noexcept {
  bool changed = false;
  for (auto& participant : participants_) {
    if (participant.occupied &&
        ((now_ns - participant.last_seen_ns) >= kRemoteStateLifetimeNs)) {
      const auto prefix = participant.data.guid_prefix;
      participant = {};
      for (auto& endpoint : endpoints_) {
        if (endpoint.occupied &&
            (endpoint.data.participant_guid_prefix.value == prefix.value)) {
          endpoint = {};
        }
      }
      changed = true;
    }
  }
  for (auto& endpoint : endpoints_) {
    if (endpoint.occupied &&
        ((now_ns - endpoint.last_seen_ns) >= kRemoteStateLifetimeNs)) {
      endpoint = {};
      changed = true;
    }
  }
  if (changed) {
    recompute_matches();
  }
}

void NetworkRuntime::recompute_matches() noexcept {
  {
    std::lock_guard<std::mutex> lock(
        context_->impl->publisher_registry_mutex);
    for (const auto& slot : context_->impl->publishers) {
      NetworkEndpointView local{};
      if (!slot.occupied || !publisher_network_view(slot.data, local)) {
        continue;
      }
      std::size_t matches = 0U;
      for (const auto& remote : endpoints_) {
        if (remote.occupied && (remote.data.kind == EndpointKind::reader) &&
            (openrtdds::rtps::evaluate_endpoint_match(
                 local.descriptor, remote.data) ==
             openrtdds::rtps::MatchStatus::matched)) {
          ++matches;
        }
      }
      publisher_network_set_match_count(slot.data, matches);
    }
  }
  {
    std::lock_guard<std::mutex> lock(
        context_->impl->subscription_registry_mutex);
    for (const auto& slot : context_->impl->subscriptions) {
      NetworkEndpointView local{};
      if (!slot.occupied || !subscription_network_view(slot.data, local)) {
        continue;
      }
      std::size_t matches = 0U;
      for (const auto& remote : endpoints_) {
        if (remote.occupied && (remote.data.kind == EndpointKind::writer) &&
            (openrtdds::rtps::evaluate_endpoint_match(
                 local.descriptor, remote.data) ==
             openrtdds::rtps::MatchStatus::matched)) {
          ++matches;
        }
      }
      subscription_network_set_match_count(slot.data, matches);
    }
  }
}

NetworkRuntime::RemoteParticipant* NetworkRuntime::find_participant(
    const GuidPrefix& prefix) noexcept {
  for (auto& participant : participants_) {
    if (participant.occupied &&
        (participant.data.guid_prefix.value == prefix.value)) {
      return &participant;
    }
  }
  return nullptr;
}

const NetworkRuntime::RemoteEndpoint* NetworkRuntime::find_remote_writer(
    const GuidPrefix& prefix,
    const openrtdds::rtps::EntityId& entity) const noexcept {
  for (const auto& endpoint : endpoints_) {
    if (endpoint.occupied && (endpoint.data.kind == EndpointKind::writer) &&
        (endpoint.data.participant_guid_prefix.value == prefix.value) &&
        (endpoint.data.endpoint_id == entity)) {
      return &endpoint;
    }
  }
  return nullptr;
}

void notify_network(rmw_context_t* const context) noexcept {
  if (context_is_valid(context) && (context->impl->network != nullptr)) {
    context->impl->network->notify();
  }
}

}  // namespace rmw_openrtdds_cpp::detail
