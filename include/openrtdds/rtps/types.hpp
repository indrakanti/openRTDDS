#pragma once

#include <array>
#include <cstdint>

namespace openrtdds::rtps {

struct ProtocolVersion final {
  std::uint8_t major{2U};
  std::uint8_t minor{5U};
};

struct VendorId final {
  std::array<std::uint8_t, 2U> value{};
};

struct GuidPrefix final {
  std::array<std::uint8_t, 12U> value{};
};

struct EntityId final {
  std::array<std::uint8_t, 4U> value{};
};

[[nodiscard]] constexpr bool operator==(const EntityId& left,
                                        const EntityId& right) noexcept {
  return (left.value[0] == right.value[0]) &&
         (left.value[1] == right.value[1]) &&
         (left.value[2] == right.value[2]) &&
         (left.value[3] == right.value[3]);
}

[[nodiscard]] constexpr bool operator!=(const EntityId& left,
                                        const EntityId& right) noexcept {
  return !(left == right);
}

}  // namespace openrtdds::rtps
