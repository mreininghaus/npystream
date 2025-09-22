// Copyright (C) 2024 Maximilian Reininghaus
// Released under European Union Public License 1.2,
// see LICENSE file
// SPDX-License-Identifier: EUPL-1.2

#include <algorithm>
#include <chrono>
#include <complex>
#include <cstdlib>
#include <deque>
#include <functional>
#include <list>
#include <numeric>
#include <ranges>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

#include <npystream/npystream.hpp>

int main() {
  {
    npystream::NpyStream<float> stream{"float.npy"};

    std::vector<float> vec(1000);
    std::iota(vec.begin(), vec.end(), 1.f);
    stream.write(std::span{std::as_const(vec)});

    std::list<float> const l{vec.crbegin(), vec.crend()};
    stream.write(l.begin(), l.end());
  }

  {
    auto const i = std::ranges::iota_view<int, int>{1, 1024};
    auto const z = i | std::ranges::views::transform([](int i) {
                     return std::tuple{i, i * i, i * i * i};
                   });
    npystream::NpyStream<int, int, int> s{"int-structured.npy", std::array{"x", "y", "z"}};
    s.write(z.begin(), z.end());
  }

  {
    using namespace std::complex_literals;
    npystream::NpyStream<std::complex<double>> stream{"complex.npy"};
    stream << 1i << std::complex{1.} << std::make_tuple(1. + 1i);
  }

  {
    std::deque<bool> vec(8);
    std::ranges::fill(vec, true);
    npystream::NpyStream<bool> stream{"bool.npy"};
    stream.write(vec.cbegin(), vec.cend());
  }

  {
    std::vector<unsigned> data;
    auto const i = std::ranges::iota_view<unsigned, unsigned>{5u, 10u};
    data.insert(data.end(), i.begin(), i.end());

    npystream::NpyStream<unsigned> stream{"unsigned.npy"};
    stream << 1u << 2u << 3u << 4u;
    stream.write(std::span{std::as_const(data)});
    stream << 10u << 11u << 12u;

    std::transform(data.cbegin(), data.cend(), data.begin(),
                   std::bind_front(std::plus<unsigned>{}, 8u));
    stream.write(std::span{std::as_const(data)});
    stream << 18u << std::tuple{19u} << std::to_array({20u});
  }

  return EXIT_SUCCESS;
}
