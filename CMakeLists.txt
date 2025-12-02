cmake_minimum_required(VERSION 3.28)
project(RaiJson LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Create library target that contains the JSON modules
add_library(RaiJson)

# C++ module interface units and sources for RaiJson
# Use FILE_SET for module interface units so the generator produces proper module maps.
target_sources(RaiJson
    PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES FILES
            src/IO/ReadingAheadBuffer.cppm
            src/IO/ReadingAheadDoubleBuffer.cppm
            src/IO/ParallelInputStreamSource.cppm
            src/Common/ThreadPool.cppm
            src/IO/JsonTokenManager.cppm
            src/IO/JsonTokenizer.cppm
            src/IO/JsonParser.cppm
            src/IO/JsonWriter.cppm
            src/IO/JsonBinding.cppm
            src/IO/JsonIO.cppm
            src/IO/SortedHashArrayMap.cppm
)

# Expose the target so other projects can link against it
add_library(RaiJson::RaiJson ALIAS RaiJson)

# Allow tests
option(RAIJSON_BUILD_TESTS "Build tests for RaiJson" OFF)
if(RAIJSON_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
