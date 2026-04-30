cmake_minimum_required(VERSION 3.14)
project(graphann LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Optimization flags
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Find OpenMP
if(APPLE AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # Homebrew libomp paths for Apple Silicon
    set(OpenMP_CXX_FLAGS "-Xpreprocessor -fopenmp -I/opt/homebrew/opt/libomp/include")
    set(OpenMP_CXX_LIB_NAMES omp)
    set(OpenMP_omp_LIBRARY /opt/homebrew/opt/libomp/lib/libomp.dylib)
endif()
find_package(OpenMP REQUIRED)

# Library with shared sources
add_library(graphann_lib STATIC
    src/distance.cpp
    src/io_utils.cpp
    src/vamana_index.cpp
    src/experiments.cpp
    src/quantizer.cpp
)
target_include_directories(graphann_lib PUBLIC include)
target_link_libraries(graphann_lib PUBLIC OpenMP::OpenMP_CXX)

# Build index executable
add_executable(build_index src/build_index.cpp)
target_link_libraries(build_index PRIVATE graphann_lib)

# Search index executable
add_executable(search_index src/search_index.cpp)
target_link_libraries(search_index PRIVATE graphann_lib)

# Benchmark executable (systematic optimization verification)
add_executable(benchmark src/benchmark.cpp)
target_link_libraries(benchmark PRIVATE graphann_lib)

# Experiment runner v3 (minimal, working version)
add_executable(experiment_runner_v3 src/experiment_runner_v3.cpp)
target_link_libraries(experiment_runner_v3 PRIVATE graphann_lib)
