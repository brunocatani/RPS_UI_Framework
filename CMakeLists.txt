cmake_minimum_required(VERSION 4.2)

set(NAME "RPS_UI_Framework")
set(FRIENDLY_NAME "RPS UI Framework")
set(VERSION 1.1.1)
set(ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(SOURCE_DIR "${ROOT_DIR}/src")
# RPS UI's own provider API: self-contained headers the DLL compiles against.
set(PUBLIC_INCLUDE_DIR "${ROOT_DIR}/include")
get_filename_component(WORKSPACE_ROOT "${ROOT_DIR}/../.." ABSOLUTE)
# RPS Framework's header-only required-update stop (RPS/Runtime/RequiredUpdate.h).
set(RPS_FRAMEWORK_INCLUDE_DIR "${WORKSPACE_ROOT}/PROJECT_ROCK_V2/my_frameworks_and_sdks/RPS_Framework/include"
  CACHE PATH "RPS Framework public include directory")
if(NOT EXISTS "${RPS_FRAMEWORK_INCLUDE_DIR}/RPS/Runtime/RequiredUpdate.h")
  message(FATAL_ERROR "RPS Framework headers not found in '${RPS_FRAMEWORK_INCLUDE_DIR}'; set RPS_FRAMEWORK_INCLUDE_DIR.")
endif()

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
  "${PUBLIC_INCLUDE_DIR}/RPS_UI_Framework/*.h")
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
  "${PUBLIC_INCLUDE_DIR}"
  "${RPS_FRAMEWORK_INCLUDE_DIR}")
target_link_libraries(${PROJECT_NAME} PRIVATE
  F4VRCommon::framework
  Microsoft::DirectXTK
  spdlog::spdlog
  d3d11.lib dxgi.lib d3dcompiler.lib shell32.lib version.lib bcrypt.lib gdi32.lib user32.lib)
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
  add_executable(RPSUIGraphicsStateCommitTests tests/GraphicsStateCommitTests.cpp)
  target_compile_features(RPSUIGraphicsStateCommitTests PRIVATE cxx_std_23)
  target_compile_definitions(RPSUIGraphicsStateCommitTests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(RPSUIGraphicsStateCommitTests PRIVATE "${SOURCE_DIR}")
  add_test(NAME RPSUIFramework.GraphicsStateCommit COMMAND RPSUIGraphicsStateCommitTests)
  # Exercise actual exported tables, registries and input subscriptions. The
  # test never installs native hooks; scene-depth demand is stubbed.
  add_executable(RPSUIRegistryTests tests/RegistryTests.cpp src/FrameworkRuntime.cpp src/Api.cpp src/WorldDrawStore.cpp src/RequiredUpdates.cpp)
  target_compile_features(RPSUIRegistryTests PRIVATE cxx_std_23)
  target_compile_definitions(RPSUIRegistryTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX NOMMNOSOUND)
  target_include_directories(RPSUIRegistryTests PRIVATE "${SOURCE_DIR}" "${PUBLIC_INCLUDE_DIR}" "${RPS_FRAMEWORK_INCLUDE_DIR}")
  target_link_libraries(RPSUIRegistryTests PRIVATE F4VRCommon::framework spdlog::spdlog)
  # No test calls OpenVR; defer its import so validation needs no game DLLs.
  target_link_libraries(RPSUIRegistryTests PRIVATE delayimp)
  target_link_options(RPSUIRegistryTests PRIVATE /DELAYLOAD:openvr_api.dll)
  target_precompile_headers(RPSUIRegistryTests PRIVATE src/PCH.h)
  add_test(NAME RPSUIFramework.Registry COMMAND RPSUIRegistryTests)
  add_executable(RPSUINumberOverlayTests tests/NumberOverlayTests.cpp)
  target_compile_features(RPSUINumberOverlayTests PRIVATE cxx_std_23)
  target_compile_options(RPSUINumberOverlayTests PRIVATE /UNDEBUG)
  target_include_directories(RPSUINumberOverlayTests PRIVATE "${SOURCE_DIR}" "${PUBLIC_INCLUDE_DIR}")
  add_test(NAME RPSUIFramework.NumberOverlay COMMAND RPSUINumberOverlayTests)
  add_executable(RPSUIWorldDrawTests tests/WorldDrawTests.cpp src/WorldDrawStore.cpp)
  target_compile_features(RPSUIWorldDrawTests PRIVATE cxx_std_23)
  target_compile_options(RPSUIWorldDrawTests PRIVATE /UNDEBUG)
  target_include_directories(RPSUIWorldDrawTests PRIVATE "${SOURCE_DIR}" "${PUBLIC_INCLUDE_DIR}")
  add_test(NAME RPSUIFramework.WorldDraw COMMAND RPSUIWorldDrawTests)
  add_executable(RPSUIPanelCooperationTests tests/PanelCooperationTests.cpp)
  target_compile_features(RPSUIPanelCooperationTests PRIVATE cxx_std_23)
  target_compile_definitions(RPSUIPanelCooperationTests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(RPSUIPanelCooperationTests PRIVATE "${SOURCE_DIR}" "${PUBLIC_INCLUDE_DIR}")
  add_test(NAME RPSUIFramework.PanelCooperation COMMAND RPSUIPanelCooperationTests)
  add_executable(RPSUIWorldPanelDepthTests tests/WorldPanelDepthTests.cpp)
  target_compile_features(RPSUIWorldPanelDepthTests PRIVATE cxx_std_23)
  target_compile_definitions(RPSUIWorldPanelDepthTests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(RPSUIWorldPanelDepthTests PRIVATE "${SOURCE_DIR}")
  target_link_libraries(RPSUIWorldPanelDepthTests PRIVATE d3d11.lib d3dcompiler.lib)
  add_test(NAME RPSUIFramework.WorldPanelDepth COMMAND RPSUIWorldPanelDepthTests)
  add_executable(RPSUINativeSceneTargetsTests tests/NativeSceneTargetsTests.cpp)
  target_compile_features(RPSUINativeSceneTargetsTests PRIVATE cxx_std_23)
  target_include_directories(RPSUINativeSceneTargetsTests PRIVATE "${SOURCE_DIR}")
  add_test(NAME RPSUIFramework.NativeSceneTargets COMMAND RPSUINativeSceneTargetsTests)
  add_executable(RPSUIPanelSeparationTests tests/PanelSeparationTests.cpp)
  target_compile_features(RPSUIPanelSeparationTests PRIVATE cxx_std_23)
  target_include_directories(RPSUIPanelSeparationTests PRIVATE "${SOURCE_DIR}" "${PUBLIC_INCLUDE_DIR}")
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
    "${PUBLIC_INCLUDE_DIR}")
  add_test(NAME RPSUIFramework.Policy
    COMMAND RPSUIFrameworkPolicyTests)

  # Every public header compiled on its own under a hostile packing, plus the
  # minimal consumer. The headers' static_asserts are the ABI layout checks.
  file(GLOB RPSUI_PUBLIC_HEADERS CONFIGURE_DEPENDS "${PUBLIC_INCLUDE_DIR}/RPS_UI_Framework/*.h")
  set(RPSUI_PUBLIC_HEADER_SOURCES)
  foreach(RPSUI_PUBLIC_HEADER IN LISTS RPSUI_PUBLIC_HEADERS)
    get_filename_component(RPSUI_PUBLIC_HEADER_NAME "${RPSUI_PUBLIC_HEADER}" NAME_WE)
    set(RPSUI_PUBLIC_HEADER_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/public_header_tests/${RPSUI_PUBLIC_HEADER_NAME}.cpp")
    file(CONFIGURE OUTPUT "${RPSUI_PUBLIC_HEADER_SOURCE}"
      CONTENT "#pragma pack(push, 1)\n#include <RPS_UI_Framework/${RPSUI_PUBLIC_HEADER_NAME}.h>\n#pragma pack(pop)\n")
    list(APPEND RPSUI_PUBLIC_HEADER_SOURCES "${RPSUI_PUBLIC_HEADER_SOURCE}")
  endforeach()
  add_executable(RPSUIPublicHeaderTests ${RPSUI_PUBLIC_HEADER_SOURCES} tests/ConsumerExample.cpp)
  set_target_properties(RPSUIPublicHeaderTests PROPERTIES INCLUDE_DIRECTORIES "${PUBLIC_INCLUDE_DIR}")
  target_compile_features(RPSUIPublicHeaderTests PRIVATE cxx_std_20)
  target_compile_options(RPSUIPublicHeaderTests PRIVATE /W4 /permissive-)
  add_test(NAME RPSUIFramework.PublicHeaders COMMAND RPSUIPublicHeaderTests)
  add_executable(RPSUIDiscoveryTests tests/DiscoveryTests.cpp)
  target_compile_features(RPSUIDiscoveryTests PRIVATE cxx_std_23)
  target_compile_options(RPSUIDiscoveryTests PRIVATE /UNDEBUG)
  target_include_directories(RPSUIDiscoveryTests PRIVATE "${SOURCE_DIR}" "${PUBLIC_INCLUDE_DIR}")
  add_test(NAME RPSUIFramework.Discovery COMMAND RPSUIDiscoveryTests)

  find_program(PWSH_EXECUTABLE NAMES pwsh REQUIRED)
  add_test(NAME RPSUIFramework.SourceContracts
    COMMAND "${PWSH_EXECUTABLE}" -NoLogo -NoProfile
      -File "${ROOT_DIR}/tests/SourceContracts.ps1")
endif()

# Tests share build-fast but stay out of the default build, so building and deploying the
# plugin never compiles them. Build them with `--target build_tests`, then run ctest.
function(fo4dev_collect_targets directory result)
  get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
  get_property(children DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
  foreach(child IN LISTS children)
    cmake_path(IS_PREFIX CMAKE_SOURCE_DIR "${child}" NORMALIZE child_is_ours)
    if(child_is_ours)
      fo4dev_collect_targets("${child}" child_targets)
      list(APPEND targets ${child_targets})
    endif()
  endforeach()
  set(${result} ${targets} PARENT_SCOPE)
endfunction()
fo4dev_collect_targets("${CMAKE_SOURCE_DIR}" fo4dev_targets)
add_custom_target(build_tests)
foreach(fo4dev_target IN LISTS fo4dev_targets)
  get_target_property(fo4dev_target_type ${fo4dev_target} TYPE)
  string(TOLOWER "${fo4dev_target}" fo4dev_target_name)
  if(fo4dev_target_name MATCHES "test" AND fo4dev_target_type MATCHES "^(EXECUTABLE|STATIC_LIBRARY|OBJECT_LIBRARY)$")
    set_target_properties(${fo4dev_target} PROPERTIES EXCLUDE_FROM_ALL TRUE)
    if(fo4dev_target_type STREQUAL "EXECUTABLE")
      add_dependencies(build_tests ${fo4dev_target})
    endif()
  endif()
endforeach()
