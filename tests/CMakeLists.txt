# Tests for RaiJson (uses googletest if needed)

# Make tests optional and fall back to fetching googletest if necessary.
include(FetchContent)

# Provide option to enable/disable tests from superbuild
option(RAIJSON_BUILD_TESTS "Build tests for RaiJson" ON)

if(NOT RAIJSON_BUILD_TESTS)
  return()
endif()

enable_testing()

# If gtest is not available, download via FetchContent
if(NOT TARGET gtest_main)
  FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
  )
  if (WIN32)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  endif()
  FetchContent_MakeAvailable(googletest)
endif()

# GoogleTest compiler warnings terser
foreach(gtest_target gtest gtest_main gmock gmock_main)
  if(TARGET ${gtest_target})
    target_compile_options(${gtest_target} PRIVATE
      $<$<CXX_COMPILER_ID:Clang>:-Wno-everything>
      $<$<CXX_COMPILER_ID:GNU>:-w>
    )
  endif()
endforeach()

# Basic test that only depends on RaiJson
add_executable(RaiJson_BasicTest BasicJsonTest.cpp)
target_link_libraries(RaiJson_BasicTest PRIVATE RaiJson::RaiJson gtest_main)
add_test(NAME RaiJson_BasicTest COMMAND RaiJson_BasicTest)

# If the main rai project is present, also build the more extensive tests that depend on rai_compiler_core
if(TARGET rai_compiler_core)
    add_executable(JsonTest JsonTest.cpp)
    target_link_libraries(JsonTest PRIVATE RaiJson::RaiJson rai_compiler_core gtest_main)
    add_test(NAME JsonTest COMMAND JsonTest)

    add_executable(JsonBenchmark JsonBenchmark.cpp)
    target_link_libraries(JsonBenchmark PRIVATE RaiJson::RaiJson rai_compiler_core gtest_main)
endif()
