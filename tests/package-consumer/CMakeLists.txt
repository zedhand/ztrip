# Builds against an installed ztrip (a release archive), the way a host would.
# Used by the release workflow:
#   cmake -S tests/package-consumer -B build/consumer -DCMAKE_PREFIX_PATH=<archive>
cmake_minimum_required(VERSION 3.24)
project(ztrip_package_consumer LANGUAGES CXX)

find_package(ztrip CONFIG REQUIRED)

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE ztrip::core)
target_compile_definitions(consumer PRIVATE
  EXPECTED_VERSION="${ztrip_VERSION_FULL}")
