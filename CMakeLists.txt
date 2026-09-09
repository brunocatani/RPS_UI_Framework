cmake_minimum_required(VERSION 4.2)

set(NAME "RPS_UI_Framework")
set(FRIENDLY_NAME "RPS UI Framework")
set(VERSION 0.1.0)
set(ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(SOURCE_DIR "${ROOT_DIR}/src")
set(SDK_INCLUDE_DIR "${ROOT_DIR}/SDK/include")
get_filename_component(WORKSPACE_ROOT "${ROOT_DIR}/../.." ABSOLUTE)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
  message(FATAL_ERROR "In-source builds are not allowed")
endif()

macro(set_from_environment VARIABLE)
  if(NOT DEFINED ${VARIABLE} AND DEFINED ENV{${VARIABLE}})
    set(${VARIABLE} $ENV{${VARIABLE}})
  endif()
endmacro()

set_from_environment(VCPKG_ROOT)
if(NOT DEFINED VCPKG_ROOT)
  message(FATAL_ERROR "VCPKG_ROOT is not set")
endif()
set(CMAKE_TOOLCHAIN_FILE
  "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
set(VCPKG_TARGET_TRIPLET "x64-windows-static" CACHE STRING "")
set(CMAKE_MSVC_RUNTIME_LIBRARY
  "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "")

if(NOT DEFINED F4VR_COMMON_FRAMEWORK_PATH)
  set(F4VR_COMMON_FRAMEWORK_PATH
    "${WORKSPACE_ROOT}/libraries_and_tools/F4VR-CommonFramework")
endif()
if(NOT EXISTS "${F4VR_COMMON_FRAMEWORK_PATH}/src/ModBase.h")
  message(FATAL_ERROR
    "F4VR-CommonFramework not found at '${F4VR_COMMON_FRAMEWORK_PATH}'")
endif()

if(NOT DEFINED COMMON_LIB_F4VR_PATH)
  set(COMMON_LIB_F4VR_PATH
    "${WORKSPACE_ROOT}/libraries_and_tools/CommonLibF4VR")
endif()
if(NOT EXISTS
   "${COMMON_LIB_F4VR_PATH}/CommonLibF4/include/REL/Relocation.h")
  message(FATAL_ERROR
    "CommonLibF4VR not found at '${COMMON_LIB_F4VR_PATH}'")
endif()
set(COMMON_LIB_F4VR_PATH
  "${COMMON_LIB_F4VR_PATH}" CACHE PATH "" FORCE)

if(NOT DEFINED ROCK_SDK_PATH)
  set(ROCK_SDK_PATH
    "${WORKSPACE_ROOT}/main_projects/RPS_SDK/SDK/ROCK/include")
endif()
if(NOT EXISTS "${ROCK_SDK_PATH}/ROCKProviderApi.h")
  message(FATAL_ERROR "ROCK SDK not found at '${ROCK_SDK_PATH}'")
endif()

project(${NAME} VERSION ${VERSION} LANGUAGES CXX)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_DEBUG OFF)
add_compile_definitions(_CRT_SECURE_NO_WARNINGS)

add_subdirectory(
  "${F4VR_COMMON_FRAMEWORK_PATH}"
  "F4VR-CommonFramework"
  EXCLUDE_FROM_ALL)
find_package(directxtk CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

file(GLOB_RECURSE headers CONFIGURE_DEPENDS
  "${SOURCE_DIR}/*.h"
  "${SDK_INCLUDE_DIR}/*.h")
file(GLOB_RECURSE sources CONFIGURE_DEPENDS
  "${SOURCE_DIR}/*.cpp")
source_group(TREE "${ROOT_DIR}" FILES ${headers} ${sources})

add_library(${PROJECT_NAME} SHARED
  ${headers}
  ${sources}
  src/exports.def)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
target_compile_definitions(${PROJECT_NAME} PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX NOMMNOSOUND)
target_include_directories(${PROJECT_NAME} PRIVATE
  "${SOURCE_DIR}"
  "${SDK_INCLUDE_DIR}"
  "${ROCK_SDK_PATH}")
target_link_libraries(${PROJECT_NAME} PRIVATE
  F4VRCommon::framework
  Microsoft::DirectXTK
  spdlog::spdlog
  d3d11.lib dxgi.lib d3dcompiler.lib shell32.lib version.lib bcrypt.lib)
target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)
target_compile_options(${PROJECT_NAME} PRIVATE
  /sdl /utf-8 /Zi /permissive- /W4 /wd4715
  /Zc:alignedNew /Zc:auto /Zc:char8_t /Zc:__cplusplus /Zc:externC
  /Zc:externConstexpr /Zc:forScope /Zc:hiddenFriend /Zc:implicitNoexcept
  /Zc:lambda /Zc:noexceptTypes /Zc:preprocessor /Zc:referenceBinding
  /Zc:rvalueCast /Zc:sizedDealloc /Zc:strictStrings /Zc:ternary
  /Zc:threadSafeInit /Zc:tlsGuards /Zc:trigraphs /Zc:wchar_t
  /external:anglebrackets /external:W0
  "$<$<CONFIG:RELEASE>:/Zc:inline;/JMC-;/Ob3>")
target_link_options(${PROJECT_NAME} PRIVATE
  /DEF:${SOURCE_DIR}/exports.def
  "$<$<CONFIG:RELEASE>:/INCREMENTAL:NO;/OPT:REF;/OPT:ICF;/DEBUG:FULL>")
string(REPLACE "/Ob2" "/Ob3"
  CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")

option(POST_BUILD_COPY_PLUGIN
  "Deploy the framework DLL and PDB to the configured mod root." OFF)
set(COPY_PLUGIN_BASE_PATH "" CACHE STRING "Semicolon-separated mod roots")
if(POST_BUILD_COPY_PLUGIN)
  if(COPY_PLUGIN_BASE_PATH STREQUAL "")
    message(FATAL_ERROR
      "COPY_PLUGIN_BASE_PATH is required when deployment is enabled")
  endif()
  foreach(copy_path IN LISTS COPY_PLUGIN_BASE_PATH)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory
        "${copy_path}/F4SE/Plugins"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:${PROJECT_NAME}>"
        "${copy_path}/F4SE/Plugins/"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_PDB_FILE:${PROJECT_NAME}>"
        "${copy_path}/F4SE/Plugins/"
      COMMENT "Deploying RPS UI Framework to ${copy_path}")
  endforeach()
endif()

option(RPS_UI_FRAMEWORK_BUILD_TESTS
  "Build deterministic framework contract tests." ON)
if(RPS_UI_FRAMEWORK_BUILD_TESTS)
  enable_testing()
  add_executable(RPSUIPanelSeparationTests tests/PanelSeparationTests.cpp)
  target_compile_features(RPSUIPanelSeparationTests PRIVATE cxx_std_23)
  target_include_directories(RPSUIPanelSeparationTests PRIVATE "${SOURCE_DIR}" "${SDK_INCLUDE_DIR}")
  add_test(NAME RPSUIFramework.PanelSeparation COMMAND RPSUIPanelSeparationTests)

  add_executable(RPSUIFrameworkPolicyTests
    tests/FrameworkPolicyTests.cpp
    src/ContextualScrollPolicy.h
    src/PanelResizePolicy.h
    src/PointerClickGate.h
    src/PointerHandSelection.h
    src/PointerPanelIntersection.h
    src/render/EngineStereoSubmissionPolicy.h)
  target_compile_features(RPSUIFrameworkPolicyTests PRIVATE cxx_std_23)
  target_include_directories(RPSUIFrameworkPolicyTests PRIVATE
    "${SOURCE_DIR}"
    "${SDK_INCLUDE_DIR}")
  add_test(NAME RPSUIFramework.Policy
    COMMAND RPSUIFrameworkPolicyTests)

  find_program(PWSH_EXECUTABLE NAMES pwsh REQUIRED)
  add_test(NAME RPSUIFramework.SourceContracts
    COMMAND "${PWSH_EXECUTABLE}" -NoLogo -NoProfile
      -File "${ROOT_DIR}/tests/SourceContracts.ps1")
endif()
