cmake_minimum_required(VERSION 3.28)
project(RaiSerialization VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Allow tests
option(RAISERIALIZATION_BUILD_TESTS "Build tests for RaiSerialization" OFF)

# Allow installing test helper module for external programs
option(RAISERIALIZATION_BUILD_TEST_HELPER "Build and install test helper module" OFF)

# Create library target that contains the serialization modules
add_library(RaiSerialization)

# C++ module interface units and sources for RaiSerialization
# Use FILE_SET for module interface units so the generator produces proper module maps.
target_sources(RaiSerialization
    PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES FILES
            src/Common/SortedHashArrayMap.cppm
            src/Common/ThreadPool.cppm
            src/Serialization/ReadingAheadBuffer.cppm
            src/Serialization/ReadingAheadDoubleBuffer.cppm
            src/Serialization/ParallelInputStreamSource.cppm
            src/Serialization/TokenManager.cppm
            src/Serialization/FormatIO.cppm
            src/Serialization/ObjectConverter.cppm
            src/Serialization/FieldSerializer.cppm
            src/Serialization/PolymorphicConverter.cppm
            src/Serialization/ObjectSerializer.cppm
            src/Serialization/Json/JsonWriter.cppm
            src/Serialization/Parser.cppm
            src/Serialization/Json/JsonTokenizer.cppm
            src/Serialization/Json/JsonIO.cppm
)

# Expose the target so other projects can link against it
add_library(RaiSerialization::RaiSerialization ALIAS RaiSerialization)

# Install library, module interface units, and export targets for find_package.
install(TARGETS RaiSerialization
    EXPORT RaiSerializationTargets
    FILE_SET cxx_modules DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/rai/serialization)
install(EXPORT RaiSerializationTargets
    FILE RaiSerializationTargets.cmake
    NAMESPACE RaiSerialization::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/RaiSerialization)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/RaiSerializationConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)

# Make the install-time option visible to the package config template so
# @RAISERIALIZATION_INSTALL_TEST_HELPER@ is substituted correctly.
set(RAISERIALIZATION_INSTALL_TEST_HELPER ${RAISERIALIZATION_BUILD_TEST_HELPER})

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RaiSerializationConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/RaiSerializationConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/RaiSerialization)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/RaiSerializationConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/RaiSerializationConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/RaiSerialization)

if(RAISERIALIZATION_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
