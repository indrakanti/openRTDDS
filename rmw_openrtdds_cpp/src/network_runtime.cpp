#include "network_runtime.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>
#include <new>
#include <time.h>
#include <unistd.h>

#include "internal.hpp"
#include "openrtdds/rtps/data_message.hpp"
#include "openrtdds/rtps/message_router.hpp"
#include "openrtdds/rtps/reliability_messages.hpp"

namespace rmw_openrtdds_cpp::detail {
namespace {

using openrtdds::rtps::EndpointKind;
using openrtdds::rtps::GuidPrefix;
using openrtdds::rtps::Locator;
using openrtdds::rtps::ReliabilityActionBuffer;
using openrtdds::rtps::ReliabilityActionKind;
using openrtdds::rtps::ReliabilityError;
using openrtdds::rtps::ReliabilityMessageError;
using openrtdds::transport::Ipv4Address;
using openrtdds::transport::UdpEndpoint;
using openrtdds::transport::UdpError;

constexpr Ipv4Address kLoopback{{{127U, 0U, 0U, 1U}}};
constexpr Ipv4Address kSpdpGroup{{{239U, 255U, 0U, 1U}}};
constexpr std::uint64_t kAnnouncementPeriodNs = 250'000'000ULL;
constexpr std::uint64_t kRemoteStateLifetimeNs = 5'000'000'000ULL;
constexpr std::uint64_t kHeartbeatPeriodNs = 100'000'000ULL;
constexpr std::uint64_t kRepairWindowNs = 2'000'000'000ULL;
constexpr std::size_t kMaximumReceivesPerSocket = 32U;
constexpr std::size_t kMaximumSamplesPerPublisherPass = 32U;
constexpr openrtdds::rtps::ProtocolVersion kProtocol{2U, 3U};
constexpr openrtdds::rtps::VendorId kVendor{{0x01U, 0x42U}};
constexpr openrtdds::rtps::EntityId kUnknownReader{{0U, 0U, 0U, 0U}};

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

void write_u16_le(std::uint8_t* const output,
                  const std::uint16_t value) noexcept {
  output[0] = static_cast<std::uint8_t>(value & 0xFFU);
  output[1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}

[[nodiscard]] bool direct_message(
    const std::uint8_t* const message, const std::size_t message_size,
    const GuidPrefix& destination, std::uint8_t* const output,
    const std::size_t output_capacity, std::size_t& output_size) noexcept {
  constexpr std::size_t rtps_header_size = 20U;
  constexpr std::size_t info_destination_size = 16U;
  if ((message == nullptr) || (output == nullptr) ||
      (output_capacity < info_destination_size) ||
      (message_size < rtps_header_size) ||
      (message_size > (output_capacity - info_destination_size))) {
    output_size = 0U;
    return false;
  }
  std::memcpy(output, message, rtps_header_size);
  output[20] = openrtdds::rtps::submessage_id_info_destination;
  output[21] = 0x01U;
  write_u16_le(&output[22], 12U);
  std::memcpy(&output[24], destination.value.data(), destination.value.size());
  std::memcpy(&output[36], &message[rtps_header_size],
              message_size - rtps_header_size);
  output_size = message_size + info_destination_size;
  return true;
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

void NetworkRuntime::drop_next_reliable_data_for_test(
    const std::uint32_t count) noexcept {
  test_drop_reliable_data_.store(count);
  notify();
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
    send_pending_samples(now);
    service_reliable_writers(now);
    prune_reliable_links();
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
    const std::uint64_t received_time = realtime_now_ns();
    if (received_time != 0U) {
      handle_data(receive_buffer_.data(), received.bytes, received_time);
    }
    handle_heartbeat(receive_buffer_.data(), received.bytes);
    handle_acknack(receive_buffer_.data(), received.bytes, now_ns);
  }
}

void NetworkRuntime::send_pending_samples(const std::uint64_t now_ns) noexcept {
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
            (remote.data.reliability ==
             openrtdds::rtps::ReliabilityKind::reliable) ||
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

    if (local.descriptor.reliability !=
        openrtdds::rtps::ReliabilityKind::reliable) {
      continue;
    }
    for (const auto& remote : endpoints_) {
      if (!remote.occupied ||
          (remote.data.kind != EndpointKind::reader) ||
          (remote.data.reliability !=
           openrtdds::rtps::ReliabilityKind::reliable) ||
          (openrtdds::rtps::evaluate_endpoint_match(
               local.descriptor, remote.data) !=
           openrtdds::rtps::MatchStatus::matched)) {
        continue;
      }
      ReliableWriterLink* const link =
          writer_link(slot_index, slot.generation, local, remote);
      if ((link == nullptr) || !link->state.empty()) {
        continue;
      }
      NetworkSample sample{};
      if (!publisher_network_copy_next(
              slot.data, link->last_queued_sequence,
              transmit_buffer_.data(), transmit_buffer_.size(), sample)) {
        continue;
      }
      ReliabilityActionBuffer<4U> actions{};
      if (link->state.write(transmit_buffer_.data(), sample.packet_size,
                            sample.sequence_number, now_ns, actions) !=
              ReliabilityError::none ||
          (actions.size() != 1U) ||
          (actions[0U].kind != ReliabilityActionKind::send_data)) {
        continue;
      }
      link->last_queued_sequence = sample.sequence_number;
      link->next_heartbeat_ns = now_ns;
      std::uint32_t drops = test_drop_reliable_data_.load();
      bool drop = false;
      while ((drops != 0U) &&
             !test_drop_reliable_data_.compare_exchange_weak(drops,
                                                              drops - 1U)) {
      }
      drop = drops != 0U;
      if (!drop) {
        static_cast<void>(user_socket_.send_to(
            link->destination, actions[0U].data, actions[0U].data_size));
      }
    }
  }
}

void NetworkRuntime::handle_data(
    const std::uint8_t* const packet, const std::size_t packet_size,
    const std::uint64_t received_timestamp_ns) noexcept {
  openrtdds::rtps::DataMessageView data{};
  if (openrtdds::rtps::parse_data_message(packet, packet_size, data) !=
      openrtdds::rtps::RtpsError::none) {
    return;
  }
  const RemoteEndpoint* const remote =
      find_remote_writer(data.guid_prefix, data.writer_id);
  if (remote == nullptr) {
    return;
  }

  std::lock_guard<std::mutex> lock(
      context_->impl->subscription_registry_mutex);
  for (std::size_t slot_index = 0U;
       slot_index < context_->impl->subscriptions.size(); ++slot_index) {
    const auto& slot = context_->impl->subscriptions[slot_index];
    NetworkEndpointView local{};
    if (!slot.occupied || !subscription_network_view(slot.data, local) ||
        (openrtdds::rtps::evaluate_endpoint_match(local.descriptor,
                                                   remote->data) !=
         openrtdds::rtps::MatchStatus::matched)) {
      continue;
    }
    if ((local.descriptor.reliability ==
         openrtdds::rtps::ReliabilityKind::reliable) &&
        (remote->data.reliability ==
         openrtdds::rtps::ReliabilityKind::reliable)) {
      ReliableReaderLink* const link = reader_link(
          slot_index, slot.generation, local, *remote, data.sequence_number);
      if (link == nullptr) {
        continue;
      }
      ReliabilityActionBuffer<4U> actions{};
      const ReliabilityError error =
          link->state.on_data(data.sequence_number, actions);
      if (error == ReliabilityError::duplicate_data ||
          error == ReliabilityError::stale_data) {
        continue;
      }
      if ((error != ReliabilityError::none) || (actions.size() != 1U) ||
          (actions[0U].kind != ReliabilityActionKind::sample_received)) {
        continue;
      }
    }
    static_cast<void>(subscription_network_receive(
        slot.data, packet, packet_size, received_timestamp_ns));
  }
}

void NetworkRuntime::handle_heartbeat(
    const std::uint8_t* const packet,
    const std::size_t packet_size) noexcept {
  openrtdds::rtps::HeartbeatView heartbeat{};
  if (openrtdds::rtps::parse_heartbeat_message(packet, packet_size,
                                                heartbeat) !=
      ReliabilityMessageError::none) {
    return;
  }
  const RemoteEndpoint* const remote = find_remote_writer(
      heartbeat.header.guid_prefix, heartbeat.writer_id);
  if ((remote == nullptr) ||
      (remote->data.reliability !=
       openrtdds::rtps::ReliabilityKind::reliable)) {
    return;
  }

  std::lock_guard<std::mutex> lock(
      context_->impl->subscription_registry_mutex);
  for (std::size_t slot_index = 0U;
       slot_index < context_->impl->subscriptions.size(); ++slot_index) {
    const auto& slot = context_->impl->subscriptions[slot_index];
    NetworkEndpointView local{};
    if (!slot.occupied || !subscription_network_view(slot.data, local) ||
        (local.descriptor.reliability !=
         openrtdds::rtps::ReliabilityKind::reliable) ||
        (openrtdds::rtps::evaluate_endpoint_match(local.descriptor,
                                                   remote->data) !=
         openrtdds::rtps::MatchStatus::matched) ||
        ((heartbeat.reader_id != kUnknownReader) &&
         (heartbeat.reader_id != local.entity_id))) {
      continue;
    }
    ReliableReaderLink* const link = reader_link(
        slot_index, slot.generation, local, *remote,
        heartbeat.first_sequence_number);
    if (link == nullptr) {
      continue;
    }
    openrtdds::rtps::HeartbeatView directed = heartbeat;
    directed.reader_id = local.entity_id;
    ReliabilityActionBuffer<4U> actions{};
    const ReliabilityError error = link->state.on_heartbeat(directed, actions);
    if ((error != ReliabilityError::none) &&
        (error != ReliabilityError::stale_control)) {
      continue;
    }
    for (std::size_t index = 0U; index < actions.size(); ++index) {
      if (actions[index].kind == ReliabilityActionKind::send_acknack) {
        static_cast<void>(send_acknack(*link, actions[index]));
      }
    }
  }
}

void NetworkRuntime::handle_acknack(
    const std::uint8_t* const packet, const std::size_t packet_size,
    const std::uint64_t now_ns) noexcept {
  openrtdds::rtps::AckNackView acknack{};
  if (openrtdds::rtps::parse_acknack_message(packet, packet_size, acknack) !=
      ReliabilityMessageError::none) {
    return;
  }
  const RemoteEndpoint* const remote = find_remote_reader(
      acknack.header.guid_prefix, acknack.reader_id);
  if ((remote == nullptr) ||
      (remote->data.reliability !=
       openrtdds::rtps::ReliabilityKind::reliable)) {
    return;
  }

  std::lock_guard<std::mutex> lock(context_->impl->publisher_registry_mutex);
  for (auto& link : reliable_writers_) {
    if (!link.occupied ||
        (link.remote_prefix.value != acknack.header.guid_prefix.value) ||
        (link.remote_reader != acknack.reader_id) ||
        (link.local_writer != acknack.writer_id) ||
        (link.local_slot >= context_->impl->publishers.size())) {
      continue;
    }
    const auto& slot = context_->impl->publishers[link.local_slot];
    if (!slot.occupied || (slot.generation != link.local_generation)) {
      link.occupied = false;
      continue;
    }
    ReliabilityActionBuffer<4U> actions{};
    const ReliabilityError error =
        link.state.on_acknack(acknack, now_ns, actions);
    if ((error != ReliabilityError::none) &&
        (error != ReliabilityError::stale_control)) {
      return;
    }
    for (std::size_t index = 0U; index < actions.size(); ++index) {
      if (actions[index].kind == ReliabilityActionKind::retransmit_data) {
        static_cast<void>(user_socket_.send_to(
            link.destination, actions[index].data, actions[index].data_size));
        link.next_heartbeat_ns = now_ns;
      }
    }
    return;
  }
}

void NetworkRuntime::service_reliable_writers(
    const std::uint64_t now_ns) noexcept {
  for (auto& link : reliable_writers_) {
    if (!link.occupied) {
      continue;
    }
    ReliabilityActionBuffer<4U> actions{};
    if (link.state.on_timer(now_ns, actions) != ReliabilityError::none) {
      link.occupied = false;
      continue;
    }
    if (!link.state.empty() && (now_ns >= link.next_heartbeat_ns)) {
      static_cast<void>(send_heartbeat(link));
      link.next_heartbeat_ns = now_ns + kHeartbeatPeriodNs;
    }
  }
}

NetworkRuntime::ReliableWriterLink* NetworkRuntime::writer_link(
    const std::size_t local_slot, const std::uint32_t local_generation,
    const NetworkEndpointView& local,
    const RemoteEndpoint& remote) noexcept {
  ReliableWriterLink* available = nullptr;
  for (auto& link : reliable_writers_) {
    if (!link.occupied) {
      if (available == nullptr) {
        available = &link;
      }
      continue;
    }
    if ((link.local_slot == local_slot) &&
        (link.local_generation == local_generation) &&
        (link.local_writer == local.entity_id) &&
        (link.remote_prefix.value ==
         remote.data.participant_guid_prefix.value) &&
        (link.remote_reader == remote.data.endpoint_id)) {
      static_cast<void>(first_endpoint(remote.data.unicast_locators,
                                       link.destination));
      return &link;
    }
  }
  UdpEndpoint destination{};
  if ((available == nullptr) ||
      !first_endpoint(remote.data.unicast_locators, destination)) {
    return nullptr;
  }
  openrtdds::rtps::ReliableWriterConfig config{};
  config.reader_id = remote.data.endpoint_id;
  config.writer_id = local.entity_id;
  config.max_repair_attempts = 2U;
  config.repair_window_ns = kRepairWindowNs;
  available->state.~ReliableWriterState();
  new (&available->state) ReliableWriterState(config);
  available->remote_prefix = remote.data.participant_guid_prefix;
  available->remote_reader = remote.data.endpoint_id;
  available->local_writer = local.entity_id;
  available->destination = destination;
  available->last_queued_sequence = 0U;
  available->next_heartbeat_ns = 0U;
  available->local_slot = local_slot;
  available->local_generation = local_generation;
  available->occupied = true;
  return available;
}

NetworkRuntime::ReliableReaderLink* NetworkRuntime::reader_link(
    const std::size_t local_slot, const std::uint32_t local_generation,
    const NetworkEndpointView& local, const RemoteEndpoint& remote,
    const std::uint64_t initial_sequence) noexcept {
  ReliableReaderLink* available = nullptr;
  for (auto& link : reliable_readers_) {
    if (!link.occupied) {
      if (available == nullptr) {
        available = &link;
      }
      continue;
    }
    if ((link.local_slot == local_slot) &&
        (link.local_generation == local_generation) &&
        (link.local_reader == local.entity_id) &&
        (link.remote_prefix.value ==
         remote.data.participant_guid_prefix.value) &&
        (link.remote_writer == remote.data.endpoint_id)) {
      if (!link.initialized && (initial_sequence != 0U)) {
        openrtdds::rtps::ReliableReaderConfig config{};
        config.reader_id = local.entity_id;
        config.writer_id = remote.data.endpoint_id;
        config.initial_sequence_number = initial_sequence;
        link.state.~ReliableReaderState();
        new (&link.state) ReliableReaderState(config);
        link.initialized = true;
      }
      static_cast<void>(first_endpoint(remote.data.unicast_locators,
                                       link.destination));
      return link.initialized ? &link : nullptr;
    }
  }
  UdpEndpoint destination{};
  if ((available == nullptr) || (initial_sequence == 0U) ||
      !first_endpoint(remote.data.unicast_locators, destination)) {
    return nullptr;
  }
  openrtdds::rtps::ReliableReaderConfig config{};
  config.reader_id = local.entity_id;
  config.writer_id = remote.data.endpoint_id;
  config.initial_sequence_number = initial_sequence;
  available->state.~ReliableReaderState();
  new (&available->state) ReliableReaderState(config);
  available->remote_prefix = remote.data.participant_guid_prefix;
  available->remote_writer = remote.data.endpoint_id;
  available->local_reader = local.entity_id;
  available->destination = destination;
  available->local_slot = local_slot;
  available->local_generation = local_generation;
  available->initialized = true;
  available->occupied = true;
  return available;
}

bool NetworkRuntime::send_heartbeat(ReliableWriterLink& link) noexcept {
  openrtdds::rtps::HeartbeatConfig heartbeat{};
  heartbeat.header.version = kProtocol;
  heartbeat.header.vendor_id = kVendor;
  heartbeat.header.guid_prefix = context_->impl->participant_prefix;
  heartbeat.final_flag = false;
  if (link.state.fill_heartbeat(heartbeat) != ReliabilityError::none) {
    return false;
  }
  openrtdds::rtps::ReliabilityMessageBuilder builder(
      control_buffer_.data(), control_buffer_.size());
  std::size_t directed_size = 0U;
  if (!builder.build_heartbeat(heartbeat) ||
      !direct_message(builder.data(), builder.size(), link.remote_prefix,
                      directed_control_buffer_.data(),
                      directed_control_buffer_.size(), directed_size)) {
    return false;
  }
  return user_socket_.send_to(link.destination,
                              directed_control_buffer_.data(),
                              directed_size).ok();
}

bool NetworkRuntime::send_acknack(
    const ReliableReaderLink& link,
    const openrtdds::rtps::ReliabilityAction& action) noexcept {
  openrtdds::rtps::AckNackConfig acknack{};
  acknack.header.version = kProtocol;
  acknack.header.vendor_id = kVendor;
  acknack.header.guid_prefix = context_->impl->participant_prefix;
  acknack.reader_id = action.reader_id;
  acknack.writer_id = action.writer_id;
  acknack.reader_state = action.reader_state;
  acknack.count = action.control_count;
  acknack.final_flag = action.final_flag;
  openrtdds::rtps::ReliabilityMessageBuilder builder(
      control_buffer_.data(), control_buffer_.size());
  std::size_t directed_size = 0U;
  if (!builder.build_acknack(acknack) ||
      !direct_message(builder.data(), builder.size(), link.remote_prefix,
                      directed_control_buffer_.data(),
                      directed_control_buffer_.size(), directed_size)) {
    return false;
  }
  return user_socket_.send_to(link.destination,
                              directed_control_buffer_.data(),
                              directed_size).ok();
}

void NetworkRuntime::prune_reliable_links() noexcept {
  {
    std::lock_guard<std::mutex> lock(context_->impl->publisher_registry_mutex);
    for (auto& link : reliable_writers_) {
      if (!link.occupied) {
        continue;
      }
      const bool local_valid =
          (link.local_slot < context_->impl->publishers.size()) &&
          context_->impl->publishers[link.local_slot].occupied &&
          (context_->impl->publishers[link.local_slot].generation ==
           link.local_generation);
      if (!local_valid ||
          (find_remote_reader(link.remote_prefix, link.remote_reader) ==
           nullptr)) {
        link.occupied = false;
      }
    }
  }
  {
    std::lock_guard<std::mutex> lock(
        context_->impl->subscription_registry_mutex);
    for (auto& link : reliable_readers_) {
      if (!link.occupied) {
        continue;
      }
      const bool local_valid =
          (link.local_slot < context_->impl->subscriptions.size()) &&
          context_->impl->subscriptions[link.local_slot].occupied &&
          (context_->impl->subscriptions[link.local_slot].generation ==
           link.local_generation);
      if (!local_valid ||
          (find_remote_writer(link.remote_prefix, link.remote_writer) ==
           nullptr)) {
        link.occupied = false;
      }
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

const NetworkRuntime::RemoteEndpoint* NetworkRuntime::find_remote_reader(
    const GuidPrefix& prefix,
    const openrtdds::rtps::EntityId& entity) const noexcept {
  for (const auto& endpoint : endpoints_) {
    if (endpoint.occupied && (endpoint.data.kind == EndpointKind::reader) &&
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

void network_test_drop_next_reliable_data(
    rmw_context_t* const context, const std::uint32_t count) noexcept {
  if (context_is_valid(context) && (context->impl->network != nullptr)) {
    context->impl->network->drop_next_reliable_data_for_test(count);
  }
}

}  // namespace rmw_openrtdds_cpp::detail
