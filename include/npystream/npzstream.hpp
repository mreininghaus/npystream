// Copyright (C) 2026 Maximilian Reininghaus
// Released under European Union Public License 1.2,
// see LICENSE file
// SPDX-License-Identifier: EUPL-1.2

#pragma once

#include <algorithm>
#include <bit>
#include <cassert>
#include <climits>
#include <complex>
#include <concepts>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <errno.h>
#include <filesystem>
#include <format>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include <zconf.h>
#include <zlib.h>

#include <npystream/map_type.hpp>
#include <npystream/npystream.hpp>
#include <npystream/tuple_util.hpp>

namespace npystream {
enum class CompressionLevel : int {
  C0 = 0,
  C1 = 1,
  C2 = 2,
  C3 = 3,
  C4 = 4,
  C5 = 5,
  C6 = 6,
  C7 = 7,
  C8 = 8,
  C9 = 9,

  Store = C0,
  Low = C1,
  Default = Z_DEFAULT_COMPRESSION,
  Max = C9
};

namespace detail {
class CommonNpzStream {
public:
  CommonNpzStream(std::filesystem::path const& path, std::string_view entryName);

protected:
  template <std::unsigned_integral V>
  void writeLE(V val);

  void flushZlibOutput();

  void appendToZip(std::span<Bytef const> data);

  void finishAndPatchHeader(std::span<uint8_t const> finalizedHeader);

  void initZipFile(size_t placeholder_size, CompressionLevel compr_level);

  std::string entryName{};

private:
  FILE* file{};
  z_stream strm{};
  std::vector<uint8_t> outBuffer = std::vector<uint8_t>(16 * 1024);

  uLong crcPayload = crc32(0L, Z_NULL, 0);
  size_t header_size = 0;

  // Physical disk offsets for later patching
  std::streampos lfhOffset;
  std::streampos lfhZip64SizesOffset;
  std::streampos rawHeaderDiskOffset;
  std::streampos lfhCrcOffset;
};
} // namespace detail

/**
 * This class represents the handle to a .npz file opened for writing.
 * The template parameter indicates the data type to be written. In case of
 * multiple template parameters, "structured" data are written.
 */
template <npy_serializable... TArgs>
class NpzStream : public detail::RealBase<TArgs...>, public detail::CommonNpzStream {
public:
  using Base = detail::RealBase<TArgs...>;
  using Base::operator<<;
  using Base::write;

  //! create a NpzStream (.npy file) at the given path.
  NpzStream(std::filesystem::path const& path, std::string_view entry,
            CompressionLevel level = CompressionLevel::Default)
      : Base{}
      , CommonNpzStream{path, std::format("{}.npy", entry)} {
    auto const header = Base::createPlaceholderHeader();
    initZipFile(header.size(), level);
  }

  //! create a NpzStream for structured data at the given path with labelled data columns
  template <typename Container>
  NpzStream(std::filesystem::path const& path, std::string_view entry, Container const& labels,
            CompressionLevel level = CompressionLevel::Default)
      : Base{labels} {
    this->init(path, entry, level);
  }

  ~NpzStream() noexcept(false) {
    // flush remaining buffered payload data
    Base::flush_buffer();

    auto const values_written = Base::values_written;

    // create final header based on values_written
    std::vector<unsigned char> updated_header = std::invoke([&]() {
      if (Base::labels.size() == 0) {
        return create_npy_header(std::span(&values_written, 1), Base::dtypes[0], Base::sizes[0]);
      } else {
        std::vector<std::string_view> label_views(Base::labels.begin(), Base::labels.end());
        return create_npy_header(std::span(&values_written, 1), label_views, Base::dtypes,
                                 Base::sizes, MemoryOrder::C);
      }
    });

    uint64_t const len_missing_padding = Base::header_end_pos - updated_header.size();
    updated_header.insert(std::prev(updated_header.end()),
                          std::max(static_cast<uint64_t>(0), len_missing_padding), ' ');

    auto const header_span = std::span{updated_header};
    auto const numpy = header_span.subspan(1, 5);
    auto const version = std::span{numpy.end(), 2};
    auto const len = std::span{version.end(), 2};

    uint8_t& len_lower = len.front();
    uint8_t& len_upper = len.back();

    len_upper = static_cast<uint8_t>((updated_header.size() - 10u) / 0x100u);
    len_lower = static_cast<uint8_t>((updated_header.size() - 10u) % 0x100u);

    assert(updated_header.size() == Base::header_end_pos);

    // patch header placeholder
    finishAndPatchHeader(updated_header);
  }

private:
  void appendPayload(std::span<std::byte const> data) override final {
    appendToZip(std::span{reinterpret_cast<Bytef const*>(data.data()), data.size()});
  }
};
} // namespace npystream
