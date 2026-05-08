cmake_minimum_required(VERSION 3.24)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

if(POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW)
endif()

project(fuck_onedrive LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)

set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 7b6aead9fb88b3623e3b3725ebb42670cbe4c579
)

FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG db78ac1d7716f56fc9f1b030b715f872f93964e4
)

FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG dac07199cfd761113d966eb8ad739254e10df2fe
)

FetchContent_MakeAvailable(glfw nlohmann_json)

FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
  FetchContent_Populate(imgui)
endif()

add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_demo.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(imgui
  PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${glfw_SOURCE_DIR}/include
)

find_package(OpenGL REQUIRED)

add_executable(fuck_onedrive WIN32
  src/app.cpp
  src/app.h
  src/common.cpp
  src/common.h
  src/main.cpp
  src/onedrive_recovery.cpp
  src/onedrive_recovery.h
  src/onedrive_system.cpp
  src/onedrive_system.h
  src/sha256.cpp
  src/sha256.h
)

target_compile_definitions(fuck_onedrive
  PRIVATE
    NOMINMAX
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
)

target_compile_options(fuck_onedrive
  PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /EHsc /utf-8 /permissive->
)

target_link_libraries(fuck_onedrive
  PRIVATE
    imgui
    glfw
    nlohmann_json::nlohmann_json
    OpenGL::GL
    bcrypt
    ole32
    shell32
)
