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

# Build the more extensive tests
add_executable(RaiJson_JsonTest JsonTest.cpp)
target_sources(RaiJson_JsonTest PRIVATE
  FILE_SET CXX_MODULES FILES
    JsonTestHelper.cppm
)
target_link_libraries(RaiJson_JsonTest PRIVATE RaiJson::RaiJson gtest_main)
add_test(NAME RaiJson_JsonTest COMMAND RaiJson_JsonTest)

add_executable(RaiJson_JsonBenchmark JsonBenchmark.cpp)
target_link_libraries(RaiJson_JsonBenchmark PRIVATE RaiJson::RaiJson gtest_main)
add_test(NAME RaiJson_JsonBenchmark COMMAND RaiJson_JsonBenchmark)
