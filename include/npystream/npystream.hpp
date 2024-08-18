// Copyright (C) 2024 Maximilian Reininghaus
// Released under European Union Public License 1.2,
// see 'EUPL-1.2 EN.txt' file

#pragma once

#include <algorithm>
#include <cassert>
#include <complex>
#include <concepts>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include <npystream/map_type.hpp>
#include <npystream/tuple_util.hpp>

namespace npystream {
enum class MemoryOrder { Fortran, C, ColumnMajor = Fortran, RowMajor = C };

std::vector<char> create_npy_header(std::span<uint64_t const> shape, char dtype, unsigned size,
                                    MemoryOrder = MemoryOrder::C);

std::vector<char> create_npy_header(std::span<uint64_t const> shape,
                                    std::span<std::string_view const> labels,
                                    std::span<char const> dtypes, std::span<size_t const> sizes,
                                    MemoryOrder memory_order);

namespace detail {
template <typename T>
struct is_complex : std::false_type {};

template <std::floating_point T>
struct is_complex<std::complex<T>> : std::true_type {};
} // namespace detail

template <typename... TArgs>
concept npy_serializable =
    ((std::is_arithmetic_v<TArgs> || detail::is_complex<TArgs>::value) && ...);

/**
 * This class represents the handle to a .npy file opened for writing.
 * The template parameter indicates the data type to be written. In case of
 * multiple template parameters, "structured" data are written.
 */
template <npy_serializable T, npy_serializable... TArgs>
class NpyStream {

  using tuple_type = std::tuple<T, TArgs...>;

  static auto constexpr& dtypes = tuple_info<tuple_type>::data_types;
  static auto constexpr& sizes = tuple_info<tuple_type>::element_sizes;

public:
  //! create a NpyStream (.npy file) at the given path.
  NpyStream(std::filesystem::path const& path) {
    if (std::size_t const size = std::tuple_size_v<tuple_type>; size > 1) {
      labels.reserve(size);
      for (std::size_t i = 0; i < size; ++i) {
        labels.emplace_back(std::format("f{}", i));
      }
    }
    init(path);
  }

  //! create a NpyStream for structured data at the given path with labelled
  //! data columns
  template <typename Container>
  NpyStream(std::filesystem::path const& path, Container const& labels_)
      : labels{std::cbegin(labels_), std::cend(labels_)} {
    init(path);
  }

  ~NpyStream() {
    std::vector<char> updated_header;
    if (labels.size() == 0) {
      updated_header =
          create_npy_header(std::span<uint64_t>(&values_written, 1), map_type(T{}), sizeof(T));
    } else {
      std::vector<std::string_view> label_views(labels.cbegin(), labels.cend());
      updated_header = create_npy_header(std::span<uint64_t const>(&values_written, 1), label_views,
                                         dtypes, sizes, MemoryOrder::C);
    }

    uint64_t const len_missing_padding = header_end_pos - updated_header.size();
    updated_header.insert(std::prev(updated_header.end()),
                          std::max(static_cast<uint64_t>(0), len_missing_padding), ' ');
    uint8_t& len_upper = *reinterpret_cast<uint8_t*>(&updated_header[7]);
    uint8_t& len_lower = *reinterpret_cast<uint8_t*>(&updated_header[8]);
    len_upper = (updated_header.size() - 10) / 0x100;
    len_lower = (updated_header.size() - 10) % 0x100;
    assert(updated_header.size() == header_end_pos);
    file.seekp(0);
    file.write(updated_header.data(), updated_header.size());
  }

  //! write single scalar value into stream
  template <std::same_as<T> U = T>
    requires(sizeof...(TArgs) == 0)
  NpyStream& operator<<(U val) {
    file.write(reinterpret_cast<char const*>(&val), sizeof(val));
    ++values_written;
    return *this;
  }

  //! write single data tuple point into stream
  template <tuple_like Tup>
    requires(convertible<Tup, tuple_type>)
  NpyStream& operator<<(Tup const& val) {
    using tup_info = tuple_info<Tup>;
    std::array<char, tup_info::sum_sizes> buffer{};
    fill(val, buffer.data());
    file.write(buffer.data(), buffer.size());
    ++values_written;
    return *this;
  }

  //! write contiguous block of scalar data, given as std::span, into stream
  template <std::same_as<T> U = T>
    requires(sizeof...(TArgs) == 0)
  NpyStream& write(std::span<U const> data) {
    file.write(reinterpret_cast<char const*>(data.data()), sizeof(T) * data.size());
    values_written += data.size();
    return *this;
  }

  //! write contiguous block of scalar data, given as iterator pair, into stream
  template <std::contiguous_iterator TConstIter>
    requires(std::same_as<std::iter_value_t<TConstIter>, T> && std::tuple_size_v<tuple_type> == 1)
  NpyStream& write(TConstIter begin, TConstIter end) {
    return write(std::span<std::add_const_t<std::iter_value_t<TConstIter>>>{begin, end});
  }

  /*
   * Write sequence of data, given as iterator pair, into stream. In case of
   * structured data, the iterator has to return std::tuples whose individual
   * member types match the data types of the file.
   */
  template <typename TConstIter>
    requires(!std::contiguous_iterator<TConstIter> ||
             (std::tuple_size_v<tuple_type> > 1 &&
              convertible<std::iter_value_t<TConstIter>, tuple_type>))
  NpyStream& write(TConstIter begin, TConstIter end) {
    for (; begin != end; ++begin) {
      *this << *begin;
    }

    return *this;
  }

private:
  void init(std::filesystem::path const& path) {
    uint64_t const max_elements = std::numeric_limits<uint64_t>::max();
    std::vector<char> header;

    size_t constexpr tuple_size = std::tuple_size_v<tuple_type>;

    if (labels.size() == 0) {
      if (tuple_size == 1) {
        header = create_npy_header(std::span<uint64_t const>(&max_elements, 1), map_type(T{}),
                                   sizeof(T));
      } else {
        throw std::runtime_error{
            "labels size does not match number of elements in structured type"};
      }
    } else {
      if (labels.size() != tuple_size) {
        throw std::runtime_error{
            "labels size does not match number of elements in structured type"};
      }

      std::vector<std::string_view> label_views(labels.cbegin(), labels.cend());
      header = create_npy_header(std::span<uint64_t const>(&max_elements, 1), label_views, dtypes,
                                 sizes, MemoryOrder::C);
    }

    file.open(path);
    file.write(header.data(), header.size());
    header_end_pos = header.size();
  }

  template <tuple_like U, int k = 0>
  static void fill(U const& tup, char* buffer) {
    auto constexpr offsets = tuple_info<U>::offsets;

    if constexpr (k < tuple_info<U>::size) {
      auto const& elem = std::get<k>(tup);
      auto constexpr elem_size = sizeof(elem);
      static_assert(tuple_info<U>::element_sizes[k] == elem_size); // sanity check

      std::array<char, elem_size> tmp{};
      memcpy(tmp.data(), std::addressof(elem), elem_size);
      std::copy(tmp.cbegin(), tmp.cend(), buffer + offsets[k]);
      fill<U, k + 1>(tup, buffer);
    }
  }

  std::ofstream file;
  size_t header_end_pos;
  uint64_t values_written{};
  std::vector<std::string> labels{};
};
} // namespace npystream
