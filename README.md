# npystream

npystream is a C++20 library for streaming numerical data into NPY files as defined in
[NEP 1 — A simple file format for NumPy arrays](https://numpy.org/neps/nep-0001-npy-format.html).

## API
To open a new file, create an object of type `npystream::NpyStream<T...>`, e.g.:
```c++
npystream::NpyStream<float> scalar_stream{"float.npy"};
npystream::NpyStream<int, double> structured_stream{"struct.npy", std::array{"field1", "field2"}};
```
The template parameters determine the data type to be written. All arithmetic types (integers,
floating point numbers, booleans) are allowed. In case of a single template parameter,
the file will contain a plain array of that type. In case of multiple template parameters,
the file will hold a [structured array](https://numpy.org/doc/stable/user/basics.rec.html) (similar to a table with labelled. columns)
The second argument of the constructor is optional and allows passing the field labels as container of strings.

Please note that only one-dimensional data are supported by npystream.

Upon destruction of the `NpyStream` object, the NPY header with the total number of elements
is written before the file gets closed.

Writing single data points into the file is possible with the `<<` operator, either with scalar
values or, in case of a structured array, tuple-like[^1] values:
```c++
scalar_stream << 1.23f << 4.56f << 7.89f;
structured_stream << std::tuple{456, 3.1415} << std::pair{42, 2.718};
```

To write multiple values, e.g. a whole container, at once, one can use `NpyStream::write(Iter begin, Iter end)`,
which writes a range of values given as begin and end iterators:
```c++
std::vector<float> vec(1000);
scalar_stream.write(std::span{std::as_const(vec)});

auto const r = std::ranges::iota_view{1, 10} | std::ranges::views::transform([](int i) {return std::pair{i, 3.14 * i};});
structured_stream.write(r.begin(), r.end());
```  


[^1]: "tuple-like" means any type `T` that behaves similar to `std::tuple`, in the sense that `std::get<N>(T&)`,
`std::tuple_size<T>`, etc. can be used with `T`. The STL types that are compatible with this interface are `std::tuple`
itself, `std::pair` and `std::array`.
