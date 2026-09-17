#include <array>
#include <cstdint>
#include <iostream>

#include "openrtdds/serialization/cdr.hpp"

// Demonstrates: ORT-SER-001, ORT-SER-002, ORT-SER-003, ORT-SER-004

int main() {
  std::array<std::uint8_t, 32U> storage{};
  openrtdds::serialization::CdrWriter writer(storage.data(), storage.size());
  if (!writer.begin(openrtdds::serialization::ByteOrder::little_endian) ||
      !writer.write_uint64(42U) || !writer.write_float32(12.5F) ||
      !writer.write_float32(-0.25F)) {
    return 1;
  }

  openrtdds::serialization::CdrReader reader(storage.data(), writer.size());
  std::uint64_t timestamp = 0U;
  float velocity = 0.0F;
  float yaw_rate = 0.0F;
  if (!reader.begin() || !reader.read_uint64(timestamp) ||
      !reader.read_float32(velocity) || !reader.read_float32(yaw_rate)) {
    return 1;
  }

  std::cout << "VehicleState{timestamp=" << timestamp
            << ", velocity=" << velocity << ", yaw_rate=" << yaw_rate
            << "}\n";
  return (timestamp == 42U && velocity == 12.5F && yaw_rate == -0.25F) ? 0
                                                                        : 1;
}
