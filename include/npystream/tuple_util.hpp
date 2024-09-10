// Copyright (C) 2024 Maximilian Reininghaus
// Released under European Union Public License 1.2,
// see LICENSE file
// SPDX-License-Identifier: EUPL-1.2

#pragma once

#include <array>
#include <cstddef>
#include <iterator>
#include <numeric>
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

template <typename T>
bool constexpr has_bool() {
  auto lambda = []<size_t... N>(std::index_sequence<N...>) {
    return (is_bool_v<std::decay_t<typename std::tuple_element<N, T>::type>> || ...);
  };

  return lambda(std::make_index_sequence<std::tuple_size_v<T>>());
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

  using index_sequence_type = decltype(std::make_index_sequence<size>());

  // prevent any instantiation
  tuple_info() = delete;
  tuple_info(tuple_info<Tup> const&) = delete;
  tuple_info& operator=(tuple_info const&) = delete;

private:
  template <std::size_t... N>
  static std::array<char, size> constexpr getDataTypes(std::index_sequence<N...>) {
    std::array<char, size> s = {map_type(std::tuple_element_t<N, Tup>{})...};
    return s;
  }

  template <std::size_t... N>
  static std::array<size_t, size> constexpr getElementSizes(std::index_sequence<N...>) {
    std::array<size_t, size> s = {sizeof(std::tuple_element_t<N, Tup>)...};
    return s;
  }

public:
  static std::array<char, size> constexpr data_types = getDataTypes(index_sequence_type{});
  static std::array<size_t, size> constexpr element_sizes = getElementSizes(index_sequence_type{});
  static size_t constexpr sum_sizes = std::reduce(element_sizes.cbegin(), element_sizes.cend());
  static std::array<size_t, size> constexpr offsets = []() {
    std::array<size_t, size> values;
    std::exclusive_scan(element_sizes.cbegin(), element_sizes.cend(), values.begin(), size_t{});
    return values;
  }();
};
} // namespace npystream
