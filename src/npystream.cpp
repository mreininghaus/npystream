// Copyright (C) 2024 Maximilian Reininghaus
// Released under European Union Public License 1.2,
// see LICENSE file
// SPDX-License-Identifier: EUPL-1.2

#include <bit>
#include <concepts>
#include <cstring>
#include <npystream/npystream.hpp>
#include <span>
#include <string_view>
#include <vector>

static_assert(std::endian::native == std::endian::big || std::endian::native == std::endian::little,
              "mixed-endianness not supported");

static std::vector<unsigned char>& append(std::vector<unsigned char>& vec, std::string_view view) {
  vec.insert(vec.end(), view.begin(), view.end());
  return vec;
}

static unsigned char constexpr native_endian_symbol =
    (std::endian::native == std::endian::little) ? '<' : '>';

template <std::integral T>
std::vector<unsigned char>& append(std::vector<unsigned char>& lhs, const T rhs) {
  std::array<unsigned char, sizeof(T)> buffer;
  memcpy(buffer.data(), std::addressof(rhs), sizeof(T));

  if constexpr (std::endian::native == std::endian::big) {
    lhs.insert(lhs.end(), buffer.crbegin(), buffer.crend());
  } else {
    lhs.insert(lhs.end(), buffer.cbegin(), buffer.cend());
  }
  return lhs;
}

std::vector<unsigned char> npystream::create_npy_header(std::span<uint64_t const> const shape,
                                                        std::span<std::string_view const> labels,
                                                        std::span<char const> dtypes,
                                                        std::span<size_t const> sizes,
                                                        MemoryOrder memory_order) {
  std::vector<unsigned char> dict;
  append(dict, "{'descr': [");

  if (labels.size() != dtypes.size() || dtypes.size() != sizes.size() ||
      sizes.size() != labels.size()) {
    throw std::runtime_error("create_npy_header: sizes of argument vectors not equal");
  }

  for (size_t i = 0; i < dtypes.size(); ++i) {
    auto const& label = labels[i];
    auto const& dtype = dtypes[i];
    auto const& size = sizes[i];

    append(dict, "('");
    append(dict, label);
    append(dict, "', '");
    dict.push_back(native_endian_symbol);
    dict.push_back(dtype);
    append(dict, std::to_string(size));
    append(dict, "')");

    if (i + 1 != dtypes.size()) {
      append(dict, ", ");
    }
  }

  if (dtypes.size() == 1) {
    dict.push_back(',');
  }

  append(dict, "], 'fortran_order': ");
  append(dict, (memory_order == MemoryOrder::C) ? "False" : "True");
  append(dict, ", 'shape': (");
  append(dict, std::to_string(shape[0]));
  for (size_t i = 1; i < shape.size(); i++) {
    append(dict, ", ");
    append(dict, std::to_string(shape[i]));
  }
  if (shape.size() == 1) {
    append(dict, ",");
  }
  append(dict, "), }");

  // pad with spaces so that preamble+dict is modulo 16 bytes. preamble is 10
  // bytes. dict needs to end with \n
  int const remainder = 16 - (10 + dict.size()) % 16;
  dict.insert(dict.end(), remainder, ' ');
  dict.back() = '\n';

  std::vector<unsigned char> header;
  header.push_back((unsigned char)0x93);
  append(header, "NUMPY");
  header.push_back((unsigned char)0x01); // major version of numpy format
  header.push_back((unsigned char)0x00); // minor version of numpy format
  append(header, (uint16_t)dict.size());
  header.insert(header.end(), dict.begin(), dict.end());

  return header;
}

std::vector<unsigned char> npystream::create_npy_header(std::span<uint64_t const> const shape,
                                                        char dtype, unsigned wordsize,
                                                        MemoryOrder memory_order) {
  std::vector<unsigned char> dict;
  append(dict, "{'descr': '");
  dict.push_back(native_endian_symbol);
  dict.push_back(dtype);
  append(dict, std::to_string(wordsize));
  append(dict, "', 'fortran_order': ");
  append(dict, (memory_order == MemoryOrder::C) ? "False" : "True");
  append(dict, ", 'shape': (");
  append(dict, std::to_string(shape[0]));
  for (size_t i = 1; i < shape.size(); i++) {
    append(dict, ", ");
    append(dict, std::to_string(shape[i]));
  }
  if (shape.size() == 1) {
    append(dict, ",");
  }
  append(dict, "), }");

  // pad with spaces so that preamble+dict is modulo 16 bytes. preamble is 10
  // bytes. dict needs to end with \n
  int const remainder = 16 - (10 + dict.size()) % 16;
  dict.insert(dict.end(), remainder, ' ');
  dict.back() = '\n';

  std::vector<unsigned char> header;
  header.push_back((unsigned char)0x93);
  append(header, "NUMPY");
  header.push_back((unsigned char)0x01); // major version of numpy format
  header.push_back((unsigned char)0x00); // minor version of numpy format
  append(header, (uint16_t)dict.size());
  header.insert(header.end(), dict.begin(), dict.end());

  return header;
}
