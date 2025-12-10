# Zero-Copy NASDAQ TotalView-ITCH 5.0 Feed Handler

A high-performance, low-latency market data feed handler for parsing NASDAQ TotalView-ITCH 5.0 protocol messages.

## Features

- **Zero-Copy Parsing**: Direct `reinterpret_cast` from buffers to structs, no `memcpy` on hot path
- **Big Endian Handling**: Compile-time optimized byte swapping using `__builtin_bswap`
- **C++20 Strict**: Modern C++ with no exceptions, no RTTI for minimal latency
- **Benchmarked**: Google Benchmark integration for performance validation

## Project Structure

```
├── include/itch/      # Header-only parser library
│   └── compat.hpp     # Endianness utilities
├── src/               # Implementation files (if needed)
├── tests/             # GTest unit tests
├── benchmarks/        # Google Benchmark files
└── CMakeLists.txt     # Build configuration
```

## Requirements

- CMake 3.20+
- C++20 compatible compiler (GCC 10+, Clang 12+, MSVC 2019+)
- Git (for dependency fetching)

## Building

```bash
# Configure
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build -j$(nproc)

# Run tests
cd build && ctest --output-on-failure

# Run benchmarks
./build/itch_benchmark
```

## Quick Start

```cpp
#include <itch/compat.hpp>

// Convert big-endian ITCH field to host byte order
uint32_t price = itch::ntoh(message->price);
```

## License

MIT
