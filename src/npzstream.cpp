// Copyright (C) 2026 Maximilian Reininghaus
// Released under European Union Public License 1.2,
// see LICENSE file
// SPDX-License-Identifier: EUPL-1.2

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <zconf.h>
#include <zlib.h>

#include "npystream/npzstream.hpp"

template <std::unsigned_integral V>
void npystream::detail::CommonNpzStream::writeLE(V val) {
  if constexpr (std::endian::native == std::endian::little) {
    std::fwrite(&val, sizeof(val), 1, file);
  } else {
    for (size_t i = 0; i < sizeof(V); ++i) {
      uint8_t const data = static_cast<uint8_t>(val & 0xFF);
      std::fwrite(&data, 1, 1, file);
      val >>= CHAR_BIT;
    }
  }
}

npystream::detail::CommonNpzStream::CommonNpzStream(std::filesystem::path const& path,
                                                    std::string_view entryName)
    : entryName{entryName}
    , file{std::fopen(path.c_str(), "w+b")} {
  if (!file) {
    throw std::runtime_error{strerror(errno)};
  }

  if (entryName.length() > std::numeric_limits<uint16_t>::max()) {
    throw std::runtime_error{"entryName too long"};
  }
}

void npystream::detail::CommonNpzStream::flushZlibOutput() {
  if (outBuffer.size() - strm.avail_out > 0) {
    size_t const bytesToDump = outBuffer.size() - strm.avail_out;
    std::fwrite(outBuffer.data(), 1, bytesToDump, file);
    strm.next_out = outBuffer.data();
    strm.avail_out = static_cast<uInt>(outBuffer.size());
  }
}

void npystream::detail::CommonNpzStream::appendToZip(std::span<Bytef const> data) {
  if (data.empty()) {
    return;
  }

  // avoid integer overflow when data.size() is too large for strm.avail_in,
  // in that case, split into chunks
  if constexpr (sizeof(size_t) > sizeof(strm.avail_in)) {
    while (data.size() > std::numeric_limits<uInt>::max()) {
      auto const chunk = data.first(std::numeric_limits<uInt>::max());
      appendToZip(chunk);
      data = data.subspan(chunk.size());
    }
  }

  // Update payload CRC-32
  crcPayload = crc32(crcPayload, data.data(), static_cast<uInt>(data.size()));

  strm.next_in = const_cast<Bytef*>(data.data()); // zlib...
  strm.avail_in = static_cast<uInt>(data.size());

  while (strm.avail_in > 0) {
    deflate(&strm, Z_NO_FLUSH);
    if (strm.avail_out == 0)
      flushZlibOutput();
  }
}

void npystream::detail::CommonNpzStream::finishAndPatchHeader(
    std::span<uint8_t const> finalizedHeader) {
  if (finalizedHeader.size() != header_size) {
    throw std::invalid_argument(
        std::format("finalized header must be exactly {} bytes.", header_size));
  }

  // flush remaining data
  while (deflate(&strm, Z_FINISH) == Z_OK) {
    flushZlibOutput();
  }
  flushZlibOutput();

  auto const totalUncompressedSize = strm.total_in;
  auto const totalCompressedSize = strm.total_out;

  deflateEnd(&strm);

  uLong const crcHeader = crc32(0L, finalizedHeader.data(), finalizedHeader.size());

  static_assert(sizeof(z_off_t) >= sizeof(size_t), "integer overflows expected on this platform");
  uLong const totalCrc = crc32_combine(crcHeader, crcPayload, totalUncompressedSize - header_size);

  // ZIP Central Directory at end of file
  std::streampos const cdOffset = std::ftell(file);
  writeLE<uint32_t>(0x02014b50u); // Central Directory Header signature
  writeLE<uint16_t>(45u);         // version made by
  writeLE<uint16_t>(45u);         // version required to extract
  writeLE<uint16_t>(0x0000u);
  writeLE<uint16_t>(8u); // DEFLATE
  writeLE<uint32_t>(0u);
  writeLE<uint32_t>(static_cast<uint32_t>(totalCrc));
  writeLE<uint32_t>(0xffffffffu);
  writeLE<uint32_t>(0xffffffffu);
  writeLE<uint16_t>(static_cast<uint16_t>(entryName.length()));
  writeLE<uint16_t>(28);          // extra field length
  writeLE<uint16_t>(0);           // comment length
  writeLE<uint16_t>(0);           // disk start
  writeLE<uint16_t>(0);           // int file attributes
  writeLE<uint32_t>(0);           // ext file attributes
  writeLE<uint32_t>(0xffffffffu); // LFH Disk Offset overflow marker
  std::fwrite(entryName.c_str(), 1, entryName.length(), file);

  // CD ZIP64 Extra Field (Tag 0x0001)
  writeLE<uint16_t>(0x0001u); // Tag: ZIP64
  writeLE<uint16_t>(24u);     // size

  writeLE<uint64_t>(totalUncompressedSize);
  writeLE<uint64_t>(totalCompressedSize);
  writeLE<uint64_t>(static_cast<uint64_t>(lfhOffset)); // True 64-bit LFH offset!

  std::streampos const cdEndOffset = std::ftell(file);
  uint32_t cdSize = static_cast<uint32_t>(cdEndOffset - cdOffset);

  std::streampos zip64EocdOffset = std::ftell(file);
  writeLE<uint32_t>(0x06064b50u);                     // ZIP64 EOCD
  writeLE<uint64_t>(44u);                             // size of this record remaining
  writeLE<uint16_t>(45u);                             // version made by
  writeLE<uint16_t>(45u);                             // version needed
  writeLE<uint32_t>(0u);                              // disk number
  writeLE<uint32_t>(0u);                              // disk of CD start
  writeLE<uint64_t>(1u);                              // CD entries on this disk
  writeLE<uint64_t>(1u);                              // total number of CD entries
  writeLE<uint64_t>(cdSize);                          // CD size
  writeLE<uint64_t>(static_cast<uint64_t>(cdOffset)); // CD offset

  // ZIP64 End of Central Directory Locator (20 bytes)
  writeLE<uint32_t>(0x07064b50u);                            // ZIP64 EOCD Locator
  writeLE<uint32_t>(0u);                                     // disk number
  writeLE<uint64_t>(static_cast<uint64_t>(zip64EocdOffset)); // ZIP64 EOCD offset
  writeLE<uint32_t>(1u);                                     // total number of disks

  // Legacy Standard EOCD Record (22 bytes)
  writeLE<uint32_t>(0x06054b50u); // Standard EOCD
  writeLE<uint16_t>(0u);
  writeLE<uint16_t>(0u);
  writeLE<uint16_t>(0xffffu);     // marker for ZIP64
  writeLE<uint16_t>(0xffffu);     // marker for ZIP64
  writeLE<uint32_t>(0xffffffffu); // marker for ZIP64
  writeLE<uint32_t>(0xffffffffu); // marker for ZIP64
  writeLE<uint16_t>(0u);          // comment length

  // replace placeholder with final header
  std::fseek(file, rawHeaderDiskOffset, SEEK_SET);
  std::fwrite(finalizedHeader.data(), 1, finalizedHeader.size(), file);

  // patch Local File Header
  std::fseek(file, lfhCrcOffset, SEEK_SET);
  writeLE<uint32_t>(static_cast<uint32_t>(totalCrc));

  std::fseek(file, lfhZip64SizesOffset, SEEK_SET);
  writeLE<uint64_t>(static_cast<uint64_t>(totalUncompressedSize));
  writeLE<uint64_t>(static_cast<uint64_t>(totalCompressedSize));

  std::fclose(file);
}

void npystream::detail::CommonNpzStream::initZipFile(size_t placeholder_size,
                                                     CompressionLevel compr_level) {
  std::vector<Bytef> placeholder(placeholder_size);
  header_size = placeholder_size;
  if (header_size > std::numeric_limits<uInt>::max()) {
    throw std::runtime_error{"header size too large"};
  }

  // ZIP Local File Header with dummy CRC and sizes
  lfhOffset = std::ftell(file);
  writeLE<uint32_t>(0x04034b50u); // LFH Signature
  writeLE<uint16_t>(45u);         // Version
  writeLE<uint16_t>(0x0000u);     // General purpose bit flag
  writeLE<uint16_t>(8u);          // compression method DEFLATE
  writeLE<uint16_t>(0u);          // File time (dummy)
  writeLE<uint16_t>(0u);          // File date (dummy)

  lfhCrcOffset = std::ftell(file);
  writeLE<uint32_t>(0u);          // CRC-32 (Placeholder, patched later)
  writeLE<uint32_t>(0xffffffffu); // ZIP64 compressed size
  writeLE<uint32_t>(0xffffffffu); // ZIP64 uncomprressed size

  writeLE<uint16_t>(static_cast<uint16_t>(entryName.length()));
  writeLE<uint16_t>(20u); // Extra field length
  std::fwrite(entryName.c_str(), 1, entryName.length(), file);

  // ZIP64 fields
  writeLE<uint16_t>(0x0001);
  writeLE<uint16_t>(16);

  lfhZip64SizesOffset = std::ftell(file);
  writeLE<uint64_t>(0u); // 64-bit Uncompressed Size (Placeholder)
  writeLE<uint64_t>(0u); // 64-bit Compressed Size (Placeholder)

  // 2. Initialize zlib for raw DEFLATE (-MAX_WBITS suppresses zlib wrapper headers)
  std::memset(&strm, 0, sizeof(strm));
  if (auto const ret =
          deflateInit2(&strm, Z_NO_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_HUFFMAN_ONLY);
      ret != Z_OK) {
    throw std::runtime_error{zError(ret)};
  }
  strm.next_out = outBuffer.data();
  strm.avail_out = static_cast<uInt>(outBuffer.size());

  strm.next_in = placeholder.data();
  strm.avail_in = placeholder.size();

  // This forces zlib to emit the 5-byte DEFLATE uncompressed block header + header placeholder
  deflate(&strm, Z_FULL_FLUSH);
  flushZlibOutput();

  // Calculate the physical disk offset where our 256 raw bytes landed!
  // LFH size + 5 bytes for the DEFLATE uncompressed block header (0x00, LEN, NLEN)
  rawHeaderDiskOffset = lfhOffset + static_cast<std::streampos>(30 + entryName.length() + 20 + 5);

  // enable compression for payload
  if (auto const ret = deflateParams(&strm, static_cast<int>(compr_level), Z_DEFAULT_STRATEGY);
      ret != Z_OK) {
    throw std::runtime_error{zError(ret)};
  }

  flushZlibOutput();
}
