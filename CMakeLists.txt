cmake_minimum_required(VERSION 3.24)

project(
    hackarena3_cpp_wrapper
    VERSION 0.1.0
    LANGUAGES CXX
)

set(HACKARENA3_WRAPPER_VERSION "0.1.0b9")
set(HACKARENA3_PACKAGE_NAME "hackarena3")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if(DEFINED VCPKG_TARGET_TRIPLET AND VCPKG_TARGET_TRIPLET MATCHES "^[Aa][Rr][Mm].*-windows$")
        message(FATAL_ERROR
            "Unsupported Windows configuration: MinGW/GNU compiler with vcpkg triplet "
            "'${VCPKG_TARGET_TRIPLET}'. This project uses MSVC triplets on Windows. "
            "In CLion, select the Visual Studio toolchain and set Architecture to 'amd64'."
        )
    endif()
    if(DEFINED VCPKG_TARGET_TRIPLET AND VCPKG_TARGET_TRIPLET MATCHES ".*-windows$")
        message(FATAL_ERROR
            "Unsupported Windows configuration: MinGW/GNU compiler with vcpkg triplet "
            "'${VCPKG_TARGET_TRIPLET}'. Use the Visual Studio/MSVC toolchain instead. "
            "In CLion, open Settings > Build, Execution, Deployment > Toolchains, select "
            "Visual Studio, and set Architecture to 'amd64'."
        )
    endif()
endif()

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

find_package(Threads REQUIRED)
find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

include(GenerateGrpcSources)

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
        #"$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)
target_link_libraries(hackarena3_proto PUBLIC gRPC::grpc++ protobuf::libprotobuf)
set_target_properties(hackarena3_proto PROPERTIES POSITION_INDEPENDENT_CODE ON)

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
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>"
)

target_link_libraries(
    hackarena3
    PRIVATE
        hackarena3_proto
        Threads::Threads
        nlohmann_json::nlohmann_json
)

set_target_properties(hackarena3 PROPERTIES OUTPUT_NAME hackarena3)

add_executable(
    bot
    template/user/src/bot/main.cpp
)
target_link_libraries(bot PRIVATE hackarena3)

set(HACKARENA3_CMAKE_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/${HACKARENA3_PACKAGE_NAME}")

install(
    TARGETS hackarena3 
    EXPORT hackarena3Targets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

install(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

install(
    FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
        "${CMAKE_CURRENT_SOURCE_DIR}/template/system/manifest.toml"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/${HACKARENA3_PACKAGE_NAME}"
)

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

set(HACKARENA3_TEMPLATE_ARCHIVE
    "${CMAKE_BINARY_DIR}/release-assets/hackarena3-cpp-template-${HACKARENA3_WRAPPER_VERSION}.zip"
)

add_custom_command(
    OUTPUT "${HACKARENA3_TEMPLATE_ARCHIVE}"
    COMMAND
        "${CMAKE_COMMAND}"
        -DREPO_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DSTAGING_DIR=${CMAKE_BINARY_DIR}/template-release
        -DOUTPUT_ZIP=${HACKARENA3_TEMPLATE_ARCHIVE}
        -DWRAPPER_VERSION=${HACKARENA3_WRAPPER_VERSION}
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/PackageTemplateRelease.cmake"
    DEPENDS
        cmake/PackageTemplateRelease.cmake
        template/CMakeLists.txt
        template/README.md
        template/system/manifest.toml
        template/user/src/bot/main.cpp
    COMMENT "Creating template release asset"
    VERBATIM
)

add_custom_target(hackarena3_template_zip DEPENDS "${HACKARENA3_TEMPLATE_ARCHIVE}")

if(MSVC)
    target_compile_options(hackarena3_proto PRIVATE /FS)
    target_compile_options(hackarena3 PRIVATE /W4 /permissive-)
    target_compile_options(bot PRIVATE /W4 /permissive-)
    target_compile_options(hackarena3 PRIVATE /FS)
    target_compile_options(bot PRIVATE /FS)
else()
    target_compile_options(hackarena3 PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(bot PRIVATE -Wall -Wextra -Wpedantic)
endif()

set(CPACK_PACKAGE_NAME "${HACKARENA3_PACKAGE_NAME}")
set(CPACK_PACKAGE_VENDOR "HackArena")
set(CPACK_PACKAGE_VERSION "${HACKARENA3_WRAPPER_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "HackArena3 C++ wrapper SDK")
set(CPACK_PACKAGE_FILE_NAME
    "${HACKARENA3_PACKAGE_NAME}-cpp-sdk-${HACKARENA3_WRAPPER_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}"
)
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)

if(WIN32)
    set(CPACK_GENERATOR "ZIP")
else()
    set(CPACK_GENERATOR "TGZ")
endif()

include(CPack)

add_custom_target(
    hackarena3_sdk_tgz
    COMMAND "${CMAKE_CPACK_COMMAND}" --config "${CMAKE_BINARY_DIR}/CPackConfig.cmake" -C $<CONFIG>
    DEPENDS hackarena3 hackarena3_proto
    COMMENT "Creating SDK release package with CPack"
    VERBATIM
)
