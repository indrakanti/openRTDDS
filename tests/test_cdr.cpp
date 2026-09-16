#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "openrtdds/serialization/cdr.hpp"
#include "test_support.hpp"

namespace {

using openrtdds::serialization::ByteOrder;
using openrtdds::serialization::CdrError;
using openrtdds::serialization::CdrReader;
using openrtdds::serialization::CdrWriter;

void test_little_endian_golden_payload() {
  std::array<std::uint8_t, 32U> buffer{};
  CdrWriter writer(buffer.data(), buffer.size());
  CHECK(writer.begin(ByteOrder::little_endian));
  CHECK(writer.write_uint64(0x0102030405060708ULL));
  CHECK(writer.write_float32(1.5F));
  CHECK(writer.write_float32(-0.5F));
  CHECK(writer.size() == 20U);

  const std::array<std::uint8_t, 20U> expected{{
      0x00U, 0x01U, 0x00U, 0x00U,  // CDR_LE encapsulation
      0x08U, 0x07U, 0x06U, 0x05U, 0x04U, 0x03U, 0x02U, 0x01U,
      0x00U, 0x00U, 0xC0U, 0x3FU,
      0x00U, 0x00U, 0x00U, 0xBFU,
  }};
  CHECK(std::memcmp(buffer.data(), expected.data(), expected.size()) == 0);

  CdrReader reader(buffer.data(), writer.size());
  std::uint64_t timestamp = 0U;
  float velocity = 0.0F;
  float yaw_rate = 0.0F;
  CHECK(reader.begin());
  CHECK(reader.byte_order() == ByteOrder::little_endian);
  CHECK(reader.read_uint64(timestamp));
  CHECK(reader.read_float32(velocity));
  CHECK(reader.read_float32(yaw_rate));
  CHECK(timestamp == 0x0102030405060708ULL);
  CHECK(velocity == 1.5F);
  CHECK(yaw_rate == -0.5F);
  CHECK(reader.remaining() == 0U);
}

void test_big_endian_and_alignment() {
  std::array<std::uint8_t, 32U> buffer{};
  CdrWriter writer(buffer.data(), buffer.size());
  CHECK(writer.begin(ByteOrder::big_endian));
  CHECK(writer.write_uint8(0xAAU));
  CHECK(writer.write_uint32(0x01020304U));
  CHECK(writer.size() == 12U);

  const std::array<std::uint8_t, 12U> expected{{
      0x00U, 0x00U, 0x00U, 0x00U,
      0xAAU, 0x00U, 0x00U, 0x00U,
      0x01U, 0x02U, 0x03U, 0x04U,
  }};
  CHECK(std::memcmp(buffer.data(), expected.data(), expected.size()) == 0);

  CdrReader reader(buffer.data(), writer.size());
  std::uint8_t first = 0U;
  std::uint32_t second = 0U;
  CHECK(reader.begin());
  CHECK(reader.read_uint8(first));
  CHECK(reader.read_uint32(second));
  CHECK(first == 0xAAU);
  CHECK(second == 0x01020304U);
}

void test_bounds_and_atomic_failure() {
  std::array<std::uint8_t, 8U> buffer{};
  CdrWriter writer(buffer.data(), buffer.size());
  CHECK(writer.begin(ByteOrder::little_endian));
  CHECK(writer.write_uint32(0x12345678U));
  const std::size_t size_before_failure = writer.size();
  CHECK(!writer.write_uint8(0xFFU));
  CHECK(writer.error() == CdrError::overflow);
  CHECK(writer.size() == size_before_failure);
  CHECK(!writer.write_uint8(0xEEU));

  const std::array<std::uint8_t, 7U> truncated{{
      0x00U, 0x01U, 0x00U, 0x00U, 0x01U, 0x02U, 0x03U,
  }};
  CdrReader reader(truncated.data(), truncated.size());
  std::uint32_t value = 99U;
  CHECK(reader.begin());
  const std::size_t position_before_failure = reader.position();
  CHECK(!reader.read_uint32(value));
  CHECK(reader.error() == CdrError::underflow);
  CHECK(reader.position() == position_before_failure);
  CHECK(value == 99U);
}

void test_bounded_string() {
  std::array<std::uint8_t, 32U> buffer{};
  CdrWriter writer(buffer.data(), buffer.size());
  CHECK(writer.begin(ByteOrder::little_endian));
  CHECK(writer.write_string("dds", 3U, 8U));

  CdrReader reader(buffer.data(), writer.size());
  std::array<char, 9U> value{};
  std::size_t length = 0U;
  CHECK(reader.begin());
  CHECK(reader.read_string(value.data(), value.size(), 8U, length));
  CHECK(length == 3U);
  CHECK(std::strcmp(value.data(), "dds") == 0);

  CHECK(writer.begin(ByteOrder::little_endian));
  CHECK(!writer.write_string("too-long", 8U, 4U));
  CHECK(writer.error() == CdrError::bound_exceeded);
  CHECK(writer.size() == 4U);
}

void test_rejects_unsupported_encapsulation() {
  const std::array<std::uint8_t, 4U> cdr2_header{{0x00U, 0x07U, 0x00U,
                                                  0x00U}};
  CdrReader reader(cdr2_header.data(), cdr2_header.size());
  CHECK(!reader.begin());
  CHECK(reader.error() == CdrError::unsupported_encoding);
}

}  // namespace

void test_cdr() {
  test_little_endian_golden_payload();
  test_big_endian_and_alignment();
  test_bounds_and_atomic_failure();
  test_bounded_string();
  test_rejects_unsupported_encapsulation();
}
