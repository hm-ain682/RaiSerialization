cmake_minimum_required(VERSION 3.28)
project(RaiJson VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Create library target that contains the JSON modules
add_library(RaiJson)

# C++ module interface units and sources for RaiJson
# Use FILE_SET for module interface units so the generator produces proper module maps.
target_sources(RaiJson
    PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES FILES
            src/Common/SortedHashArrayMap.cppm
            src/Common/ThreadPool.cppm
            src/IO/ReadingAheadBuffer.cppm
            src/IO/ReadingAheadDoubleBuffer.cppm
            src/IO/ParallelInputStreamSource.cppm
            src/IO/JsonWriter.cppm
            src/IO/JsonParser.cppm
            src/IO/JsonTokenManager.cppm
            src/IO/JsonTokenizer.cppm
            src/IO/JsonConcepts.cppm
            src/IO/JsonField.cppm
            src/IO/JsonPolymorphic.cppm
            src/IO/JsonFieldSet.cppm
            src/IO/JsonIO.cppm
)


# Expose the target so other projects can link against it
add_library(RaiJson::RaiJson ALIAS RaiJson)

# Install library, module interface units, and export targets for find_package.
install(TARGETS RaiJson
    EXPORT RaiJsonTargets
    FILE_SET cxx_modules DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/rai/json)

install(EXPORT RaiJsonTargets
    FILE RaiJsonTargets.cmake
    NAMESPACE RaiJson::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/RaiJson)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/RaiJsonConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RaiJsonConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/RaiJsonConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/RaiJson)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/RaiJsonConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/RaiJsonConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/RaiJson)

# Allow tests
option(RAIJSON_BUILD_TESTS "Build tests for RaiJson" OFF)
if(RAIJSON_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
