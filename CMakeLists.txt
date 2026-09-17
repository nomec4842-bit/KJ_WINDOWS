cmake_minimum_required(VERSION 3.20)
project(KJ VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
    add_compile_definitions(
        NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE
        _CRT_SECURE_NO_WARNINGS
        SMTG_OS_WINDOWS=1 SMTG_PLATFORM_WINDOWS=1
    )
endif()

# Keep build products for different architectures separated to avoid mixing x86 and
# x64 artifacts when reusing a build directory.
if (MSVC)
    # Prefer the generator platform when available to avoid mixing x86 and x64
    # artifacts in the same build tree. Reusing a build directory across
    # architectures can leave behind incompatible libraries (for example,
    # base.lib), which later manifest as "invalid or corrupt file" linker
    # errors on Windows.
    if (CMAKE_GENERATOR_PLATFORM)
        set(_kj_output_platform "${CMAKE_GENERATOR_PLATFORM}")
    else()
        set(_kj_output_platform "${CMAKE_VS_PLATFORM_NAME}")
    endif()

    if (KJ_MSVC_PLATFORM_CACHE AND NOT KJ_MSVC_PLATFORM_CACHE STREQUAL _kj_output_platform)
        message(FATAL_ERROR "Build directory was previously configured for platform '${KJ_MSVC_PLATFORM_CACHE}' but is now configured for '${_kj_output_platform}'. Please use a fresh build directory per architecture to avoid stale artifacts.")
    endif()

    set(KJ_MSVC_PLATFORM_CACHE "${_kj_output_platform}" CACHE STRING "Last configured MSVC platform" FORCE)
else()
    set(_kj_output_platform "${CMAKE_SYSTEM_PROCESSOR}")
endif()

foreach(_config IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${_config}" _config_upper)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${_config_upper} "${CMAKE_BINARY_DIR}/bin/${_kj_output_platform}/${_config}")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${_config_upper} "${CMAKE_BINARY_DIR}/lib/${_kj_output_platform}/${_config}")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${_config_upper} "${CMAKE_BINARY_DIR}/lib/${_kj_output_platform}/${_config}")
endforeach()

include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

add_subdirectory(src/core)
add_subdirectory(src/gui)

option(KJ_ENABLE_VST3 "Build the experimental VST3 host and loading probe" OFF)
if(KJ_ENABLE_VST3)
    enable_testing()
    add_subdirectory(src/hosting)
endif()

add_executable(KJ
    src/main.cpp
)

target_link_libraries(KJ
    PRIVATE kj_core kj_gui
)

if (MSVC)
    # Ensure all GUI symbols are available when linking the Windows executable.
    # This avoids missing exports like requestMainMenuRefresh() that may
    # otherwise be omitted when kj_gui is consumed as a static library.
    target_link_options(KJ PRIVATE "/WHOLEARCHIVE:kj_gui")
endif()

option(KJ_BUILD_TESTS "Build sampler regression tests" OFF)
if(KJ_BUILD_TESTS)
    enable_testing()
    target_compile_definitions(kj_core PRIVATE KJ_BUILD_TESTS)
    add_executable(kj_reliability_tests tests/reliability.cpp)
    target_link_libraries(kj_reliability_tests PRIVATE kj_core kj_gui)
    add_test(NAME reliability COMMAND kj_reliability_tests)
    add_executable(kj_input_tests tests/audio_input.cpp)
    target_link_libraries(kj_input_tests PRIVATE kj_core kj_gui)
    add_test(NAME audio_input COMMAND kj_input_tests "${CMAKE_CURRENT_BINARY_DIR}/input_test.jik")
    add_executable(kj_input_live_tests tests/audio_input_live.cpp)
    target_link_libraries(kj_input_live_tests PRIVATE kj_core kj_gui)

    add_executable(kj_lfo_tests tests/lfo_bank.cpp)
    add_executable(kj_vst3_project_data_tests tests/vst3_project_data.cpp)
    target_link_libraries(kj_vst3_project_data_tests PRIVATE kj_core kj_gui)
    add_test(NAME vst3_project_data COMMAND kj_vst3_project_data_tests "${CMAKE_CURRENT_BINARY_DIR}/plugin_data_test.jik")
    target_link_libraries(kj_lfo_tests PRIVATE kj_core kj_gui)
    add_test(NAME lfo_bank COMMAND kj_lfo_tests "${CMAKE_CURRENT_BINARY_DIR}/lfo_test.jik")
    add_executable(kj_editing_panel_tests tests/editing_panel.cpp)
    add_test(NAME editing_panel COMMAND kj_editing_panel_tests)
    add_executable(kj_snapshot_tests tests/playback_snapshot.cpp)
    add_test(NAME playback_snapshot COMMAND kj_snapshot_tests)
    add_executable(kj_sampler_tests tests/sampler_modes.cpp)
    add_executable(kj_piano_tests tests/piano_continuous.cpp)
    add_executable(kj_eq_tests tests/parametric_eq.cpp)
    add_executable(kj_eq_spectrum_tests tests/eq_spectrum.cpp src/core/eq_spectrum.cpp)
    target_include_directories(kj_eq_spectrum_tests PRIVATE include)
    add_test(NAME eq_spectrum COMMAND kj_eq_spectrum_tests)
    target_link_libraries(kj_eq_tests PRIVATE kj_core kj_gui)
    add_test(NAME parametric_eq COMMAND kj_eq_tests "${CMAKE_CURRENT_BINARY_DIR}/eq_preview.bmp")
    add_executable(kj_audio_device_tests tests/audio_device_initialization.cpp)
    target_link_libraries(kj_audio_device_tests PRIVATE kj_core kj_gui)
    add_test(NAME audio_device_initialization COMMAND kj_audio_device_tests)
    target_link_libraries(kj_piano_tests PRIVATE kj_core kj_gui)
    add_test(NAME piano_continuous COMMAND kj_piano_tests)
    add_executable(kj_piano_scroll_tests tests/piano_scrolling.cpp)
    target_link_libraries(kj_piano_scroll_tests PRIVATE kj_core kj_gui)
    add_test(NAME piano_scrolling COMMAND kj_piano_scroll_tests)
    target_link_libraries(kj_sampler_tests PRIVATE kj_core kj_gui)
    add_test(NAME sampler_modes COMMAND kj_sampler_tests "${CMAKE_CURRENT_BINARY_DIR}/sampler_test.jik")
    add_executable(kj_envelope_tests tests/envelope_timing.cpp)
    add_test(NAME envelope_timing COMMAND kj_envelope_tests)
    add_executable(kj_synth_tests tests/synth_oscillators.cpp)
    add_executable(kj_recording_tests tests/audio_recording.cpp src/core/audio_recording.cpp)
    add_test(NAME audio_recording COMMAND kj_recording_tests "${CMAKE_CURRENT_BINARY_DIR}/recording_test.wav")
    add_test(NAME synth_oscillators COMMAND kj_synth_tests)
endif()
