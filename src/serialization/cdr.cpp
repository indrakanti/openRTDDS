#include "openrtdds/serialization/cdr.hpp"

#include <cstring>
#include <limits>

namespace openrtdds::serialization {
namespace {

constexpr std::size_t encapsulation_size = 4U;

[[nodiscard]] constexpr bool is_power_of_two(
    const std::size_t value) noexcept {
  return (value != 0U) && ((value & (value - 1U)) == 0U);
}

[[nodiscard]] bool aligned_position(const std::size_t offset,
                                    const std::size_t origin,
                                    const std::size_t alignment,
                                    std::size_t& result) noexcept {
  if ((offset < origin) || !is_power_of_two(alignment)) {
    return false;
  }
  const std::size_t relative = offset - origin;
  const std::size_t remainder = relative & (alignment - 1U);
  const std::size_t padding =
      remainder == 0U ? 0U : alignment - remainder;
  if (offset > (std::numeric_limits<std::size_t>::max() - padding)) {
    return false;
  }
  result = offset + padding;
  return true;
}

}  // namespace

const char* to_string(const CdrError error) noexcept {
  switch (error) {
    case CdrError::none:
      return "none";
    case CdrError::invalid_argument:
      return "invalid argument";
    case CdrError::not_initialized:
      return "CDR encapsulation not initialized";
    case CdrError::overflow:
      return "output buffer overflow";
    case CdrError::underflow:
      return "input buffer underflow";
    case CdrError::unsupported_encoding:
      return "unsupported CDR encoding";
    case CdrError::invalid_data:
      return "invalid serialized data";
    case CdrError::bound_exceeded:
      return "declared bound exceeded";
  }
  return "unknown CDR error";
}

CdrWriter::CdrWriter(std::uint8_t* const buffer,
                     const std::size_t capacity) noexcept
    : buffer_(buffer), capacity_(capacity) {
  if ((buffer_ == nullptr) && (capacity_ != 0U)) {
    error_ = CdrError::invalid_argument;
  }
}

bool CdrWriter::begin(const ByteOrder byte_order) noexcept {
  offset_ = 0U;
  origin_ = 0U;
  byte_order_ = byte_order;
  initialized_ = false;
  error_ = CdrError::none;

  if ((buffer_ == nullptr) && (capacity_ != 0U)) {
    return fail(CdrError::invalid_argument);
  }
  if ((byte_order != ByteOrder::big_endian) &&
      (byte_order != ByteOrder::little_endian)) {
    return fail(CdrError::invalid_argument);
  }
  if (capacity_ < encapsulation_size) {
    return fail(CdrError::overflow);
  }

  // XCDR1 PLAIN_CDR: 0x0000 big endian, 0x0001 little endian.
  buffer_[0] = 0U;
  buffer_[1] = byte_order == ByteOrder::little_endian ? 1U : 0U;
  buffer_[2] = 0U;
  buffer_[3] = 0U;
  offset_ = encapsulation_size;
  origin_ = offset_;
  initialized_ = true;
  return true;
}

bool CdrWriter::reserve(const std::size_t alignment, const std::size_t width,
                        std::size_t& start) noexcept {
  if (error_ != CdrError::none) {
    return false;
  }
  if (!initialized_) {
    return fail(CdrError::not_initialized);
  }
  if (!aligned_position(offset_, origin_, alignment, start)) {
    return fail(CdrError::overflow);
  }
  if ((start > capacity_) || (width > (capacity_ - start))) {
    return fail(CdrError::overflow);
  }
  for (std::size_t index = offset_; index < start; ++index) {
    buffer_[index] = 0U;
  }
  return true;
}

void CdrWriter::store_integral(const std::size_t start,
                               const std::uint64_t value,
                               const std::size_t width) noexcept {
  for (std::size_t index = 0U; index < width; ++index) {
    const std::size_t shift_index =
        byte_order_ == ByteOrder::little_endian ? index : width - 1U - index;
    buffer_[start + index] =
        static_cast<std::uint8_t>((value >> (shift_index * 8U)) & 0xFFU);
  }
}

bool CdrWriter::write_integral(const std::uint64_t value,
                               const std::size_t width,
                               const std::size_t alignment) noexcept {
  std::size_t start = 0U;
  if (!reserve(alignment, width, start)) {
    return false;
  }
  store_integral(start, value, width);
  offset_ = start + width;
  return true;
}

bool CdrWriter::write_bool(const bool value) noexcept {
  return write_uint8(value ? 1U : 0U);
}

bool CdrWriter::write_uint8(const std::uint8_t value) noexcept {
  return write_integral(value, 1U, 1U);
}

bool CdrWriter::write_int16(const std::int16_t value) noexcept {
  return write_uint16(static_cast<std::uint16_t>(value));
}

bool CdrWriter::write_uint16(const std::uint16_t value) noexcept {
  return write_integral(value, 2U, 2U);
}

bool CdrWriter::write_int32(const std::int32_t value) noexcept {
  return write_uint32(static_cast<std::uint32_t>(value));
}

bool CdrWriter::write_uint32(const std::uint32_t value) noexcept {
  return write_integral(value, 4U, 4U);
}

bool CdrWriter::write_int64(const std::int64_t value) noexcept {
  return write_uint64(static_cast<std::uint64_t>(value));
}

bool CdrWriter::write_uint64(const std::uint64_t value) noexcept {
  return write_integral(value, 8U, 8U);
}

bool CdrWriter::write_float32(const float value) noexcept {
  static_assert(std::numeric_limits<float>::is_iec559 &&
                    (sizeof(float) == sizeof(std::uint32_t)),
                "OpenRTDDS requires an IEEE-754 binary32 float");
  std::uint32_t bits = 0U;
  std::memcpy(&bits, &value, sizeof(bits));
  return write_uint32(bits);
}

bool CdrWriter::write_float64(const double value) noexcept {
  static_assert(std::numeric_limits<double>::is_iec559 &&
                    (sizeof(double) == sizeof(std::uint64_t)),
                "OpenRTDDS requires an IEEE-754 binary64 double");
  std::uint64_t bits = 0U;
  std::memcpy(&bits, &value, sizeof(bits));
  return write_uint64(bits);
}

bool CdrWriter::write_bytes(const std::uint8_t* const data,
                            const std::size_t size) noexcept {
  if ((data == nullptr) && (size != 0U)) {
    return fail(CdrError::invalid_argument);
  }
  std::size_t start = 0U;
  if (!reserve(1U, size, start)) {
    return false;
  }
  if (size != 0U) {
    std::memcpy(&buffer_[start], data, size);
  }
  offset_ = start + size;
  return true;
}

bool CdrWriter::write_string(const char* const value,
                             const std::size_t length,
                             const std::size_t max_characters) noexcept {
  if ((value == nullptr) && (length != 0U)) {
    return fail(CdrError::invalid_argument);
  }
  if (length > max_characters) {
    return fail(CdrError::bound_exceeded);
  }
  if (length >= std::numeric_limits<std::uint32_t>::max()) {
    return fail(CdrError::bound_exceeded);
  }

  const std::size_t serialized_length = length + 1U;
  if (serialized_length >
      (std::numeric_limits<std::size_t>::max() - 4U)) {
    return fail(CdrError::bound_exceeded);
  }
  std::size_t start = 0U;
  if (!reserve(4U, 4U + serialized_length, start)) {
    return false;
  }
  store_integral(start, static_cast<std::uint32_t>(serialized_length), 4U);
  if (length != 0U) {
    std::memcpy(&buffer_[start + 4U], value, length);
  }
  buffer_[start + 4U + length] = 0U;
  offset_ = start + 4U + serialized_length;
  return true;
}

bool CdrWriter::fail(const CdrError error) noexcept {
  if (error_ == CdrError::none) {
    error_ = error;
  }
  return false;
}

CdrReader::CdrReader(const std::uint8_t* const buffer,
                     const std::size_t size) noexcept
    : buffer_(buffer), size_(size) {
  if ((buffer_ == nullptr) && (size_ != 0U)) {
    error_ = CdrError::invalid_argument;
  }
}

bool CdrReader::begin() noexcept {
  offset_ = 0U;
  origin_ = 0U;
  initialized_ = false;
  error_ = CdrError::none;

  if ((buffer_ == nullptr) && (size_ != 0U)) {
    return fail(CdrError::invalid_argument);
  }
  if (size_ < encapsulation_size) {
    return fail(CdrError::underflow);
  }
  if ((buffer_[0] != 0U) || ((buffer_[1] != 0U) && (buffer_[1] != 1U))) {
    return fail(CdrError::unsupported_encoding);
  }

  byte_order_ = buffer_[1] == 1U ? ByteOrder::little_endian
                                 : ByteOrder::big_endian;
  offset_ = encapsulation_size;
  origin_ = offset_;
  initialized_ = true;
  return true;
}

bool CdrReader::locate(const std::size_t alignment, const std::size_t width,
                       std::size_t& start, std::size_t& end) noexcept {
  if (error_ != CdrError::none) {
    return false;
  }
  if (!initialized_) {
    return fail(CdrError::not_initialized);
  }
  if (!aligned_position(offset_, origin_, alignment, start)) {
    return fail(CdrError::underflow);
  }
  if ((start > size_) || (width > (size_ - start))) {
    return fail(CdrError::underflow);
  }
  end = start + width;
  return true;
}

std::uint64_t CdrReader::load_integral(const std::size_t start,
                                       const std::size_t width) const noexcept {
  std::uint64_t value = 0U;
  for (std::size_t index = 0U; index < width; ++index) {
    const std::size_t shift_index =
        byte_order_ == ByteOrder::little_endian ? index : width - 1U - index;
    value |= static_cast<std::uint64_t>(buffer_[start + index])
             << (shift_index * 8U);
  }
  return value;
}

bool CdrReader::read_integral(std::uint64_t& value, const std::size_t width,
                              const std::size_t alignment) noexcept {
  std::size_t start = 0U;
  std::size_t end = 0U;
  if (!locate(alignment, width, start, end)) {
    return false;
  }
  value = load_integral(start, width);
  offset_ = end;
  return true;
}

bool CdrReader::read_bool(bool& value) noexcept {
  std::uint8_t encoded = 0U;
  const std::size_t saved_offset = offset_;
  if (!read_uint8(encoded)) {
    return false;
  }
  if (encoded > 1U) {
    offset_ = saved_offset;
    return fail(CdrError::invalid_data);
  }
  value = encoded == 1U;
  return true;
}

bool CdrReader::read_uint8(std::uint8_t& value) noexcept {
  std::uint64_t decoded = 0U;
  if (!read_integral(decoded, 1U, 1U)) {
    return false;
  }
  value = static_cast<std::uint8_t>(decoded);
  return true;
}

bool CdrReader::read_int16(std::int16_t& value) noexcept {
  std::uint16_t bits = 0U;
  if (!read_uint16(bits)) {
    return false;
  }
  std::memcpy(&value, &bits, sizeof(value));
  return true;
}

bool CdrReader::read_uint16(std::uint16_t& value) noexcept {
  std::uint64_t decoded = 0U;
  if (!read_integral(decoded, 2U, 2U)) {
    return false;
  }
  value = static_cast<std::uint16_t>(decoded);
  return true;
}

bool CdrReader::read_int32(std::int32_t& value) noexcept {
  std::uint32_t bits = 0U;
  if (!read_uint32(bits)) {
    return false;
  }
  std::memcpy(&value, &bits, sizeof(value));
  return true;
}

bool CdrReader::read_uint32(std::uint32_t& value) noexcept {
  std::uint64_t decoded = 0U;
  if (!read_integral(decoded, 4U, 4U)) {
    return false;
  }
  value = static_cast<std::uint32_t>(decoded);
  return true;
}

bool CdrReader::read_int64(std::int64_t& value) noexcept {
  std::uint64_t bits = 0U;
  if (!read_uint64(bits)) {
    return false;
  }
  std::memcpy(&value, &bits, sizeof(value));
  return true;
}

bool CdrReader::read_uint64(std::uint64_t& value) noexcept {
  return read_integral(value, 8U, 8U);
}

bool CdrReader::read_float32(float& value) noexcept {
  std::uint32_t bits = 0U;
  if (!read_uint32(bits)) {
    return false;
  }
  std::memcpy(&value, &bits, sizeof(value));
  return true;
}

bool CdrReader::read_float64(double& value) noexcept {
  std::uint64_t bits = 0U;
  if (!read_uint64(bits)) {
    return false;
  }
  std::memcpy(&value, &bits, sizeof(value));
  return true;
}

bool CdrReader::read_bytes(std::uint8_t* const destination,
                           const std::size_t size) noexcept {
  if ((destination == nullptr) && (size != 0U)) {
    return fail(CdrError::invalid_argument);
  }
  std::size_t start = 0U;
  std::size_t end = 0U;
  if (!locate(1U, size, start, end)) {
    return false;
  }
  if (size != 0U) {
    std::memcpy(destination, &buffer_[start], size);
  }
  offset_ = end;
  return true;
}

bool CdrReader::read_string(char* const destination,
                            const std::size_t destination_capacity,
                            const std::size_t max_characters,
                            std::size_t& length) noexcept {
  if ((destination == nullptr) && (destination_capacity != 0U)) {
    return fail(CdrError::invalid_argument);
  }

  std::size_t length_start = 0U;
  std::size_t data_start = 0U;
  if (!locate(4U, 4U, length_start, data_start)) {
    return false;
  }
  const std::uint64_t serialized_length_value =
      load_integral(length_start, 4U);
  const std::size_t serialized_length =
      static_cast<std::size_t>(serialized_length_value);
  if (serialized_length == 0U) {
    return fail(CdrError::invalid_data);
  }
  const std::size_t character_count = serialized_length - 1U;
  if (character_count > max_characters) {
    return fail(CdrError::bound_exceeded);
  }
  if (serialized_length > destination_capacity) {
    return fail(CdrError::bound_exceeded);
  }
  if ((data_start > size_) || (serialized_length > (size_ - data_start))) {
    return fail(CdrError::underflow);
  }
  if (buffer_[data_start + serialized_length - 1U] != 0U) {
    return fail(CdrError::invalid_data);
  }

  std::memcpy(destination, &buffer_[data_start], serialized_length);
  length = character_count;
  offset_ = data_start + serialized_length;
  return true;
}

bool CdrReader::fail(const CdrError error) noexcept {
  if (error_ == CdrError::none) {
    error_ = error;
  }
  return false;
}

}  // namespace openrtdds::serialization
