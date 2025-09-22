// Copyright (C) 2024 Maximilian Reininghaus
// Released under European Union Public License 1.2,
// see LICENSE file
// SPDX-License-Identifier: EUPL-1.2

#pragma once

#include <complex>
#include <concepts>
#include <type_traits>

namespace npystream {

template <std::floating_point F>
char consteval map_type(std::complex<F>) {
  return 'c';
}

template <typename T>
  requires(std::is_arithmetic_v<T>)
char consteval map_type(T) {
  if constexpr (std::is_same_v<T, bool>) {
    return 'b';
  } else if constexpr (std::is_integral_v<T>) {
    return std::is_signed_v<T> ? 'i' : 'u';
  } else if constexpr (std::is_floating_point_v<T>) {
    return 'f';
  }
}

} // namespace npystream
