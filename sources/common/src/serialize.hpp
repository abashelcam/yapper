#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace yapper
{
class ByteWriter
{
public:
  void WriteU8(std::uint8_t value);
  void WriteU16(std::uint16_t value);
  void WriteU32(std::uint32_t value);
  void WriteU64(std::uint64_t value);

  void WriteString(std::string_view value);

  void WriteBytes(std::span<const std::uint8_t> bytes);

  [[nodiscard]] const std::vector<std::uint8_t> &Data() const
  {
    return buffer_;
  }
  [[nodiscard]] std::vector<std::uint8_t> Take()
  {
    return std::move(buffer_);
  }

private:
  std::vector<std::uint8_t> buffer_;
};

class ByteReader
{
public:
  explicit ByteReader(std::span<const std::uint8_t> data) : data_(data)
  {
  }

  std::uint8_t  ReadU8();
  std::uint16_t ReadU16();
  std::uint32_t ReadU32();
  std::uint64_t ReadU64();

  std::string ReadString(std::size_t maxBytes);

  void ReadBytes(std::span<std::uint8_t> out);

  std::vector<std::uint8_t> ReadRest();

  [[nodiscard]] bool Ok() const
  {
    return ok_;
  }

  [[nodiscard]] bool AtEnd() const
  {
    return pos_ == data_.size();
  }

  [[nodiscard]] std::size_t Remaining() const
  {
    return data_.size() - pos_;
  }

private:
  bool Require(std::size_t count);

  std::span<const std::uint8_t> data_;
  std::size_t                   pos_ = 0;
  bool                          ok_ = true;
};
} // namespace yapper