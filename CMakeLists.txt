cmake_minimum_required(VERSION 3.14)
project(NanoMatchBenchmark)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native -mtune=native -flto")

include(FetchContent)
FetchContent_Declare(
  googlebenchmark
  GIT_REPOSITORY https://github.com/google/benchmark.git
  GIT_TAG v1.8.3
)

set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googlebenchmark)

add_executable(engine_bench benchmark.cpp LimitOrderBook.cpp BaselineOrderBook.hpp)
target_link_libraries(engine_bench benchmark::benchmark pthread)

add_executable(engine_main main.cpp LimitOrderBook.cpp)
target_link_libraries(engine_main pthread)