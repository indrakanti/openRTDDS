#pragma once

#include <cstddef>
#include <cstdint>

namespace openrtdds::serialization {

// Requirements: ORT-SER-001, ORT-SER-002, ORT-SER-003, ORT-SER-004,
// Requirements: ORT-SER-005, ORT-SER-006

enum class ByteOrder : std::uint8_t {
  big_endian = 0,
  little_endian = 1,
};

enum class CdrError : std::uint8_t {
  none = 0,
  invalid_argument,
  not_initialized,
  overflow,
  underflow,
  unsupported_encoding,
  invalid_data,
  bound_exceeded,
};

[[nodiscard]] const char* to_string(CdrError error) noexcept;

// Bounded writer for the XCDR1 PLAIN_CDR representation used by final and
// appendable DDS types. No allocation occurs. Errors are sticky until begin()
// starts a new payload.
class CdrWriter final {
 public:
  CdrWriter(std::uint8_t* buffer, std::size_t capacity) noexcept;

  // Writes the four-byte RTPS encapsulation header and resets alignment at the
  // first byte following it, as required by DDSI-RTPS.
  [[nodiscard]] bool begin(ByteOrder byte_order) noexcept;

  [[nodiscard]] bool write_bool(bool value) noexcept;
  [[nodiscard]] bool write_uint8(std::uint8_t value) noexcept;
  [[nodiscard]] bool write_int16(std::int16_t value) noexcept;
  [[nodiscard]] bool write_uint16(std::uint16_t value) noexcept;
  [[nodiscard]] bool write_int32(std::int32_t value) noexcept;
  [[nodiscard]] bool write_uint32(std::uint32_t value) noexcept;
  [[nodiscard]] bool write_int64(std::int64_t value) noexcept;
  [[nodiscard]] bool write_uint64(std::uint64_t value) noexcept;
  [[nodiscard]] bool write_float32(float value) noexcept;
  [[nodiscard]] bool write_float64(double value) noexcept;
  [[nodiscard]] bool write_bytes(const std::uint8_t* data,
                                 std::size_t size) noexcept;

  // length excludes the required terminating NUL. max_characters is the IDL
  // string bound and also excludes the terminator.
  [[nodiscard]] bool write_string(const char* value, std::size_t length,
                                  std::size_t max_characters) noexcept;

  [[nodiscard]] const std::uint8_t* data() const noexcept { return buffer_; }
  [[nodiscard]] std::size_t size() const noexcept { return offset_; }
  [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
  [[nodiscard]] CdrError error() const noexcept { return error_; }
  [[nodiscard]] bool good() const noexcept { return error_ == CdrError::none; }
  [[nodiscard]] ByteOrder byte_order() const noexcept { return byte_order_; }

 private:
  [[nodiscard]] bool write_integral(std::uint64_t value, std::size_t width,
                                    std::size_t alignment) noexcept;
  [[nodiscard]] bool reserve(std::size_t alignment, std::size_t width,
                             std::size_t& start) noexcept;
  void store_integral(std::size_t start, std::uint64_t value,
                      std::size_t width) noexcept;
  [[nodiscard]] bool fail(CdrError error) noexcept;

  std::uint8_t* buffer_{nullptr};
  std::size_t capacity_{0U};
  std::size_t offset_{0U};
  std::size_t origin_{0U};
  ByteOrder byte_order_{ByteOrder::little_endian};
  CdrError error_{CdrError::none};
  bool initialized_{false};
};

// Bounded reader matching CdrWriter. A failed read does not advance the
// cursor, and the error remains sticky until begin() is called again.
class CdrReader final {
 public:
  CdrReader(const std::uint8_t* buffer, std::size_t size) noexcept;

  [[nodiscard]] bool begin() noexcept;

  [[nodiscard]] bool read_bool(bool& value) noexcept;
  [[nodiscard]] bool read_uint8(std::uint8_t& value) noexcept;
  [[nodiscard]] bool read_int16(std::int16_t& value) noexcept;
  [[nodiscard]] bool read_uint16(std::uint16_t& value) noexcept;
  [[nodiscard]] bool read_int32(std::int32_t& value) noexcept;
  [[nodiscard]] bool read_uint32(std::uint32_t& value) noexcept;
  [[nodiscard]] bool read_int64(std::int64_t& value) noexcept;
  [[nodiscard]] bool read_uint64(std::uint64_t& value) noexcept;
  [[nodiscard]] bool read_float32(float& value) noexcept;
  [[nodiscard]] bool read_float64(double& value) noexcept;
  [[nodiscard]] bool read_bytes(std::uint8_t* destination,
                                std::size_t size) noexcept;

  // Writes a NUL-terminated value into destination and returns its character
  // count in length. destination_capacity includes space for the terminator.
  [[nodiscard]] bool read_string(char* destination,
                                 std::size_t destination_capacity,
                                 std::size_t max_characters,
                                 std::size_t& length) noexcept;

  [[nodiscard]] std::size_t position() const noexcept { return offset_; }
  [[nodiscard]] std::size_t remaining() const noexcept {
    return offset_ <= size_ ? size_ - offset_ : 0U;
  }
  [[nodiscard]] CdrError error() const noexcept { return error_; }
  [[nodiscard]] bool good() const noexcept { return error_ == CdrError::none; }
  [[nodiscard]] ByteOrder byte_order() const noexcept { return byte_order_; }

 private:
  [[nodiscard]] bool read_integral(std::uint64_t& value, std::size_t width,
                                   std::size_t alignment) noexcept;
  [[nodiscard]] bool locate(std::size_t alignment, std::size_t width,
                            std::size_t& start,
                            std::size_t& end) noexcept;
  [[nodiscard]] std::uint64_t load_integral(std::size_t start,
                                            std::size_t width) const noexcept;
  [[nodiscard]] bool fail(CdrError error) noexcept;

  const std::uint8_t* buffer_{nullptr};
  std::size_t size_{0U};
  std::size_t offset_{0U};
  std::size_t origin_{0U};
  ByteOrder byte_order_{ByteOrder::little_endian};
  CdrError error_{CdrError::none};
  bool initialized_{false};
};

}  // namespace openrtdds::serialization
