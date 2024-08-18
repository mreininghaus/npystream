// Copyright (C) 2024 Maximilian Reininghaus
// Released under European Union Public License 1.2
// licence available in 'EUPL-1.2 EN.txt' file

#pragma once

#include <array>
#include <cstddef>
#include <iterator>
#include <tuple>
#include <type_traits>
#include <utility>

#include <npystream/map_type.hpp>

namespace npystream {

template <typename T>
concept tuple_like = requires(T a) { std::tuple_size<T>::value; };

namespace detail {
template <typename T>
struct is_bool : public std::is_same<T, bool> {};

template <typename T>
bool constexpr is_bool_v = is_bool<T>::value;

template <typename T, size_t N, size_t i>
bool constexpr has_bool_impl() {
  if constexpr (i < N)
    return is_bool_v<std::decay_t<typename std::tuple_element<i, T>::type>> ||
           has_bool_impl<T, N, i + 1>();
  else
    return false;
}

template <typename T>
bool constexpr has_bool() {
  return has_bool_impl<T, std::tuple_size<T>::value, 0>();
}

template <typename A, typename B>
struct ConvertibleTester {
  template <typename From, typename To, std::size_t... N>
    requires(std::tuple_size<From>::value == std::tuple_size<To>::value &&
             sizeof...(N) == std::tuple_size<To>::value)
  static auto constexpr check_tuple_convertible(std::index_sequence<N...>)
      -> std::bool_constant<(std::is_same_v<std::remove_cvref_t<std::tuple_element_t<N, From>>,
                                            std::tuple_element_t<N, To>> &&
                             ...)>;

  template <typename From, typename To, std::size_t... N>
    requires(!(std::tuple_size<From>::value == std::tuple_size<To>::value &&
               sizeof...(N) == std::tuple_size<To>::value))
  static std::false_type constexpr check_tuple_convertible(std::index_sequence<N...>);

  template <tuple_like U1, tuple_like U2>
  static auto test(void*) {
    return decltype(check_tuple_convertible<U1, U2>(
        std::make_index_sequence<std::tuple_size_v<U1>>{})){};
  }

  template <typename U1, typename U2>
    requires(!tuple_like<U1> || !tuple_like<U2>)
  static std::false_type test(void*);

  static bool constexpr value = decltype(test<A, B>(nullptr))::value;
};
} // namespace detail

template <typename TupA, typename TupB>
concept convertible = (detail::ConvertibleTester<TupA, TupB>::value);

template <tuple_like Tup>
struct tuple_info {
  static auto constexpr size = std::tuple_size_v<Tup>;
  static bool constexpr has_bool_element = detail::has_bool<Tup>();

  // prevent any instantiation
  tuple_info() = delete;
  tuple_info(tuple_info<Tup> const&) = delete;
  tuple_info& operator=(tuple_info const&) = delete;

private:
  static std::array<char, size> constexpr getDataTypes() {
    std::array<char, size> types{};
    getDataTypes_impl<0>(types);
    return types;
  }

  static std::array<size_t, size> constexpr getElementSizes() {
    std::array<size_t, size> sizes{};
    getSizes_impl<0>(sizes);
    return sizes;
  }

public:
  static std::array<char, size> constexpr data_types = getDataTypes();
  static std::array<size_t, size> constexpr element_sizes = getElementSizes();

private:
  static size_t constexpr sum_size_impl() {
    size_t sum{};

    for (auto const& v : element_sizes) {
      sum += v;
    }

    return sum;
  }

public:
  static size_t constexpr sum_sizes = sum_size_impl();

private:
  static std::array<size_t, size> constexpr calc_offsets() {
    std::array<size_t, size> offsets{};
    offsets[0] = 0;
    calc_offsets_impl<1>(offsets);
    return offsets;
  }

public:
  static std::array<size_t, size> constexpr offsets = calc_offsets();

private:
  template <int k>
  static void constexpr getDataTypes_impl(std::array<char, size>& sizes) {
    if constexpr (k < size) {
      sizes[k] = map_type(std::tuple_element_t<k, Tup>{});
      getDataTypes_impl<k + 1>(sizes);
    }
  }

  template <int k>
  static void constexpr getSizes_impl(std::array<size_t, size>& sizes) {
    if constexpr (k < size) {
      sizes[k] = sizeof(std::tuple_element_t<k, Tup>);
      getSizes_impl<k + 1>(sizes);
    }
  }

  template <int k>
  static void constexpr calc_offsets_impl(std::array<size_t, size>& offsets) {
    if constexpr (k < size) {
      offsets[k] = offsets[k - 1] + element_sizes[k - 1];
      calc_offsets_impl<k + 1>(offsets);
    }
  }
};
} // namespace npystream
