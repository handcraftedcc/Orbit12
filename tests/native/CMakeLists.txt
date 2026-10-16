cmake_minimum_required(VERSION 3.20)
project(orbit12_native_tests LANGUAGES CXX)

# This project builds acceptance fixtures only. Preserve assert checks even
# when exercising optimized configurations, without conflicting /D and /U.
foreach(config RELEASE RELWITHDEBINFO MINSIZEREL)
  string(REGEX REPLACE "[-/]DNDEBUG" "" CMAKE_CXX_FLAGS_${config} "${CMAKE_CXX_FLAGS_${config}}")
endforeach()

option(ORBIT_NATIVE_ASAN_UBSAN "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(ORBIT_NATIVE_TSAN "Enable ThreadSanitizer (bridge-only job)" OFF)

if(ORBIT_NATIVE_ASAN_UBSAN AND ORBIT_NATIVE_TSAN)
  message(FATAL_ERROR "ASan/UBSan and TSan must run in separate builds")
endif()

add_library(orbit_domain INTERFACE)
target_include_directories(orbit_domain INTERFACE
  "${CMAKE_CURRENT_LIST_DIR}/../../firmware/Orbit12/src")
target_compile_features(orbit_domain INTERFACE cxx_std_17)

if(MSVC)
  set(ORBIT_WARNINGS /W4 /WX /permissive-)
else()
  set(ORBIT_WARNINGS -Wall -Wextra -Wpedantic -Werror)
endif()

function(orbit_native_test target source)
  add_executable(${target} ${source})
  target_link_libraries(${target} PRIVATE orbit_domain)
  target_compile_options(${target} PRIVATE ${ORBIT_WARNINGS})
  if(ORBIT_NATIVE_ASAN_UBSAN)
    target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=address,undefined)
  elseif(ORBIT_NATIVE_TSAN)
    target_compile_options(${target} PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=thread)
  endif()
  add_test(NAME ${target} COMMAND ${target})
endfunction()

enable_testing()
orbit_native_test(t01_foundation t01_foundation.cpp)
orbit_native_test(t02_fakes t02_fakes.cpp)
orbit_native_test(t03_music t03_music.cpp)
orbit_native_test(t04_transport t04_transport.cpp)
target_sources(t04_transport PRIVATE ../../firmware/Orbit12/src/domain/transport.cpp)
orbit_native_test(t05_output t05_output.cpp)
target_sources(t05_output PRIVATE ../../firmware/Orbit12/src/domain/output.cpp)
orbit_native_test(t06_chain t06_chain.cpp)
target_sources(t06_chain PRIVATE ../../firmware/Orbit12/src/domain/chain.cpp ../../firmware/Orbit12/src/domain/modules/registry.cpp ../../firmware/Orbit12/src/domain/modules/basics.cpp ../../firmware/Orbit12/src/domain/output.cpp)
orbit_native_test(t09a_basics t09a_modules.cpp)
target_sources(t09a_basics PRIVATE ../../firmware/Orbit12/src/domain/modules/registry.cpp ../../firmware/Orbit12/src/domain/modules/basics.cpp)
orbit_native_test(t08a_inputs t08a_inputs.cpp)
target_sources(t08a_inputs PRIVATE ../../firmware/Orbit12/src/domain/modules/inputs.cpp ../../firmware/Orbit12/src/domain/modules/chords_input.cpp ../../firmware/Orbit12/src/domain/modules/input_registry.cpp ../../firmware/Orbit12/src/domain/modules/matrix_input.cpp ../../firmware/Orbit12/src/domain/modules/walk_inputs.cpp)
orbit_native_test(t08b_chords t08b_chords.cpp)
target_sources(t08b_chords PRIVATE ../../firmware/Orbit12/src/domain/modules/inputs.cpp ../../firmware/Orbit12/src/domain/modules/chords_input.cpp ../../firmware/Orbit12/src/domain/modules/input_registry.cpp ../../firmware/Orbit12/src/domain/modules/matrix_input.cpp ../../firmware/Orbit12/src/domain/modules/walk_inputs.cpp)
orbit_native_test(t07a_scene_codec t07a_scene_codec.cpp)
target_sources(t07a_scene_codec PRIVATE ../../firmware/Orbit12/src/persistence/scene_codec.cpp)
find_package(Threads REQUIRED)
orbit_native_test(t11a_bridge t11a_bridge.cpp)
target_link_libraries(t11a_bridge PRIVATE Threads::Threads)
orbit_native_test(t07a_scene_float_text t07a_scene_float_text.cpp)
target_sources(t07a_scene_float_text PRIVATE ../../firmware/Orbit12/src/persistence/scene_codec.cpp)
target_link_libraries(t07a_scene_float_text PRIVATE Threads::Threads)
orbit_native_test(t08c_matrix t08c_matrix.cpp)
target_sources(t08c_matrix PRIVATE ../../firmware/Orbit12/src/domain/modules/matrix_input.cpp ../../firmware/Orbit12/src/domain/modules/inputs.cpp ../../firmware/Orbit12/src/domain/modules/chords_input.cpp ../../firmware/Orbit12/src/domain/modules/input_registry.cpp ../../firmware/Orbit12/src/domain/modules/matrix_input.cpp ../../firmware/Orbit12/src/domain/modules/walk_inputs.cpp)
orbit_native_test(t07b_scene_service t07b_scene_service.cpp)
target_sources(t07b_scene_service PRIVATE ../../firmware/Orbit12/src/persistence/scene_service.cpp ../../firmware/Orbit12/src/persistence/scene_names.cpp ../../firmware/Orbit12/src/persistence/scene_codec.cpp ../../firmware/Orbit12/src/domain/output.cpp)
target_sources(t07b_scene_service PRIVATE ../../firmware/Orbit12/src/persistence/user_settings.cpp)
target_sources(t06_chain PRIVATE ../../firmware/Orbit12/src/domain/modules/drop_randomize.cpp)
target_sources(t09a_basics PRIVATE ../../firmware/Orbit12/src/domain/modules/drop_randomize.cpp)
orbit_native_test(t09b_drop_randomize t09b_drop_randomize.cpp)
target_sources(t09b_drop_randomize PRIVATE ../../firmware/Orbit12/src/domain/modules/registry.cpp ../../firmware/Orbit12/src/domain/modules/basics.cpp ../../firmware/Orbit12/src/domain/modules/drop_randomize.cpp)
orbit_native_test(t08d_walk t08d_walk.cpp)
target_sources(t08d_walk PRIVATE ../../firmware/Orbit12/src/domain/modules/walk_inputs.cpp ../../firmware/Orbit12/src/domain/modules/inputs.cpp ../../firmware/Orbit12/src/domain/modules/chords_input.cpp ../../firmware/Orbit12/src/domain/modules/input_registry.cpp ../../firmware/Orbit12/src/domain/modules/matrix_input.cpp)
target_sources(t06_chain PRIVATE ../../firmware/Orbit12/src/domain/modules/delay_bounce.cpp)
target_sources(t09a_basics PRIVATE ../../firmware/Orbit12/src/domain/modules/delay_bounce.cpp)
target_sources(t09b_drop_randomize PRIVATE ../../firmware/Orbit12/src/domain/modules/delay_bounce.cpp)
orbit_native_test(t09c_delay_bounce t09c_delay_bounce.cpp)
target_sources(t09c_delay_bounce PRIVATE ../../firmware/Orbit12/src/domain/modules/registry.cpp ../../firmware/Orbit12/src/domain/modules/basics.cpp ../../firmware/Orbit12/src/domain/modules/drop_randomize.cpp ../../firmware/Orbit12/src/domain/modules/delay_bounce.cpp ../../firmware/Orbit12/src/domain/modules/gate_chopper_retrigger.cpp ../../firmware/Orbit12/src/domain/modules/euclid.cpp)
target_sources(t06_chain PRIVATE ../../firmware/Orbit12/src/domain/modules/gate_chopper_retrigger.cpp)
target_sources(t09a_basics PRIVATE ../../firmware/Orbit12/src/domain/modules/gate_chopper_retrigger.cpp)
target_sources(t09b_drop_randomize PRIVATE ../../firmware/Orbit12/src/domain/modules/gate_chopper_retrigger.cpp)
orbit_native_test(t09d_gate_chopper_retrigger t09d_gate_chopper_retrigger.cpp)
target_sources(t09d_gate_chopper_retrigger PRIVATE ../../firmware/Orbit12/src/domain/modules/registry.cpp ../../firmware/Orbit12/src/domain/modules/basics.cpp ../../firmware/Orbit12/src/domain/modules/drop_randomize.cpp ../../firmware/Orbit12/src/domain/modules/delay_bounce.cpp ../../firmware/Orbit12/src/domain/modules/gate_chopper_retrigger.cpp)
target_sources(t06_chain PRIVATE ../../firmware/Orbit12/src/domain/modules/euclid.cpp)
target_sources(t09a_basics PRIVATE ../../firmware/Orbit12/src/domain/modules/euclid.cpp)
target_sources(t09b_drop_randomize PRIVATE ../../firmware/Orbit12/src/domain/modules/euclid.cpp)
target_sources(t09d_gate_chopper_retrigger PRIVATE ../../firmware/Orbit12/src/domain/modules/euclid.cpp)
orbit_native_test(t09g_euclid t09g_euclid.cpp)
target_sources(t09g_euclid PRIVATE ../../firmware/Orbit12/src/domain/modules/registry.cpp ../../firmware/Orbit12/src/domain/modules/basics.cpp ../../firmware/Orbit12/src/domain/modules/drop_randomize.cpp ../../firmware/Orbit12/src/domain/modules/delay_bounce.cpp ../../firmware/Orbit12/src/domain/modules/gate_chopper_retrigger.cpp ../../firmware/Orbit12/src/domain/modules/euclid.cpp)
target_sources(t06_chain PRIVATE ../../firmware/Orbit12/src/domain/modules/euclid.cpp ../../firmware/Orbit12/src/domain/modules/arp.cpp)
target_sources(t09a_basics PRIVATE ../../firmware/Orbit12/src/domain/modules/euclid.cpp ../../firmware/Orbit12/src/domain/modules/arp.cpp)
target_sources(t09b_drop_randomize PRIVATE ../../firmware/Orbit12/src/domain/modules/euclid.cpp ../../firmware/Orbit12/src/domain/modules/arp.cpp)
target_sources(t09c_delay_bounce PRIVATE ../../firmware/Orbit12/src/domain/modules/arp.cpp)
target_sources(t09d_gate_chopper_retrigger PRIVATE ../../firmware/Orbit12/src/domain/modules/euclid.cpp ../../firmware/Orbit12/src/domain/modules/arp.cpp)
target_sources(t09g_euclid PRIVATE ../../firmware/Orbit12/src/domain/modules/arp.cpp)
orbit_native_test(t09e_arp t09e_arp.cpp)
target_sources(t09e_arp PRIVATE ../../firmware/Orbit12/src/domain/modules/registry.cpp ../../firmware/Orbit12/src/domain/modules/basics.cpp ../../firmware/Orbit12/src/domain/modules/drop_randomize.cpp ../../firmware/Orbit12/src/domain/modules/delay_bounce.cpp ../../firmware/Orbit12/src/domain/modules/gate_chopper_retrigger.cpp ../../firmware/Orbit12/src/domain/modules/euclid.cpp ../../firmware/Orbit12/src/domain/modules/arp.cpp)
target_sources(t06_chain PRIVATE ../../firmware/Orbit12/src/domain/modules/latch_pick.cpp)
target_sources(t09a_basics PRIVATE ../../firmware/Orbit12/src/domain/modules/latch_pick.cpp)
target_sources(t09b_drop_randomize PRIVATE ../../firmware/Orbit12/src/domain/modules/latch_pick.cpp)
target_sources(t09c_delay_bounce PRIVATE ../../firmware/Orbit12/src/domain/modules/latch_pick.cpp)
target_sources(t09d_gate_chopper_retrigger PRIVATE ../../firmware/Orbit12/src/domain/modules/latch_pick.cpp)
target_sources(t09e_arp PRIVATE ../../firmware/Orbit12/src/domain/modules/latch_pick.cpp)
target_sources(t09g_euclid PRIVATE ../../firmware/Orbit12/src/domain/modules/latch_pick.cpp)
orbit_native_test(t09h_latch_pick t09h_latch_pick.cpp)
target_sources(t09h_latch_pick PRIVATE ../../firmware/Orbit12/src/domain/modules/registry.cpp ../../firmware/Orbit12/src/domain/modules/basics.cpp ../../firmware/Orbit12/src/domain/modules/drop_randomize.cpp ../../firmware/Orbit12/src/domain/modules/delay_bounce.cpp ../../firmware/Orbit12/src/domain/modules/gate_chopper_retrigger.cpp ../../firmware/Orbit12/src/domain/modules/euclid.cpp ../../firmware/Orbit12/src/domain/modules/arp.cpp ../../firmware/Orbit12/src/domain/modules/latch_pick.cpp)
target_sources(t06_chain PRIVATE ../../firmware/Orbit12/src/domain/modules/arp_walk.cpp)
target_sources(t09a_basics PRIVATE ../../firmware/Orbit12/src/domain/modules/arp_walk.cpp)
target_sources(t09b_drop_randomize PRIVATE ../../firmware/Orbit12/src/domain/modules/arp_walk.cpp)
target_sources(t09c_delay_bounce PRIVATE ../../firmware/Orbit12/src/domain/modules/arp_walk.cpp)
target_sources(t09d_gate_chopper_retrigger PRIVATE ../../firmware/Orbit12/src/domain/modules/arp_walk.cpp)
target_sources(t09e_arp PRIVATE ../../firmware/Orbit12/src/domain/modules/arp_walk.cpp)
target_sources(t09g_euclid PRIVATE ../../firmware/Orbit12/src/domain/modules/arp_walk.cpp)
target_sources(t09h_latch_pick PRIVATE ../../firmware/Orbit12/src/domain/modules/arp_walk.cpp)
orbit_native_test(t09f_arp_walk t09f_arp_walk.cpp)
target_sources(t09f_arp_walk PRIVATE ../../firmware/Orbit12/src/domain/modules/registry.cpp ../../firmware/Orbit12/src/domain/modules/basics.cpp ../../firmware/Orbit12/src/domain/modules/drop_randomize.cpp ../../firmware/Orbit12/src/domain/modules/delay_bounce.cpp ../../firmware/Orbit12/src/domain/modules/gate_chopper_retrigger.cpp ../../firmware/Orbit12/src/domain/modules/euclid.cpp ../../firmware/Orbit12/src/domain/modules/arp.cpp ../../firmware/Orbit12/src/domain/modules/latch_pick.cpp ../../firmware/Orbit12/src/domain/modules/quantize_strum.cpp ../../firmware/Orbit12/src/domain/modules/arp_walk.cpp)
target_sources(t06_chain PRIVATE ../../firmware/Orbit12/src/domain/modules/quantize_strum.cpp)
target_sources(t09a_basics PRIVATE ../../firmware/Orbit12/src/domain/modules/quantize_strum.cpp)
target_sources(t09b_drop_randomize PRIVATE ../../firmware/Orbit12/src/domain/modules/quantize_strum.cpp)
target_sources(t09c_delay_bounce PRIVATE ../../firmware/Orbit12/src/domain/modules/quantize_strum.cpp)
target_sources(t09d_gate_chopper_retrigger PRIVATE ../../firmware/Orbit12/src/domain/modules/quantize_strum.cpp)
target_sources(t09e_arp PRIVATE ../../firmware/Orbit12/src/domain/modules/quantize_strum.cpp)
target_sources(t09g_euclid PRIVATE ../../firmware/Orbit12/src/domain/modules/quantize_strum.cpp)
target_sources(t09h_latch_pick PRIVATE ../../firmware/Orbit12/src/domain/modules/quantize_strum.cpp)
orbit_native_test(t09i_quantize_strum t09i_quantize_strum.cpp)
target_sources(t09i_quantize_strum PRIVATE ../../firmware/Orbit12/src/domain/modules/registry.cpp ../../firmware/Orbit12/src/domain/modules/basics.cpp ../../firmware/Orbit12/src/domain/modules/drop_randomize.cpp ../../firmware/Orbit12/src/domain/modules/delay_bounce.cpp ../../firmware/Orbit12/src/domain/modules/gate_chopper_retrigger.cpp ../../firmware/Orbit12/src/domain/modules/euclid.cpp ../../firmware/Orbit12/src/domain/modules/arp.cpp ../../firmware/Orbit12/src/domain/modules/latch_pick.cpp ../../firmware/Orbit12/src/domain/modules/arp_walk.cpp ../../firmware/Orbit12/src/domain/modules/quantize_strum.cpp)
target_sources(t06_chain PRIVATE ../../firmware/Orbit12/src/domain/modules/wander.cpp)
target_sources(t09a_basics PRIVATE ../../firmware/Orbit12/src/domain/modules/wander.cpp)
target_sources(t09b_drop_randomize PRIVATE ../../firmware/Orbit12/src/domain/modules/wander.cpp)
target_sources(t09c_delay_bounce PRIVATE ../../firmware/Orbit12/src/domain/modules/wander.cpp)
target_sources(t09d_gate_chopper_retrigger PRIVATE ../../firmware/Orbit12/src/domain/modules/wander.cpp)
target_sources(t09e_arp PRIVATE ../../firmware/Orbit12/src/domain/modules/wander.cpp)
target_sources(t09f_arp_walk PRIVATE ../../firmware/Orbit12/src/domain/modules/wander.cpp)
target_sources(t09g_euclid PRIVATE ../../firmware/Orbit12/src/domain/modules/wander.cpp)
target_sources(t09h_latch_pick PRIVATE ../../firmware/Orbit12/src/domain/modules/wander.cpp)
target_sources(t09i_quantize_strum PRIVATE ../../firmware/Orbit12/src/domain/modules/wander.cpp)
orbit_native_test(t09j_wander t09j_wander.cpp)
target_sources(t09j_wander PRIVATE ../../firmware/Orbit12/src/domain/modules/registry.cpp ../../firmware/Orbit12/src/domain/modules/basics.cpp ../../firmware/Orbit12/src/domain/modules/drop_randomize.cpp ../../firmware/Orbit12/src/domain/modules/delay_bounce.cpp ../../firmware/Orbit12/src/domain/modules/gate_chopper_retrigger.cpp ../../firmware/Orbit12/src/domain/modules/euclid.cpp ../../firmware/Orbit12/src/domain/modules/arp.cpp ../../firmware/Orbit12/src/domain/modules/latch_pick.cpp ../../firmware/Orbit12/src/domain/modules/arp_walk.cpp ../../firmware/Orbit12/src/domain/modules/quantize_strum.cpp ../../firmware/Orbit12/src/domain/modules/wander.cpp)
orbit_native_test(t10_platform t10_platform.cpp)
target_sources(t10_platform PRIVATE ../../firmware/Orbit12/src/domain/output.cpp)

# T11b controller / ui_model
set(ORBIT_SRC "${CMAKE_CURRENT_LIST_DIR}/../../firmware/Orbit12/src")
set(ORBIT_T11B_SOURCES
  ${ORBIT_SRC}/app/controller.cpp ${ORBIT_SRC}/app/ui_model.cpp
  ${ORBIT_SRC}/domain/chain.cpp ${ORBIT_SRC}/domain/output.cpp ${ORBIT_SRC}/domain/transport.cpp
  ${ORBIT_SRC}/domain/modules/registry.cpp ${ORBIT_SRC}/domain/modules/basics.cpp
  ${ORBIT_SRC}/domain/modules/drop_randomize.cpp ${ORBIT_SRC}/domain/modules/delay_bounce.cpp
  ${ORBIT_SRC}/domain/modules/gate_chopper_retrigger.cpp ${ORBIT_SRC}/domain/modules/euclid.cpp
  ${ORBIT_SRC}/domain/modules/arp.cpp ${ORBIT_SRC}/domain/modules/latch_pick.cpp
  ${ORBIT_SRC}/domain/modules/arp_walk.cpp ${ORBIT_SRC}/domain/modules/quantize_strum.cpp
  ${ORBIT_SRC}/domain/modules/wander.cpp ${ORBIT_SRC}/domain/modules/inputs.cpp
  ${ORBIT_SRC}/domain/modules/chords_input.cpp ${ORBIT_SRC}/domain/modules/input_registry.cpp
  ${ORBIT_SRC}/domain/modules/matrix_input.cpp ${ORBIT_SRC}/domain/modules/walk_inputs.cpp
  ${ORBIT_SRC}/persistence/scene_codec.cpp ${ORBIT_SRC}/persistence/scene_service.cpp ${ORBIT_SRC}/persistence/scene_names.cpp
  ${ORBIT_SRC}/persistence/user_settings.cpp)
orbit_native_test(t11b_ui_model t11b_ui_model.cpp)
target_sources(t11b_ui_model PRIVATE ${ORBIT_SRC}/app/ui_model.cpp)
orbit_native_test(t11b_shortcuts t11b_shortcuts.cpp)
target_sources(t11b_shortcuts PRIVATE ${ORBIT_T11B_SOURCES})
orbit_native_test(t11b_engine t11b_engine.cpp)
target_sources(t11b_engine PRIVATE ${ORBIT_T11B_SOURCES})
orbit_native_test(t11b_scenes t11b_scenes.cpp)
target_sources(t11b_scenes PRIVATE ${ORBIT_T11B_SOURCES})

orbit_native_test(t14_slice t14_slice.cpp)
orbit_native_test(t14c_core1_attribution t14c_core1_attribution.cpp)
target_sources(t14_slice PRIVATE ${ORBIT_T11B_SOURCES} ${ORBIT_SRC}/app/app_main.cpp ${ORBIT_SRC}/app/renderer.cpp
  ${ORBIT_SRC}/persistence/maintenance_protocol.cpp)

# T13 persistence: user settings, maintenance protocol (CDC import/export), SceneService user-settings ops
set(ORBIT_T13_SOURCES
  ${ORBIT_SRC}/persistence/scene_codec.cpp ${ORBIT_SRC}/persistence/scene_service.cpp ${ORBIT_SRC}/persistence/scene_names.cpp
  ${ORBIT_SRC}/persistence/user_settings.cpp ${ORBIT_SRC}/persistence/maintenance_protocol.cpp)
orbit_native_test(t13a_protocol t13a_protocol.cpp)
target_sources(t13a_protocol PRIVATE ${ORBIT_T13_SOURCES})
orbit_native_test(t13b_user_settings t13b_user_settings.cpp)
target_sources(t13b_user_settings PRIVATE ${ORBIT_T13_SOURCES})

orbit_native_test(t12_renderer t12_renderer.cpp)
target_sources(t12_renderer PRIVATE ${ORBIT_SRC}/app/renderer.cpp ${ORBIT_SRC}/app/ui_model.cpp)
orbit_native_test(t12b_format t12b_format.cpp)
target_sources(t12b_format PRIVATE ${ORBIT_SRC}/app/ui_model.cpp)
orbit_native_test(t12c_renderer_sweep t12c_renderer_sweep.cpp)
target_sources(t12c_renderer_sweep PRIVATE ${ORBIT_SRC}/app/renderer.cpp ${ORBIT_SRC}/app/ui_model.cpp)
orbit_native_test(t13c_interrupted t13c_interrupted.cpp)
target_sources(t13c_interrupted PRIVATE ${ORBIT_T13_SOURCES})
orbit_native_test(t13d_format t13d_format.cpp)
target_sources(t13d_format PRIVATE ${ORBIT_T13_SOURCES} ${ORBIT_SRC}/persistence/fs_format.cpp)

# T15 module selector footer descriptions (app/module_descriptors.hpp)
orbit_native_test(t15_module_descriptors t15_module_descriptors.cpp)
target_sources(t15_module_descriptors PRIVATE
  ${ORBIT_SRC}/app/ui_model.cpp
  ${ORBIT_SRC}/domain/modules/registry.cpp ${ORBIT_SRC}/domain/modules/basics.cpp
  ${ORBIT_SRC}/domain/modules/drop_randomize.cpp ${ORBIT_SRC}/domain/modules/delay_bounce.cpp
  ${ORBIT_SRC}/domain/modules/gate_chopper_retrigger.cpp ${ORBIT_SRC}/domain/modules/euclid.cpp
  ${ORBIT_SRC}/domain/modules/arp.cpp ${ORBIT_SRC}/domain/modules/latch_pick.cpp
  ${ORBIT_SRC}/domain/modules/arp_walk.cpp ${ORBIT_SRC}/domain/modules/quantize_strum.cpp
  ${ORBIT_SRC}/domain/modules/wander.cpp ${ORBIT_SRC}/domain/modules/inputs.cpp
  ${ORBIT_SRC}/domain/modules/chords_input.cpp ${ORBIT_SRC}/domain/modules/input_registry.cpp
  ${ORBIT_SRC}/domain/modules/matrix_input.cpp ${ORBIT_SRC}/domain/modules/walk_inputs.cpp)
