#include <cstdlib>
#include <deque>
#include <ranges>

#include <npystream/npzstream.hpp>

void write_scalar() {
  using T = uint32_t;

  auto stream = npystream::NpzStream<T>("scalar.npz", "data", npystream::CompressionLevel::Store);

  stream << T{0x1122} << T{0x3344} << T{0x5566};

  // non-contiguous input
  auto const view = std::views::iota(T{0}, T{100}) | std::ranges::to<std::deque>();
  stream.write(view.begin(), view.end());

  // contiguous input
  auto const view2 = std::views::iota(T{100}, T{200}) | std::ranges::to<std::vector>();
  stream.write(view2.begin(), view2.end());

  // lazily computed input
  auto const view3 = std::views::iota(T{200}, T{300});
  stream.write(view3.begin(), view3.end());
}

void write_tabular() {
  auto stream = npystream::NpzStream<int32_t, float>{"tabular.npz", "data",
                                                     npystream::CompressionLevel::Store};

  auto const val_tup = std::tuple{int32_t{3}, 3.f};
  stream << val_tup << val_tup;

  auto const v =
      std::views::zip(std::views::iota(int32_t{0}), std::views::repeat(5.f)) | std::views::take(5);

  stream.write(v.begin(), v.end());
}

int main() {
  write_scalar();
  write_tabular();

  return EXIT_SUCCESS;
}
