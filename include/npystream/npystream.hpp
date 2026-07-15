// Copyright (C) 2024-2026 Maximilian Reininghaus
// Released under European Union Public License 1.2,
// see LICENSE file
// SPDX-License-Identifier: EUPL-1.2

#pragma once

#include <algorithm>
#include <cassert>
#include <complex>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
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

#include <npystream/map_type.hpp>
#include <npystream/tuple_util.hpp>

namespace npystream {
enum class MemoryOrder { Fortran, C, ColumnMajor = Fortran, RowMajor = C };

std::vector<unsigned char> create_npy_header(std::span<uint64_t const> shape, char dtype,
                                             size_t size, MemoryOrder = MemoryOrder::C);

std::vector<unsigned char> create_npy_header(std::span<uint64_t const> shape,
                                             std::span<std::string_view const> labels,
                                             std::span<char const> dtypes,
                                             std::span<size_t const> sizes,
                                             MemoryOrder memory_order);

void wrap_up(std::ofstream& file, uint64_t values_written, size_t header_end_pos,
             std::span<std::string const> labels, std::span<char const> dtypes,
             std::span<size_t const> element_sizes);

namespace detail {
template <typename T>
struct is_complex : std::false_type {};

template <std::floating_point T>
struct is_complex<std::complex<T>> : std::true_type {};

template <tuple_like U, size_t k = 0>
static void fill(U const& tup, std::span<std::byte> buffer) {
  auto constexpr offsets = tuple_info<U>::offsets;

  if constexpr (k < tuple_info<U>::size) {
    auto const& elem = std::get<k>(tup);
    auto constexpr elem_size = sizeof(elem);
    static_assert(tuple_info<U>::element_sizes[k] == elem_size); // sanity check

    std::array<std::byte, elem_size> tmp{};
    memcpy(tmp.data(), std::addressof(elem), elem_size);
    std::copy(tmp.cbegin(), tmp.cend(), std::next(buffer.begin(), offsets[k]));
    fill<U, k + 1>(tup, buffer);
  }
}

/**
 * This class is conditionally used as base class for NpzStream in order
 * to enable/disable the functions based on the template parameters
 */
template <typename Base, typename T>
struct Mixin {
  Base* base;

  Mixin(Base* base_)
      : base{base_} {}

  //! write single scalar value into stream
  Base& operator<<(T val) {
    return (*(base) << std::tuple<T>{val});
  }

  //! write contiguous block of scalar data, given as std::span, into stream
  Base& write(std::span<T const> data) {
    if (base->buffer_size) {
      base->flush_buffer();
    }

    base->appendPayload(std::as_bytes(data));
    base->values_written += data.size();

    return *base;
  }

  //! write contiguous block of scalar data, given as iterator pair, into stream
  template <std::contiguous_iterator TConstIter>
    requires(std::same_as<std::iter_value_t<TConstIter>, T>)
  Base& write(TConstIter begin, TConstIter end) {
    return write(std::span<std::add_const_t<std::iter_value_t<TConstIter>>>{begin, end});
  }
};

class empty {
  struct Private {};

public:
  empty(void*) {};
  void operator<<(Private) const;
  void write();
};

template <typename T, typename... TArgs>
class RealBase : public std::conditional_t<sizeof...(TArgs) == 0, detail::Mixin<RealBase<T>, T>,
                                           detail::empty> {
public:
  using Base =
      std::conditional_t<sizeof...(TArgs) == 0, detail::Mixin<RealBase<T>, T>, detail::empty>;
  friend Base;

  using tuple_type = std::tuple<T, TArgs...>;

  static auto constexpr& dtypes = tuple_info<tuple_type>::data_types;
  static auto constexpr& sizes = tuple_info<tuple_type>::element_sizes;

  using Base::operator<<;
  using Base::write;

protected:
  RealBase()
      : Base(this) {
    if constexpr (std::size_t const size = std::tuple_size_v<tuple_type>; size > 1) {
      labels.reserve(size);
      for (std::size_t i = 0; i < size; ++i) {
        labels.emplace_back(std::format("f{}", i));
      }
    }
  }

  template <typename Container>
  RealBase(Container const& labels_)
      : Base(this)
      , labels{std::cbegin(labels_), std::cend(labels_)} {}

  virtual ~RealBase() noexcept(false) = default;

  virtual void appendPayload(std::span<std::byte const>) = 0;

  void flush_buffer() {
    appendPayload(std::as_bytes(std::span{std::as_const(buffer).begin(), buffer_size}));
    buffer_size = 0;
  }

  std::vector<unsigned char> createPlaceholderHeader() {
    uint64_t const max_elements = std::numeric_limits<uint64_t>::max();
    std::vector<unsigned char> header = std::invoke([&]() {
      size_t constexpr tuple_size = std::tuple_size_v<tuple_type>;

      if (labels.size() == 0) {
        if constexpr (tuple_size == 1) {
          return create_npy_header(std::span<uint64_t const>(&max_elements, 1), map_type(T{}),
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

        std::vector<std::string_view> const label_views(labels.cbegin(), labels.cend());
        return create_npy_header(std::span<uint64_t const>(&max_elements, 1), label_views, dtypes,
                                 sizes, MemoryOrder::C);
      }
    });

    std::fill(std::next(header.begin(), 8), header.end(), '\0');
    header_end_pos = header.size();

    return header;
  }

public:
  //! write single data tuple into stream
  template <tuple_like Tup>
    requires(convertible<Tup, tuple_type>)
  RealBase& operator<<(Tup const& val) {
    detail::fill(val, buffer[buffer_size]);
    if (++buffer_size == buffer_capacity) {
      flush_buffer();
    }
    ++values_written;
    return *this;
  }

  /**
   * Write sequence of data, given as iterator pair, into stream. In case of
   * structured data, the iterator has to return std::tuples whose individual
   * member types match the data types of the file.
   */
  template <typename TConstIter, std::sentinel_for<TConstIter> Sentinel>
    requires(!std::contiguous_iterator<TConstIter> ||
             (std::tuple_size_v<tuple_type> > 1 &&
              convertible<std::iter_value_t<TConstIter>, tuple_type>))
  RealBase& write(TConstIter begin, Sentinel end) {
    auto f = std::bind_front(&RealBase::operator<< <tuple_type>, this);
    auto g = std::function<void(tuple_type)>{f};
    std::ranges::for_each(begin, end, f);
    return *this;
  }

protected:
  size_t header_end_pos;
  uint64_t values_written{}, buffer_size{};
  std::vector<std::string> labels{};

  static size_t constexpr buffer_capacity =
      std::max<size_t>(1, 256 / tuple_info<tuple_type>::sum_sizes);
  std::array<std::array<std::byte, tuple_info<tuple_type>::sum_sizes>, buffer_capacity> buffer{};

  static_assert(sizeof(buffer) == buffer_capacity * tuple_info<tuple_type>::sum_sizes);
};

} // namespace detail

//! Serializable types are the fundamental arithmetic types (int, float, ...), and std::complex.
template <typename... TArgs>
concept npy_serializable =
    ((std::is_arithmetic_v<TArgs> || detail::is_complex<TArgs>::value) && ...);

/**
 * This class represents the handle to a .npy file opened for writing.
 * The template parameter indicates the data type to be written. In case of
 * multiple template parameters, "structured" data are written.
 */
template <npy_serializable T, npy_serializable... TArgs>
class NpyStream : public detail::RealBase<T, TArgs...> {
public:
  using Base = detail::RealBase<T, TArgs...>;
  using Base::operator<<;
  using Base::write;

  //! create a NpyStream (.npy file) at the given path.
  NpyStream(std::filesystem::path const& path)
      : Base{} {
    init(path);
  }

  //! create a NpyStream for structured data at the given path with labelled data columns
  template <typename Container>
  NpyStream(std::filesystem::path const& path, Container const& labels)
      : Base{labels} {
    init(path);
  }

  ~NpyStream() {
    Base::flush_buffer();
    wrap_up(file, Base::values_written, Base::header_end_pos, Base::labels, Base::dtypes,
            Base::sizes);
  }

  virtual void appendPayload(std::span<std::byte const> data) override final {
    file.write(reinterpret_cast<char const*>(data.data()), data.size());
  }

private:
  void init(std::filesystem::path const& path) {
    auto const header = Base::createPlaceholderHeader();
    file.open(path, std::ios_base::binary);
    file.write(reinterpret_cast<char const*>(header.data()), header.size());
  }

  std::ofstream file;
};
} // namespace npystream
