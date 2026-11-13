cmake_minimum_required(VERSION 3.24)

project(
  vmedia-ai-autoframe
  VERSION 1.1.2
  DESCRIPTION "V Media AI AutoFrame - AI auto-framing filter for OBS Studio"
  HOMEPAGE_URL "https://vmediaproduction.com"
  LANGUAGES C CXX)

if(WIN32)
  if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "V Media AI AutoFrame on Windows targets x64 only.")
  endif()
elseif(NOT APPLE)
  message(FATAL_ERROR "V Media AI AutoFrame supports Windows x64 and macOS.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(VMAF_BUILD_PLUGIN "Build the OBS plugin (needs the OBS SDK from the setup script)" ON)
option(VMAF_BUILD_TESTS "Build unit tests (no OBS needed)" ON)
option(VMAF_BUILD_TOOLS "Build the offline video harness (and on Windows the libobs integration test)" ON)

set(VMAF_THIRD_PARTY "${CMAKE_SOURCE_DIR}/third_party" CACHE PATH "Dependencies prepared by the setup script")
set(OBS_SDK_DIR "${VMAF_THIRD_PARTY}/obs-sdk" CACHE PATH "OBS headers (+ import libraries on Windows)")
set(VMAF_MODELS_DIR "${VMAF_THIRD_PARTY}/models")
set(VMAF_MODEL_FILES "${VMAF_MODELS_DIR}/yolox_nano.onnx" "${VMAF_MODELS_DIR}/yolox_tiny.onnx")

if(WIN32)
  # ONNX Runtime DirectML build (CPU + any DirectX 12 GPU) + DirectML redistributable
  set(VMAF_ORT_VERSION "1.24.4" CACHE STRING "ONNX Runtime (DirectML) version")
  set(VMAF_DML_VERSION "1.15.4" CACHE STRING "DirectML version")
  set(ORT_DIR "${VMAF_THIRD_PARTY}/ort-${VMAF_ORT_VERSION}")
  set(DML_DIR "${VMAF_THIRD_PARTY}/directml-${VMAF_DML_VERSION}")
  set(ORT_INCLUDE_DIR "${ORT_DIR}/build/native/include")
  set(ORT_LICENSE "${ORT_DIR}/LICENSE")
  set(ORT_NOTICES "${ORT_DIR}/ThirdPartyNotices.txt")
  # [source file, name in the runtime folder] pairs
  set(VMAF_RUNTIME_SRC "${ORT_DIR}/runtimes/win-x64/native/onnxruntime.dll" "${DML_DIR}/bin/x64-win/DirectML.dll")
  set(VMAF_RUNTIME_DST onnxruntime.dll DirectML.dll)
else()
  # ONNX Runtime universal2 build (arm64 + x86_64; CPU + Core ML)
  set(VMAF_ORT_VERSION "1.23.2" CACHE STRING "ONNX Runtime (macOS universal2) version")
  set(ORT_DIR "${VMAF_THIRD_PARTY}/onnxruntime-osx-universal2-${VMAF_ORT_VERSION}")
  set(ORT_INCLUDE_DIR "${ORT_DIR}/include")
  set(ORT_LICENSE "${ORT_DIR}/LICENSE")
  set(ORT_NOTICES "${ORT_DIR}/ThirdPartyNotices.txt")
  set(VMAF_RUNTIME_SRC "${ORT_DIR}/lib/libonnxruntime.${VMAF_ORT_VERSION}.dylib")
  set(VMAF_RUNTIME_DST libonnxruntime.dylib)
  set(OBS_FRAMEWORK_DIR "" CACHE PATH "Folder containing libobs.framework (from OBS.app, same CPU architecture)")
endif()

if(NOT EXISTS "${ORT_INCLUDE_DIR}/onnxruntime_cxx_api.h")
  message(FATAL_ERROR "ONNX Runtime not found in ${ORT_DIR}. Run the setup script first.")
endif()

if(MSVC)
  add_compile_options(/W3 /utf-8 /permissive- /Zc:__cplusplus /MP)
  # Release builds keep symbols (separate .pdb) so crash reports are readable
  add_compile_options($<$<CONFIG:Release>:/Zi>)
  add_link_options($<$<CONFIG:Release>:/DEBUG> $<$<CONFIG:Release>:/OPT:REF> $<$<CONFIG:Release>:/OPT:ICF>)
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
else()
  add_compile_options(-Wall -Wno-unused-parameter -Wno-missing-field-initializers)
endif()

# Copies the AI runtime into <dir> under its runtime-folder names (used by the plugin and tests)
function(vmaf_copy_runtime target dir)
  list(LENGTH VMAF_RUNTIME_SRC n)
  math(EXPR last "${n} - 1")
  add_custom_command(TARGET ${target} POST_BUILD COMMAND "${CMAKE_COMMAND}" -E make_directory "${dir}" VERBATIM)
  foreach(i RANGE ${last})
    list(GET VMAF_RUNTIME_SRC ${i} src)
    list(GET VMAF_RUNTIME_DST ${i} dst)
    add_custom_command(TARGET ${target} POST_BUILD COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${src}" "${dir}/${dst}"
                       VERBATIM)
  endforeach()
endfunction()

# --------------------------------------------------------------------------- core (pure C++)
add_library(
  vmaf_core STATIC
  src/settings.cpp
  src/person-tracker.cpp
  src/zoom-controller.cpp
  src/framing-controller.cpp
  src/yolox.cpp
  src/sha256.cpp
  src/performance-monitor.cpp)
target_include_directories(vmaf_core PUBLIC src)
set_target_properties(vmaf_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# --------------------------------------------------------------------------- detector (ONNX Runtime, loaded at run time)
add_library(vmaf_detector STATIC src/person-detector.cpp src/detection-worker.cpp)
target_include_directories(vmaf_detector PUBLIC "${ORT_INCLUDE_DIR}")
# The ONNX Runtime library is never linked: it is loaded on demand from the plugin's own folder,
# so a missing or broken runtime cannot prevent OBS (or the filter) from loading.
target_compile_definitions(vmaf_detector PUBLIC ORT_API_MANUAL_INIT)
target_link_libraries(vmaf_detector PUBLIC vmaf_core)
set_target_properties(vmaf_detector PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(NOT WIN32)
  target_link_libraries(vmaf_detector PUBLIC ${CMAKE_DL_LIBS})
endif()

# --------------------------------------------------------------------------- OBS plugin
if(VMAF_BUILD_PLUGIN)
  if(NOT EXISTS "${OBS_SDK_DIR}/include/obs/obs-module.h")
    message(FATAL_ERROR "OBS SDK headers not found in ${OBS_SDK_DIR}. Run the setup script first.")
  endif()
  # Plugin API = headers of the OLDEST supported OBS (libobs rejects plugins built against newer
  # headers). The binary interface comes from a newer OBS (import libs / framework), resolved by name.
  add_library(obs_sdk INTERFACE)
  target_include_directories(obs_sdk INTERFACE "${OBS_SDK_DIR}/include/obs")
  if(WIN32)
    target_link_libraries(obs_sdk INTERFACE "${OBS_SDK_DIR}/lib/obs.lib" "${OBS_SDK_DIR}/lib/w32-pthreads.lib")
    # Integration-test host = headers of the OBS version it actually boots
    add_library(obs_sdk_host INTERFACE)
    target_include_directories(obs_sdk_host INTERFACE "${OBS_SDK_DIR}/include-host/obs")
    target_link_libraries(obs_sdk_host INTERFACE "${OBS_SDK_DIR}/lib/obs.lib" "${OBS_SDK_DIR}/lib/w32-pthreads.lib")
  else()
    if(NOT EXISTS "${OBS_FRAMEWORK_DIR}/libobs.framework")
      message(FATAL_ERROR "libobs.framework not found in OBS_FRAMEWORK_DIR='${OBS_FRAMEWORK_DIR}'.")
    endif()
    target_link_options(obs_sdk INTERFACE "-F${OBS_FRAMEWORK_DIR}" "-framework" "libobs")
  endif()

  set(VMAF_PLUGIN_SOURCES src/plugin-main.cpp src/autoframe-filter.cpp src/autoframe-filter.hpp src/obs-settings.cpp
                          src/obs-renderer.cpp)
  if(WIN32)
    configure_file(src/version.rc.in "${CMAKE_BINARY_DIR}/version.rc" @ONLY)
    list(APPEND VMAF_PLUGIN_SOURCES "${CMAKE_BINARY_DIR}/version.rc")
  endif()
  add_library(vmedia-ai-autoframe MODULE ${VMAF_PLUGIN_SOURCES})
  target_link_libraries(vmedia-ai-autoframe PRIVATE vmaf_detector obs_sdk)
  target_compile_definitions(vmedia-ai-autoframe PRIVATE PLUGIN_VERSION="${PROJECT_VERSION}")
  set_target_properties(vmedia-ai-autoframe PROPERTIES PREFIX "" OUTPUT_NAME "vmedia-ai-autoframe")

  if(WIN32)
    # Ready-to-copy plugin folder:
    #   <build>/stage/vmedia-ai-autoframe/bin/64bit/*.dll
    #   <build>/stage/vmedia-ai-autoframe/data/{effects,locale,models,runtime,licenses}
    set(VMAF_STAGE "${CMAKE_BINARY_DIR}/stage/vmedia-ai-autoframe")
    set(VMAF_DATA "${VMAF_STAGE}/data")
    add_custom_command(
      TARGET vmedia-ai-autoframe
      POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${VMAF_STAGE}/bin/64bit"
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:vmedia-ai-autoframe>" "${VMAF_STAGE}/bin/64bit/"
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_PDB_FILE:vmedia-ai-autoframe>" "${VMAF_STAGE}/bin/64bit/"
      VERBATIM)
  else()
    # macOS: vmedia-ai-autoframe.plugin/Contents/{MacOS/vmedia-ai-autoframe, Resources/<data>}
    configure_file(cmake/macos/Info.plist.in "${CMAKE_BINARY_DIR}/Info.plist" @ONLY)
    set_target_properties(
      vmedia-ai-autoframe
      PROPERTIES BUNDLE TRUE
                 BUNDLE_EXTENSION plugin
                 MACOSX_BUNDLE_INFO_PLIST "${CMAKE_BINARY_DIR}/Info.plist"
                 BUILD_WITH_INSTALL_RPATH TRUE
                 INSTALL_RPATH "@executable_path/../Frameworks;@loader_path/../Frameworks")
    set(VMAF_DATA "$<TARGET_BUNDLE_CONTENT_DIR:vmedia-ai-autoframe>/Resources")
  endif()

  add_custom_command(
    TARGET vmedia-ai-autoframe
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${VMAF_DATA}/models" "${VMAF_DATA}/licenses"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_SOURCE_DIR}/data" "${VMAF_DATA}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${VMAF_MODEL_FILES} "${VMAF_DATA}/models/"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_SOURCE_DIR}/LICENSE" "${VMAF_DATA}/licenses/LICENSE.txt"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
            "${VMAF_DATA}/licenses/THIRD_PARTY_NOTICES.md"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${ORT_LICENSE}" "${VMAF_DATA}/licenses/onnxruntime-LICENSE.txt"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${ORT_NOTICES}" "${VMAF_DATA}/licenses/onnxruntime-ThirdPartyNotices.txt"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${VMAF_MODELS_DIR}/YOLOX-LICENSE.txt"
            "${VMAF_DATA}/licenses/YOLOX-LICENSE.txt"
    VERBATIM)
  if(WIN32)
    add_custom_command(
      TARGET vmedia-ai-autoframe
      POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${DML_DIR}/LICENSE.txt" "${VMAF_DATA}/licenses/DirectML-LICENSE.txt"
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${DML_DIR}/ThirdPartyNotices.txt"
              "${VMAF_DATA}/licenses/DirectML-ThirdPartyNotices.txt"
      VERBATIM)
  endif()
  vmaf_copy_runtime(vmedia-ai-autoframe "${VMAF_DATA}/runtime")
endif()

# --------------------------------------------------------------------------- tests
if(VMAF_BUILD_TESTS)
  enable_testing()
  add_executable(
    vmaf-tests
    tests/test-main.cpp
    tests/geometry-tests.cpp
    tests/tracking-tests.cpp
    tests/framing-tests.cpp
    tests/zoom-tests.cpp
    tests/recovery-tests.cpp
    tests/yolox-tests.cpp)
  target_link_libraries(vmaf-tests PRIVATE vmaf_core)
  add_test(NAME unit-tests COMMAND vmaf-tests)

  # Detector test: real ONNX Runtime + real model on real footage (no OBS needed)
  add_executable(vmaf-detector-test tests/detector-test.cpp)
  target_link_libraries(vmaf-detector-test PRIVATE vmaf_detector)
  target_compile_definitions(
    vmaf-detector-test PRIVATE VMAF_RUNTIME_DIR="${CMAKE_BINARY_DIR}/test-runtime" VMAF_MODELS_DIR="${VMAF_MODELS_DIR}"
                               VMAF_TESTDATA_DIR="${CMAKE_SOURCE_DIR}/tests/data")
  vmaf_copy_runtime(vmaf-detector-test "${CMAKE_BINARY_DIR}/test-runtime")
  add_test(NAME detector-test COMMAND vmaf-detector-test)
endif()

# --------------------------------------------------------------------------- tools
if(VMAF_BUILD_TOOLS)
  # Offline harness: runs detector + tracker + framing on a video file through ffmpeg pipes
  add_executable(autoframe-sim tools/autoframe-sim.cpp)
  target_link_libraries(autoframe-sim PRIVATE vmaf_detector)
  target_compile_definitions(autoframe-sim PRIVATE VMAF_RUNTIME_DIR="${CMAKE_BINARY_DIR}/test-runtime"
                                                   VMAF_MODELS_DIR="${VMAF_MODELS_DIR}")
  vmaf_copy_runtime(autoframe-sim "${CMAKE_BINARY_DIR}/test-runtime")

  if(VMAF_BUILD_PLUGIN AND WIN32)
    # Integration test: boots real libobs (D3D11), loads the plugin and renders a video through it
    add_executable(obs-headless-test tools/obs-headless-test.cpp)
    target_link_libraries(obs-headless-test PRIVATE obs_sdk_host vmaf_core delayimp dbghelp)
    # obs.dll is delay-loaded so the test can point the DLL search path at the OBS install first
    target_link_options(obs-headless-test PRIVATE /DELAYLOAD:obs.dll /DELAYLOAD:w32-pthreads.dll)
  endif()

  if(VMAF_BUILD_PLUGIN AND APPLE)
    # Load test: boots the real libobs from an OBS.app and loads the plugin bundle (package-macos.sh)
    add_executable(macos-load-check tools/macos-load-check.c)
  endif()
endif()
