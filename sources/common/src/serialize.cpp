#include "serialize.hpp"

#include <algorithm>

namespace yapper
{
void ByteWriter::WriteU8(std::uint8_t value)
{
  buffer_.push_back(value);
}

void ByteWriter::WriteU16(std::uint16_t value)
{
  for (int i = 0; i < 2; ++i)
    buffer_.push_back(static_cast<std::uint8_t>(value >> (i * 8)));
}

void ByteWriter::WriteU32(std::uint32_t value)
{
  for (int i = 0; i < 4; ++i)
    buffer_.push_back(static_cast<std::uint8_t>(value >> (i * 8)));
}

void ByteWriter::WriteU64(std::uint64_t value)
{
  for (int i = 0; i < 8; ++i)
    buffer_.push_back(static_cast<std::uint8_t>(value >> (i * 8)));
}

void ByteWriter::WriteString(std::string_view value)
{
  const auto length = static_cast<std::uint16_t>(value.size() > 0xFFFF ? 0xFFFF : value.size());

  WriteU16(length);
  buffer_.insert(buffer_.end(), value.begin(), value.begin() + length);
}

void ByteWriter::WriteBytes(std::span<const std::uint8_t> bytes)
{
  buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());
}

bool ByteReader::Require(std::size_t count)
{
  if (!ok_ || Remaining() < count)
  {
    ok_ = false;
    return false;
  }
  return true;
}

std::uint8_t ByteReader::ReadU8()
{
  if (!Require(1))
    return 0;

  return data_[pos_++];
}

std::uint16_t ByteReader::ReadU16()
{
  if (!Require(2))
    return 0;

  std::uint16_t value = 0;
  for (int i = 0; i < 2; ++i)
    value |= static_cast<std::uint16_t>(data_[pos_++]) << (i * 8);
  return value;
}

std::uint32_t ByteReader::ReadU32()
{
  if (!Require(4))
    return 0;

  std::uint32_t value = 0;
  for (int i = 0; i < 4; ++i)
    value |= static_cast<std::uint32_t>(data_[pos_++]) << (i * 8);
  return value;
}

std::uint64_t ByteReader::ReadU64()
{
  if (!Require(8))
    return 0;

  std::uint64_t value = 0;
  for (int i = 0; i < 8; ++i)
    value |= static_cast<std::uint64_t>(data_[pos_++]) << (i * 8);
  return value;
}

std::string ByteReader::ReadString(std::size_t maxBytes)
{
  const std::uint16_t length = ReadU16();

  if (length > maxBytes)
  {
    ok_ = false;
    return {};
  }

  if (!Require(length))
    return {};

  std::string value(reinterpret_cast<const char *>(data_.data() + pos_), length);
  pos_ += length;
  return value;
}

void ByteReader::ReadBytes(std::span<std::uint8_t> out)
{
  if (!Require(out.size()))
    return;

  std::copy_n(data_.begin() + pos_, out.size(), out.begin());
  pos_ += out.size();
}

std::vector<std::uint8_t> ByteReader::ReadRest()
{
  if (!ok_)
    return {};

  std::vector<std::uint8_t> rest(data_.begin() + pos_, data_.end());
  pos_ = data_.size();
  return rest;
}
} // namespace yapper