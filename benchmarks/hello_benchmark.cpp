/**
 * @file hello_benchmark.cpp
 * @brief Placeholder benchmark to verify Google Benchmark integration.
 *
 * This file validates that:
 * 1. CMake correctly links against Google Benchmark.
 * 2. The itch_parser header-only library is accessible.
 * 3. The compat.hpp byte swap utilities work correctly.
 */

#include <benchmark/benchmark.h>
#include <cstdint>
#include <itch/compat.hpp>

/**
 * @brief Benchmark the bswap32 function to verify zero-overhead.
 *
 * On x86/x64, this should compile to a single BSWAP instruction.
 * Expected throughput: ~1 cycle per operation.
 */
static void BM_Bswap32(benchmark::State &state) {
  uint32_t value = 0x12345678;
  for (auto _ : state) {
    benchmark::DoNotOptimize(value = itch::bswap32(value));
  }
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Bswap32);

/**
 * @brief Benchmark the bswap64 function.
 */
static void BM_Bswap64(benchmark::State &state) {
  uint64_t value = 0x123456789ABCDEF0ull;
  for (auto _ : state) {
    benchmark::DoNotOptimize(value = itch::bswap64(value));
  }
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Bswap64);

/**
 * @brief Baseline: empty loop to measure benchmark overhead.
 */
static void BM_Baseline(benchmark::State &state) {
  for (auto _ : state) {
    benchmark::ClobberMemory();
  }
}
BENCHMARK(BM_Baseline);
