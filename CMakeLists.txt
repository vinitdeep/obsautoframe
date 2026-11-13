cmake_minimum_required(VERSION 3.24)

project(
  vmedia-ai-autoframe
  VERSION 1.1.1
  DESCRIPTION "V Media AI AutoFrame - AI auto-framing filter for OBS Studio"
  HOMEPAGE_URL "https://vmediaproduction.com"
  LANGUAGES C CXX)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "V Media AI AutoFrame targets Windows x64 only.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(VMAF_BUILD_PLUGIN "Build the OBS plugin (needs the OBS SDK from scripts/setup.ps1)" ON)
option(VMAF_BUILD_TESTS "Build unit tests (no OBS needed)" ON)
option(VMAF_BUILD_TOOLS "Build the offline video harness and the libobs integration test" ON)

set(VMAF_THIRD_PARTY "${CMAKE_SOURCE_DIR}/third_party" CACHE PATH "Dependencies prepared by scripts/setup.ps1")
set(VMAF_ORT_VERSION "1.24.4" CACHE STRING "ONNX Runtime (DirectML) version")
set(VMAF_DML_VERSION "1.15.4" CACHE STRING "DirectML version")
set(OBS_SDK_DIR "${VMAF_THIRD_PARTY}/obs-sdk" CACHE PATH "OBS headers + import libraries")
set(ORT_DIR "${VMAF_THIRD_PARTY}/ort-${VMAF_ORT_VERSION}")
set(DML_DIR "${VMAF_THIRD_PARTY}/directml-${VMAF_DML_VERSION}")
set(VMAF_MODELS_DIR "${VMAF_THIRD_PARTY}/models")

if(NOT EXISTS "${ORT_DIR}/build/native/include/onnxruntime_cxx_api.h")
  message(FATAL_ERROR "ONNX Runtime not found in ${ORT_DIR}. Run scripts/setup.ps1 first.")
endif()

if(MSVC)
  add_compile_options(/W3 /utf-8 /permissive- /Zc:__cplusplus /MP)
  # Release builds keep symbols (separate .pdb) so crash reports are readable
  add_compile_options($<$<CONFIG:Release>:/Zi>)
  add_link_options($<$<CONFIG:Release>:/DEBUG> $<$<CONFIG:Release>:/OPT:REF> $<$<CONFIG:Release>:/OPT:ICF>)
endif()
add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)

# --------------------------------------------------------------------------- core (pure C++)
add_library(
  vmaf_core STATIC
  src/settings.cpp
  src/person-tracker.cpp
  src/zoom-controller.cpp
  src/framing-controller.cpp
  src/yolox.cpp
  src/performance-monitor.cpp)
target_include_directories(vmaf_core PUBLIC src)

# --------------------------------------------------------------------------- detector (ONNX Runtime, loaded at run time)
add_library(vmaf_detector STATIC src/person-detector.cpp src/detection-worker.cpp)
target_include_directories(vmaf_detector PUBLIC "${ORT_DIR}/build/native/include")
# onnxruntime.lib is never linked: the runtime is loaded on demand from the plugin's own
# folder, so a missing or broken runtime cannot prevent OBS (or the filter) from loading.
target_compile_definitions(vmaf_detector PUBLIC ORT_API_MANUAL_INIT)
target_link_libraries(vmaf_detector PUBLIC vmaf_core bcrypt)

set(VMAF_RUNTIME_FILES "${ORT_DIR}/runtimes/win-x64/native/onnxruntime.dll" "${DML_DIR}/bin/x64-win/DirectML.dll")
set(VMAF_MODEL_FILES "${VMAF_MODELS_DIR}/yolox_nano.onnx" "${VMAF_MODELS_DIR}/yolox_tiny.onnx")

# --------------------------------------------------------------------------- OBS plugin
if(VMAF_BUILD_PLUGIN)
  if(NOT EXISTS "${OBS_SDK_DIR}/include/obs/obs-module.h" OR NOT EXISTS "${OBS_SDK_DIR}/lib/obs.lib")
    message(FATAL_ERROR "OBS SDK not found in ${OBS_SDK_DIR}. Run scripts/setup.ps1 first.")
  endif()
  # Plugin API = headers of the OLDEST supported OBS (libobs rejects plugins built against
  # newer headers). Import libraries resolve by symbol name, so they come from any newer OBS.
  add_library(obs_sdk INTERFACE)
  target_include_directories(obs_sdk INTERFACE "${OBS_SDK_DIR}/include/obs")
  target_link_libraries(obs_sdk INTERFACE "${OBS_SDK_DIR}/lib/obs.lib" "${OBS_SDK_DIR}/lib/w32-pthreads.lib")
  # Integration-test host = headers of the OBS version it actually boots
  add_library(obs_sdk_host INTERFACE)
  target_include_directories(obs_sdk_host INTERFACE "${OBS_SDK_DIR}/include-host/obs")
  target_link_libraries(obs_sdk_host INTERFACE "${OBS_SDK_DIR}/lib/obs.lib" "${OBS_SDK_DIR}/lib/w32-pthreads.lib")

  configure_file(src/version.rc.in "${CMAKE_BINARY_DIR}/version.rc" @ONLY)
  add_library(
    vmedia-ai-autoframe MODULE
    src/plugin-main.cpp
    src/autoframe-filter.cpp
    src/autoframe-filter.hpp
    src/obs-settings.cpp
    src/obs-renderer.cpp
    "${CMAKE_BINARY_DIR}/version.rc")
  target_link_libraries(vmedia-ai-autoframe PRIVATE vmaf_detector obs_sdk)
  target_compile_definitions(vmedia-ai-autoframe PRIVATE PLUGIN_VERSION="${PROJECT_VERSION}")
  set_target_properties(vmedia-ai-autoframe PROPERTIES PREFIX "" OUTPUT_NAME "vmedia-ai-autoframe")

  # Stage a ready-to-copy plugin folder next to the build:
  #   <build>/stage/vmedia-ai-autoframe/bin/64bit/*.dll
  #   <build>/stage/vmedia-ai-autoframe/data/{effects,locale,models,runtime,licenses}
  set(VMAF_STAGE "${CMAKE_BINARY_DIR}/stage/vmedia-ai-autoframe")
  add_custom_command(
    TARGET vmedia-ai-autoframe
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${VMAF_STAGE}/bin/64bit" "${VMAF_STAGE}/data/models"
            "${VMAF_STAGE}/data/runtime" "${VMAF_STAGE}/data/licenses"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:vmedia-ai-autoframe>" "${VMAF_STAGE}/bin/64bit/"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_PDB_FILE:vmedia-ai-autoframe>" "${VMAF_STAGE}/bin/64bit/"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_SOURCE_DIR}/data" "${VMAF_STAGE}/data"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${VMAF_MODEL_FILES} "${VMAF_STAGE}/data/models/"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${VMAF_RUNTIME_FILES} "${VMAF_STAGE}/data/runtime/"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_SOURCE_DIR}/LICENSE" "${VMAF_STAGE}/data/licenses/LICENSE.txt"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
            "${VMAF_STAGE}/data/licenses/THIRD_PARTY_NOTICES.md"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${ORT_DIR}/LICENSE" "${VMAF_STAGE}/data/licenses/onnxruntime-LICENSE.txt"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${ORT_DIR}/ThirdPartyNotices.txt"
            "${VMAF_STAGE}/data/licenses/onnxruntime-ThirdPartyNotices.txt"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${DML_DIR}/LICENSE.txt" "${VMAF_STAGE}/data/licenses/DirectML-LICENSE.txt"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${DML_DIR}/ThirdPartyNotices.txt"
            "${VMAF_STAGE}/data/licenses/DirectML-ThirdPartyNotices.txt"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${VMAF_MODELS_DIR}/YOLOX-LICENSE.txt"
            "${VMAF_STAGE}/data/licenses/YOLOX-LICENSE.txt"
    COMMENT "Staging plugin folder in ${VMAF_STAGE}"
    VERBATIM)

  install(DIRECTORY "${VMAF_STAGE}/" DESTINATION "vmedia-ai-autoframe")
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

  # Detector test: real ONNX Runtime + real model on a real photo (no OBS needed)
  add_executable(vmaf-detector-test tests/detector-test.cpp)
  target_link_libraries(vmaf-detector-test PRIVATE vmaf_detector)
  target_compile_definitions(
    vmaf-detector-test PRIVATE VMAF_RUNTIME_DIR="${CMAKE_BINARY_DIR}/test-runtime" VMAF_MODELS_DIR="${VMAF_MODELS_DIR}"
                               VMAF_TESTDATA_DIR="${CMAKE_SOURCE_DIR}/tests/data")
  add_custom_command(
    TARGET vmaf-detector-test
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/test-runtime"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${VMAF_RUNTIME_FILES} "${CMAKE_BINARY_DIR}/test-runtime/"
    VERBATIM)
  add_test(NAME detector-test COMMAND vmaf-detector-test)
endif()

# --------------------------------------------------------------------------- tools
if(VMAF_BUILD_TOOLS)
  # Offline harness: runs detector + tracker + framing on a video file through ffmpeg pipes
  add_executable(autoframe-sim tools/autoframe-sim.cpp)
  target_link_libraries(autoframe-sim PRIVATE vmaf_detector)
  target_compile_definitions(autoframe-sim PRIVATE VMAF_RUNTIME_DIR="${CMAKE_BINARY_DIR}/test-runtime"
                                                   VMAF_MODELS_DIR="${VMAF_MODELS_DIR}")
  add_custom_command(
    TARGET autoframe-sim
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/test-runtime"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${VMAF_RUNTIME_FILES} "${CMAKE_BINARY_DIR}/test-runtime/"
    VERBATIM)

  if(VMAF_BUILD_PLUGIN)
    # Integration test: boots real libobs (D3D11), loads the plugin and renders a video through it
    add_executable(obs-headless-test tools/obs-headless-test.cpp)
    target_link_libraries(obs-headless-test PRIVATE obs_sdk_host vmaf_core delayimp dbghelp)
    # obs.dll is delay-loaded so the test can point the DLL search path at the OBS install first
    target_link_options(obs-headless-test PRIVATE /DELAYLOAD:obs.dll /DELAYLOAD:w32-pthreads.dll)
  endif()
endif()
