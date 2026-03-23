cmake_minimum_required(VERSION 3.24)

project(hackarena3_template_bot LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(HACKARENA3_SDK_ROOT "" CACHE PATH "Path to the extracted hackarena3 C++ SDK package root")
if(HACKARENA3_SDK_ROOT)
    list(PREPEND CMAKE_PREFIX_PATH "${HACKARENA3_SDK_ROOT}")
endif()

find_package(hackarena3 CONFIG REQUIRED)

add_executable(bot user/src/bot/main.cpp)
target_link_libraries(bot PRIVATE hackarena3::hackarena3)

if(COMMAND hackarena3_copy_runtime_dlls)
    hackarena3_copy_runtime_dlls(bot)
endif()

if(MSVC)
    target_compile_options(bot PRIVATE /W4 /permissive-)
else()
    target_compile_options(bot PRIVATE -Wall -Wextra -Wpedantic)
endif()
