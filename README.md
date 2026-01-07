# jobkit
Game-engine friendly job scheduler with priorities, dependency counters, wait-helping, and zero-heap runtime mode.

## Build
Requires C++20 and CMake 3.20+.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Install and use

```sh
cmake --install build --prefix <install-dir>
```

```cmake
find_package(jobkit CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE jobkit::jobkit)
```

To enable telemetry fields:

```sh
cmake -S . -B build -DJOBKIT_ENABLE_TELEMETRY=ON
```

## Tests

```sh
cmake -S . -B build -DJOBKIT_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build
```
