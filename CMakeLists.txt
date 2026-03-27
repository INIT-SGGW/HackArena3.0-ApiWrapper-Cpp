cmake_minimum_required(VERSION 4.24)

project(
    hackarena3_cpp_wrapper
    VERSION 0.1.0
    LANGUAGES CXX
)

# =========================
# Force Release build only
# =========================
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CONFIGURATION_TYPES "Release" CACHE STRING "" FORCE)

# =========================
# Versions
# =========================
set(HACKARENA3_WRAPPER_VERSION "0.1.0b9")
set(HACKARENA3_PACKAGE_NAME "hackarena3")

# =========================
# Compiler settings
# =========================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# =========================
# Windows sanity checks
# =========================
if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if(DEFINED VCPKG_TARGET_TRIPLET AND VCPKG_TARGET_TRIPLET MATCHES ".*-windows$")
        message(FATAL_ERROR
            "Unsupported Windows configuration: MinGW/GNU with vcpkg. Use MSVC."
        )
    endif()
endif()

# =========================
# Output dirs
# =========================
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# =========================
# Dependencies
# =========================
find_package(Threads REQUIRED)
find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

include(GenerateGrpcSources)

# =========================
# Protobuf generation
# =========================
set(HA3_PROTO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/HackArena3.0-Proto/proto")
set(HA_PROTO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/HackArena-Proto/proto")

hackarena3_generate_grpc_sources(
    GENERATED_PROTO_SRCS
    GENERATED_PROTO_HDRS
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
    "${HA3_PROTO_ROOT}"
    "${HA_PROTO_ROOT}"
)

add_library(hackarena3_proto STATIC ${GENERATED_PROTO_SRCS} ${GENERATED_PROTO_HDRS})

target_include_directories(
    hackarena3_proto
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

target_link_libraries(hackarena3_proto PUBLIC gRPC::grpc++ protobuf::libprotobuf)
set_target_properties(hackarena3_proto PROPERTIES POSITION_INDEPENDENT_CODE ON)

# =========================
# Main library
# =========================
add_library(
  hackarena3 SHARED
    src/types.cpp
    src/client.cpp
    src/config.cpp
    src/auth.cpp
    src/game_token.cpp
    src/runtime_common.cpp
    src/runtime_discovery.cpp
    src/runtime_race.cpp
    src/runtime_convert.cpp
    src/runtime_loop.cpp
    src/runtime.cpp
    src/detail/environment.cpp
    src/detail/path_utils.cpp
    src/detail/process.cpp
)

target_compile_definitions(
    hackarena3
    PRIVATE HACKARENA3_WRAPPER_VERSION="${HACKARENA3_WRAPPER_VERSION}"
)

target_include_directories(
    hackarena3
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(
    hackarena3
    PRIVATE
        hackarena3_proto
    PRIVATE
        Threads::Threads
        nlohmann_json::nlohmann_json
)

set_target_properties(hackarena3 PROPERTIES OUTPUT_NAME hackarena3)

# =========================
# Example bot
# =========================
add_executable(
    bot
    template/user/src/bot/main.cpp
)

target_link_libraries(bot PRIVATE hackarena3)

# =========================
# Strip binaries (reduce size)
# =========================
if(NOT MSVC)
    add_custom_command(TARGET bot POST_BUILD
        COMMAND ${CMAKE_STRIP} $<TARGET_FILE:bot>
    )
endif()


# =========================
# Install
# =========================
set(HACKARENA3_CMAKE_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/${HACKARENA3_PACKAGE_NAME}")

install(
    TARGETS hackarena3 hackarena3_proto
    EXPORT hackarena3Targets
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

install(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

install(
    DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

# =========================
# Minimal vcpkg packaging (FIXED)
# =========================
if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    set(HACKARENA3_VENDORED_VCPKG_PREFIX "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
endif()

if(HACKARENA3_VENDORED_VCPKG_PREFIX AND EXISTS "${HACKARENA3_VENDORED_VCPKG_PREFIX}")
    install(
        DIRECTORY
            "${HACKARENA3_VENDORED_VCPKG_PREFIX}/bin"
        DESTINATION "vendor/bin"
        OPTIONAL
    )

    install(
        DIRECTORY
            "${HACKARENA3_VENDORED_VCPKG_PREFIX}/lib"
        DESTINATION "vendor/lib"
        OPTIONAL
        FILES_MATCHING
            PATTERN "*.dll"
            PATTERN "*.so"
            PATTERN "*.dylib"
    )
else()
    message(WARNING "Vcpkg not found — SDK will not be self-contained.")
endif()

# =========================
# CMake package config
# =========================
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/hackarena3Config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/hackarena3Config.cmake"
    INSTALL_DESTINATION "${HACKARENA3_CMAKE_INSTALL_DIR}"
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/hackarena3ConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion
)

install(
    EXPORT hackarena3Targets
    FILE hackarena3Targets.cmake
    NAMESPACE hackarena3::
    DESTINATION "${HACKARENA3_CMAKE_INSTALL_DIR}"
)

install(
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/hackarena3Config.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/hackarena3ConfigVersion.cmake"
    DESTINATION "${HACKARENA3_CMAKE_INSTALL_DIR}"
)

# =========================
# CPack (Release only)
# =========================
set(CPACK_PACKAGE_NAME "${HACKARENA3_PACKAGE_NAME}")
set(CPACK_PACKAGE_VENDOR "HackArena")
set(CPACK_PACKAGE_VERSION "${HACKARENA3_WRAPPER_VERSION}")
set(CPACK_PACKAGE_FILE_NAME
    "${HACKARENA3_PACKAGE_NAME}-cpp-sdk-${HACKARENA3_WRAPPER_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}"
)
set(CPACK_GENERATOR "TGZ")

include(CPack)

add_custom_target(
    hackarena3_sdk_tgz
    COMMAND "${CMAKE_CPACK_COMMAND}"
        --config "${CMAKE_BINARY_DIR}/CPackConfig.cmake"
        -C Release
    DEPENDS hackarena3 hackarena3_proto
)
