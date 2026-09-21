#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>

#include "openrtdds/rtps/sedp.hpp"

// Demonstrates: ORT-SEDP-001, ORT-SEDP-003, ORT-SEDP-004,
// Demonstrates: ORT-SEDP-005, ORT-SEDP-006

int main() {
  using namespace openrtdds::rtps;
  SedpAnnouncementConfig publication{};
  publication.endpoint.vendor_id.value = {{0x01U, 0x42U}};
  for (std::size_t index = 0U;
       index < publication.endpoint.participant_guid_prefix.value.size();
       ++index) {
    publication.endpoint.participant_guid_prefix.value[index] =
        static_cast<std::uint8_t>(0x20U + index);
  }
  publication.endpoint.endpoint_id = {{0U, 0U, 0x10U, 0x02U}};
  publication.endpoint.kind = EndpointKind::writer;
  constexpr char topic[] = "VehicleState";
  constexpr char type[] = "openrtdds::VehicleState";
  publication.endpoint.topic_name_size = sizeof(topic) - 1U;
  publication.endpoint.type_name_size = sizeof(type) - 1U;
  std::memcpy(publication.endpoint.topic_name.data(), topic,
              publication.endpoint.topic_name_size);
  std::memcpy(publication.endpoint.type_name.data(), type,
              publication.endpoint.type_name_size);
  publication.endpoint.reliability = ReliabilityKind::reliable;
  Locator locator{};
  if (!make_udp_v4_locator({{127U, 0U, 0U, 1U}}, 9200U, locator) ||
      !publication.endpoint.unicast_locators.push_back(locator)) {
    return 1;
  }

  std::array<std::uint8_t, 1600U> datagram{};
  SedpMessageBuilder builder(datagram.data(), datagram.size());
  if (!builder.build(publication)) {
    std::cerr << to_string(builder.error()) << '\n';
    return 1;
  }

  SedpMessageView discovered{};
  const SedpResult parsed = parse_sedp_message(
      datagram.data(), builder.size(),
      publication.endpoint.participant_guid_prefix, discovered);
  if (!parsed.ok()) {
    std::cerr << to_string(parsed.error) << '\n';
    return 1;
  }
  DiscoveredEndpointTable<4U> endpoints{};
  if (!endpoints.upsert(discovered).ok()) {
    return 1;
  }

  LocalEndpointDescriptor subscription{};
  subscription.kind = EndpointKind::reader;
  subscription.topic_name = topic;
  subscription.topic_name_size = sizeof(topic) - 1U;
  subscription.type_name = type;
  subscription.type_name_size = sizeof(type) - 1U;
  subscription.reliability = ReliabilityKind::reliable;
  const MatchStatus match = evaluate_endpoint_match(
      subscription, discovered.endpoint);
  std::cout << "endpoint=" << endpoints.size()
            << " match=" << to_string(match)
            << " bytes=" << builder.size() << '\n';
  return match == MatchStatus::matched ? 0 : 1;
}
