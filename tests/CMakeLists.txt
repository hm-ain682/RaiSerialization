# Tests for RaiSerialization (uses googletest if needed)

# Make tests optional and fall back to fetching googletest if necessary.
include(FetchContent)

# Provide option to enable/disable tests from superbuild
option(RAISERIALIZATION_BUILD_TESTS "Build tests for RaiSerialization" ON)

if(NOT RAISERIALIZATION_BUILD_TESTS)
  return()
endif()

enable_testing()

# If gtest is not available, download via FetchContent
if(NOT TARGET GTest::gtest_main AND NOT TARGET gtest_main)
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

# Create test helper library (moved from top-level)
add_library(RaiSerializationTest)
target_sources(RaiSerializationTest
    PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES FILES
            JsonTestHelper.cppm
)
# Ensure GTest import names exist if the downloaded targets are present
if(TARGET gtest AND NOT TARGET GTest::gtest)
    add_library(GTest::gtest ALIAS gtest)
endif()
if(TARGET gtest_main AND NOT TARGET GTest::gtest_main)
    add_library(GTest::gtest_main ALIAS gtest_main)
endif()

target_link_libraries(RaiSerializationTest PUBLIC RaiSerialization::RaiSerialization GTest::gtest)
add_library(RaiSerialization::RaiSerializationTest ALIAS RaiSerializationTest)

if(RAISERIALIZATION_BUILD_TEST_HELPER)
    install(TARGETS RaiSerializationTest
        EXPORT RaiSerializationTargets
        FILE_SET cxx_modules DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/rai/serialization/test)
endif()

# Build the more extensive tests
add_executable(RaiSerialization_JsonTest JsonTest.cpp)
target_link_libraries(RaiSerialization_JsonTest PRIVATE RaiSerialization::RaiSerializationTest GTest::gtest_main)
target_sources(RaiSerialization_JsonTest PRIVATE JsonEnumFieldTest.cpp)
add_test(NAME RaiSerialization_JsonTest COMMAND RaiSerialization_JsonTest)

add_executable(RaiSerialization_JsonBenchmark JsonBenchmark.cpp)
target_link_libraries(RaiSerialization_JsonBenchmark PRIVATE RaiSerialization::RaiSerialization GTest::gtest_main)
add_test(NAME RaiSerialization_JsonBenchmark COMMAND RaiSerialization_JsonBenchmark)
