cmake_minimum_required(VERSION 3.10)
project(EfzExtendedPractice)

option(EFZ_XP_COMPAT "Enable Windows XP-oriented compile settings (requires v141_xp-compatible toolset)" OFF)
option(EFZ_ENABLE_INPUT_LOGS "Enable verbose input/menu navigation trace logging" OFF)
option(EFZ_ENABLE_AUTO_ACTION_WAKE_TRACE "Enable verbose auto-action wake/subframe trace logging" OFF)
option(EFZ_ENABLE_EXPERIMENTAL_TRIAL_TUTORIAL
       "Enable the unfinished trial/tutorial modes and their title-screen Practice patch"
       OFF)
set(EFZ_MINHOOK_SOURCE_DIR "" CACHE PATH "Optional path to a MinHook source tree for source builds")

set(EFZ_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(EFZ_USE_SOURCE_MINHOOK OFF)
set(EFZ_USE_SOURCE_DETOURS OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "${EFZ_MSVC_RUNTIME_LIBRARY}" CACHE STRING "" FORCE)
    set(CMAKE_GENERATOR_PLATFORM Win32)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /MACHINE:X86")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /MACHINE:X86")

    foreach(flag_var
            CMAKE_C_FLAGS CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_RELEASE
            CMAKE_C_FLAGS_MINSIZEREL CMAKE_C_FLAGS_RELWITHDEBINFO
            CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
            CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO)
        if(${flag_var} MATCHES "/MD")
            string(REGEX REPLACE "/MD" "/MT" ${flag_var} "${${flag_var}}")
        endif()
    endforeach()

    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHsc")
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)

    # All gamepad input is handled via PreNewFrameInputs(); ImGui's built-in
    # XInput polling would cause double-navigation.
    add_definitions(-DIMGUI_IMPL_WIN32_DISABLE_GAMEPAD)

    if(EFZ_XP_COMPAT)
        add_definitions(-DWINVER=0x0501 -D_WIN32_WINNT=0x0501 -DEFZ_XP_COMPAT=1)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Zc:threadSafeInit-")
        set(EFZ_USE_SOURCE_MINHOOK ON)
        if(NOT CMAKE_GENERATOR_TOOLSET MATCHES "v141_xp")
            message(WARNING "EFZ_XP_COMPAT is enabled but generator toolset is not v141_xp; the binary may still import Vista+ APIs.")
        endif()
    endif()
endif()

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/imgui/imgui.cpp")
    message(FATAL_ERROR "ImGui files not found. Please check that 3rdparty/imgui/imgui.cpp exists.")
endif()

set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/imgui")

set(IMGUI_SOURCES
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_demo.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_win32.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_dx9.cpp"
)

file(GLOB ROOT_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
file(GLOB CORE_SOURCES CONFIGURE_DEPENDS "src/core/*.cpp")
file(GLOB GAME_SOURCES CONFIGURE_DEPENDS "src/game/*.cpp")
file(GLOB GAME_PRACTICE_MENU_SOURCES CONFIGURE_DEPENDS "src/game/practice_menu/*.cpp")
file(GLOB GAME_MISSION_SOURCES CONFIGURE_DEPENDS "src/game/mission/*.cpp")
file(GLOB GUI_SOURCES CONFIGURE_DEPENDS "src/gui/*.cpp")
file(GLOB GUI_CUSTOM_MENU_SOURCES CONFIGURE_DEPENDS "src/gui/custom_menu/*.cpp")
file(GLOB INPUT_SOURCES CONFIGURE_DEPENDS "src/input/*.cpp")
file(GLOB UTILS_SOURCES CONFIGURE_DEPENDS "src/utils/*.cpp")
file(GLOB RUNTIME_SOURCES CONFIGURE_DEPENDS "src/runtime/*.cpp")
set(RESOURCE_SOURCES "src/resources/efz_training_resources.rc")

list(REMOVE_ITEM ROOT_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/frame_advantage.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/frame_monitor.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/frame_analysis.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/utilities.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/input_handler.cpp"
)

set(SOURCES
    ${ROOT_SOURCES}
    ${CORE_SOURCES}
    ${GAME_SOURCES}
    ${GAME_PRACTICE_MENU_SOURCES}
    ${GAME_MISSION_SOURCES}
    ${GUI_SOURCES}
    ${GUI_CUSTOM_MENU_SOURCES}
    ${INPUT_SOURCES}
    ${UTILS_SOURCES}
    ${RUNTIME_SOURCES}
    ${IMGUI_SOURCES}
    ${RESOURCE_SOURCES}
)

set(EFZ_MINHOOK_SOURCE_CANDIDATES
    "${EFZ_MINHOOK_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/minhook"
    "${CMAKE_CURRENT_SOURCE_DIR}/../InGameControlsRebind/third_party/minhook"
    "${CMAKE_CURRENT_SOURCE_DIR}/../efz_bgtool/3rdparty/minhook"
)

if(EFZ_USE_SOURCE_MINHOOK)
    set(EFZ_RESOLVED_MINHOOK_SOURCE_DIR "")
    foreach(candidate IN LISTS EFZ_MINHOOK_SOURCE_CANDIDATES)
        if(candidate AND EXISTS "${candidate}/CMakeLists.txt")
            set(EFZ_RESOLVED_MINHOOK_SOURCE_DIR "${candidate}")
            break()
        endif()
    endforeach()

    if(NOT EFZ_RESOLVED_MINHOOK_SOURCE_DIR)
        message(FATAL_ERROR "EFZ_XP_COMPAT requires MinHook source. Set EFZ_MINHOOK_SOURCE_DIR or provide a source tree at ../InGameControlsRebind/third_party/minhook.")
    endif()

    message(STATUS "[XP] Using MinHook source build from ${EFZ_RESOLVED_MINHOOK_SOURCE_DIR}")

    if(NOT TARGET minhook)
        add_subdirectory("${EFZ_RESOLVED_MINHOOK_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/third_party/minhook" EXCLUDE_FROM_ALL)
    endif()

    if(MSVC)
        set_property(TARGET minhook PROPERTY MSVC_RUNTIME_LIBRARY "${EFZ_MSVC_RUNTIME_LIBRARY}")
    endif()
endif()

if(EFZ_USE_SOURCE_DETOURS)
    message(STATUS "[XP] Using in-tree Detours source build")

    add_library(efz_detours STATIC
        "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/detours/src/detours.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/detours/src/modules.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/detours/src/disasm.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/detours/src/image.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/detours/src/creatwth.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/detours/src/disolx86.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/detours/src/disolx64.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/detours/src/disolia64.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/detours/src/disolarm.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/detours/src/disolarm64.cpp"
    )

    target_include_directories(efz_detours PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/detours/include"
    )
    target_include_directories(efz_detours PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/detours/src"
    )
    target_compile_definitions(efz_detours PRIVATE WIN32_LEAN_AND_MEAN)

    if(MSVC)
        set_property(TARGET efz_detours PROPERTY MSVC_RUNTIME_LIBRARY "${EFZ_MSVC_RUNTIME_LIBRARY}")
    endif()
endif()

add_library(efz_training_mode SHARED ${SOURCES})

if(EFZ_ENABLE_INPUT_LOGS)
    target_compile_definitions(efz_training_mode PRIVATE EFZ_ENABLE_INPUT_LOGS=1)
endif()

if(EFZ_ENABLE_AUTO_ACTION_WAKE_TRACE)
    target_compile_definitions(efz_training_mode PRIVATE EFZ_ENABLE_AUTO_ACTION_WAKE_TRACE=1)
endif()

if(EFZ_ENABLE_EXPERIMENTAL_TRIAL_TUTORIAL)
    target_compile_definitions(efz_training_mode PRIVATE EFZ_ENABLE_EXPERIMENTAL_TRIAL_TUTORIAL=1)
endif()

target_include_directories(efz_training_mode PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/core
    ${CMAKE_CURRENT_SOURCE_DIR}/include/game
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gui
    ${CMAKE_CURRENT_SOURCE_DIR}/include/input
    ${CMAKE_CURRENT_SOURCE_DIR}/include/utils
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/detours/include
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/imgui/backends
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/minhook/include
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/json/single_include
)

set(EFZ_THIRD_PARTY_LINKS
    user32
    kernel32
    winmm
    ws2_32
    iphlpapi
    comctl32
    gdiplus
    shlwapi
    d3d9
    dbghelp
    legacy_stdio_definitions
)

if(EFZ_USE_SOURCE_DETOURS)
    list(APPEND EFZ_THIRD_PARTY_LINKS efz_detours)
else()
    if(EFZ_XP_COMPAT)
        message(STATUS "[XP] Using prebuilt Detours library to preserve the known-good runtime hook path")
    endif()
    list(APPEND EFZ_THIRD_PARTY_LINKS "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/detours/lib.X86/detours.lib")
endif()

if(EFZ_USE_SOURCE_MINHOOK)
    list(APPEND EFZ_THIRD_PARTY_LINKS minhook)
else()
    list(APPEND EFZ_THIRD_PARTY_LINKS "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/minhook/lib/libMinHook.x86.lib")
endif()

target_link_libraries(efz_training_mode PRIVATE ${EFZ_THIRD_PARTY_LINKS})

if(MSVC)
    set_target_properties(efz_training_mode PROPERTIES
        STATIC_LIBRARY_FLAGS "/LTCG")

    target_compile_options(efz_training_mode PRIVATE
        /FS
        "$<$<CONFIG:Release>:/Z7>"
    )

    target_link_options(efz_training_mode PRIVATE
        "$<$<CONFIG:Release>:/DEBUG:FULL>"
        "$<$<CONFIG:Release>:/OPT:REF>"
        "$<$<CONFIG:Release>:/OPT:ICF>"
        # Debug directory holds the PDB's file name, not the build machine's path.
        "$<$<CONFIG:Release>:/PDBALTPATH:efz_training_mode.pdb>"
    )

    if(EFZ_XP_COMPAT)
        # The v141_xp toolset does not ship dxguid.lib; the DirectInput GUIDs
        # are provided in-tree.
        target_link_options(efz_training_mode PRIVATE "/NODEFAULTLIB:dxguid.lib")
    endif()
endif()

set_target_properties(efz_training_mode PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    PDB_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin/Release"
    PDB_NAME "efz_training_mode"
)

set_property(TARGET efz_training_mode PROPERTY
    MSVC_RUNTIME_LIBRARY "${EFZ_MSVC_RUNTIME_LIBRARY}")

include(CTest)
if(BUILD_TESTING)
    add_executable(overlay_reset_gate_tests tests/overlay_reset_gate_tests.cpp)
    target_include_directories(overlay_reset_gate_tests PRIVATE include)
    add_test(NAME overlay_reset_gate_tests COMMAND overlay_reset_gate_tests)
    add_executable(overlay_context_scope_tests
        tests/overlay_context_scope_tests.cpp
        ${IMGUI_DIR}/imgui.cpp ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_tables.cpp ${IMGUI_DIR}/imgui_widgets.cpp)
    target_include_directories(overlay_context_scope_tests PRIVATE include ${IMGUI_DIR})
    add_test(NAME overlay_context_scope_tests COMMAND overlay_context_scope_tests)

    add_executable(audio_output_lifetime_tests
        tests/practice_isolation/audio_output_lifetime_tests.cpp)
    target_include_directories(audio_output_lifetime_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    set_property(TARGET audio_output_lifetime_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "${EFZ_MSVC_RUNTIME_LIBRARY}")
    add_test(NAME audio_output_lifetime_tests COMMAND audio_output_lifetime_tests)

    if(MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 4)
        add_executable(audio_native_adapter_abi_tests
            tests/practice_isolation/audio_native_adapter_abi_tests.cpp)
        target_include_directories(audio_native_adapter_abi_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include)
        set_property(TARGET audio_native_adapter_abi_tests PROPERTY
            MSVC_RUNTIME_LIBRARY "${EFZ_MSVC_RUNTIME_LIBRARY}")
        add_test(NAME audio_native_adapter_abi_tests COMMAND audio_native_adapter_abi_tests)
    endif()

    add_executable(patch_ledger_win32_tests
        tests/practice_isolation/patch_ledger_win32_tests.cpp
        src/runtime/patch_ledger.cpp
        src/runtime/patch_ledger_win32.cpp)
    target_include_directories(patch_ledger_win32_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    set_property(TARGET patch_ledger_win32_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "${EFZ_MSVC_RUNTIME_LIBRARY}")
    add_test(NAME patch_ledger_win32_tests COMMAND patch_ledger_win32_tests)

    add_executable(audio_runtime_state_tests
        tests/practice_isolation/audio_runtime_state_tests.cpp
        src/utils/audio_runtime_state.cpp)
    target_include_directories(audio_runtime_state_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    set_property(TARGET audio_runtime_state_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "${EFZ_MSVC_RUNTIME_LIBRARY}")
    add_test(NAME audio_runtime_state_tests COMMAND audio_runtime_state_tests)

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../ExtendedConfig/src/feature/audio_control.cpp"
       AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../ExtendedConfig/src/feature/shared_config_store.cpp")
        add_executable(audio_file_transactions_tests
            tests/practice_isolation/audio_file_transactions_tests.cpp
            tests/practice_isolation/audio_transaction_training.cpp
            tests/practice_isolation/audio_transaction_companion.cpp
            src/utils/audio_runtime_state.cpp)
        target_include_directories(audio_file_transactions_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/../ExtendedConfig/include
            ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/minhook/include)
        target_compile_definitions(audio_file_transactions_tests PRIVATE
            NOMINMAX WIN32_LEAN_AND_MEAN)
        if(MSVC)
            target_compile_options(audio_file_transactions_tests PRIVATE /Gy)
        endif()
        set_property(TARGET audio_file_transactions_tests PROPERTY
            MSVC_RUNTIME_LIBRARY "${EFZ_MSVC_RUNTIME_LIBRARY}")
        if(TARGET minhook)
            target_link_libraries(audio_file_transactions_tests PRIVATE minhook)
        else()
            target_link_libraries(audio_file_transactions_tests PRIVATE
                "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/minhook/lib/libMinHook.x86.lib")
        endif()
        add_test(NAME audio_file_transactions_tests COMMAND audio_file_transactions_tests)
    else()
        message(STATUS "Audio companion transaction tests unavailable: ExtendedConfig source checkout is required")
    endif()

    add_executable(patch_ledger_tests
        tests/practice_isolation/patch_ledger_tests.cpp
        src/runtime/patch_ledger.cpp)
    target_include_directories(patch_ledger_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    set_property(TARGET patch_ledger_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "${EFZ_MSVC_RUNTIME_LIBRARY}")
    add_test(NAME patch_ledger_tests COMMAND patch_ledger_tests)

    add_executable(hook_registry_tests
        tests/practice_isolation/hook_registry_tests.cpp
        src/runtime/hook_registry.cpp
        src/runtime/patch_ledger.cpp)
    target_include_directories(hook_registry_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    set_property(TARGET hook_registry_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "${EFZ_MSVC_RUNTIME_LIBRARY}")
    add_test(NAME hook_registry_tests COMMAND hook_registry_tests)

    add_executable(external_hook_registry_tests
        tests/practice_isolation/external_hook_registry_tests.cpp
        src/runtime/hook_registry.cpp
        src/runtime/patch_ledger.cpp)
    target_include_directories(external_hook_registry_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    set_property(TARGET external_hook_registry_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "${EFZ_MSVC_RUNTIME_LIBRARY}")
    add_test(NAME external_hook_registry_tests COMMAND external_hook_registry_tests)

    if(MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 4)
        add_executable(practice_native_adapter_tests
            tests/practice_isolation/practice_native_adapter_tests.cpp
            src/runtime/practice_frontend_case.cpp
            src/runtime/hook_registry.cpp
            src/runtime/patch_ledger.cpp)
        target_include_directories(practice_native_adapter_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include)
        set_property(TARGET practice_native_adapter_tests PROPERTY
            MSVC_RUNTIME_LIBRARY "${EFZ_MSVC_RUNTIME_LIBRARY}")
        add_test(NAME practice_native_adapter_tests COMMAND practice_native_adapter_tests)

        add_executable(practice_battle_gates_tests
            tests/practice_isolation/practice_battle_gates_tests.cpp
            src/runtime/practice_battle_gates.cpp
            src/runtime/hook_registry.cpp
            src/runtime/patch_ledger.cpp)
        target_include_directories(practice_battle_gates_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include)
        set_property(TARGET practice_battle_gates_tests PROPERTY
            MSVC_RUNTIME_LIBRARY "${EFZ_MSVC_RUNTIME_LIBRARY}")
        add_test(NAME practice_battle_gates_tests COMMAND practice_battle_gates_tests)
    endif()

    add_executable(practice_session_tests
        tests/practice_isolation/practice_session_tests.cpp)
    target_include_directories(practice_session_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    set_property(TARGET practice_session_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "${EFZ_MSVC_RUNTIME_LIBRARY}")
    add_test(NAME practice_session_tests COMMAND practice_session_tests)

    add_executable(practice_worker_tests
        tests/practice_isolation/practice_worker_tests.cpp
        src/runtime/practice_worker.cpp)
    target_include_directories(practice_worker_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    set_property(TARGET practice_worker_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "${EFZ_MSVC_RUNTIME_LIBRARY}")
    add_test(NAME practice_worker_tests COMMAND practice_worker_tests)

    add_executable(practice_runtime_tests
        tests/practice_isolation/practice_runtime_tests.cpp
        src/runtime/practice_runtime.cpp
        src/runtime/practice_worker.cpp
        src/runtime/hook_registry.cpp
        src/runtime/patch_ledger.cpp
        src/runtime/patch_ledger_win32.cpp)
    target_include_directories(practice_runtime_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    set_property(TARGET practice_runtime_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "${EFZ_MSVC_RUNTIME_LIBRARY}")
    add_test(NAME practice_runtime_tests COMMAND practice_runtime_tests)
    add_test(NAME practice_runtime_duplicate_begin_tests
        COMMAND practice_runtime_tests duplicate-begin)

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../InGameNetplay/src/netplay/bridge/practice_runtime_owner.cpp")
        add_executable(practice_provider_owner_tests
            tests/practice_isolation/practice_provider_owner_tests.cpp
            src/runtime/practice_runtime.cpp
            src/runtime/practice_worker.cpp
            src/runtime/hook_registry.cpp
            src/runtime/patch_ledger.cpp
            src/runtime/patch_ledger_win32.cpp
            ../InGameNetplay/src/netplay/bridge/practice_runtime_owner.cpp)
        target_include_directories(practice_provider_owner_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/../InGameNetplay/include)
        set_property(TARGET practice_provider_owner_tests PROPERTY
            MSVC_RUNTIME_LIBRARY "${EFZ_MSVC_RUNTIME_LIBRARY}")
        add_test(NAME practice_provider_owner_tests COMMAND practice_provider_owner_tests)

        add_executable(practice_provider_hold_tests
            tests/practice_isolation/practice_provider_hold_tests.cpp
            ../InGameNetplay/src/netplay/bridge/practice_runtime_owner.cpp)
        target_include_directories(practice_provider_hold_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/../InGameNetplay/include)
        set_property(TARGET practice_provider_hold_tests PROPERTY
            MSVC_RUNTIME_LIBRARY "${EFZ_MSVC_RUNTIME_LIBRARY}")
        add_test(NAME practice_provider_hold_tests COMMAND practice_provider_hold_tests)

        add_executable(practice_provider_registration_tests
            tests/practice_isolation/practice_provider_registration_tests.cpp
            ../InGameNetplay/src/netplay/bridge/practice_runtime_bridge.cpp
            ../InGameNetplay/src/netplay/bridge/practice_runtime_owner.cpp)
        target_include_directories(practice_provider_registration_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/../InGameNetplay/include)
        target_compile_definitions(practice_provider_registration_tests PRIVATE NOMINMAX)
        set_property(TARGET practice_provider_registration_tests PROPERTY
            MSVC_RUNTIME_LIBRARY "${EFZ_MSVC_RUNTIME_LIBRARY}")
        add_test(NAME practice_provider_registration_tests
            COMMAND practice_provider_registration_tests)
    else()
        message(STATUS "Practice provider owner tests unavailable: InGameNetplay source checkout is required")
    endif()

    add_executable(practice_frontend_result_tests
        tests/practice_isolation/practice_frontend_result_tests.cpp
        src/game/battle_frontend_result.cpp)
    target_include_directories(practice_frontend_result_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    set_property(TARGET practice_frontend_result_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "${EFZ_MSVC_RUNTIME_LIBRARY}")
    add_test(NAME practice_frontend_result_tests COMMAND practice_frontend_result_tests)

    add_executable(audio_volume_contract
        tests/practice_isolation/audio_volume_contract.cpp)
    target_include_directories(audio_volume_contract PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    set_property(TARGET audio_volume_contract PROPERTY
        MSVC_RUNTIME_LIBRARY "${EFZ_MSVC_RUNTIME_LIBRARY}")
    add_test(NAME audio_volume_contract COMMAND audio_volume_contract)

    add_executable(revival_profile_tests
        tests/revival_profile_tests.cpp)
    target_include_directories(revival_profile_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    set_property(TARGET revival_profile_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "${EFZ_MSVC_RUNTIME_LIBRARY}")
    add_test(NAME revival_profile_tests
             COMMAND revival_profile_tests)

    add_executable(character_hotswap_transition_tests
        tests/character_hotswap_transition_tests.cpp)
    target_include_directories(character_hotswap_transition_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    set_property(TARGET character_hotswap_transition_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "${EFZ_MSVC_RUNTIME_LIBRARY}")
    add_test(NAME character_hotswap_transition_tests
             COMMAND character_hotswap_transition_tests)

    add_executable(character_hotswap_frontend_tests
        tests/practice_isolation/character_hotswap_frontend_tests.cpp
        src/game/character_hotswap_frontend.cpp)
    target_include_directories(character_hotswap_frontend_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    set_property(TARGET character_hotswap_frontend_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "${EFZ_MSVC_RUNTIME_LIBRARY}")
    add_test(NAME character_hotswap_frontend_tests
        COMMAND character_hotswap_frontend_tests)

    add_executable(mission_sequence_policy_tests
        tests/mission_sequence_policy_tests.cpp)
    target_include_directories(mission_sequence_policy_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    set_property(TARGET mission_sequence_policy_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "${EFZ_MSVC_RUNTIME_LIBRARY}")
    add_test(NAME mission_sequence_policy_tests
             COMMAND mission_sequence_policy_tests)

    add_executable(macro_playback_start_policy_tests
        tests/macro_playback_start_policy_tests.cpp)
    target_include_directories(macro_playback_start_policy_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    set_property(TARGET macro_playback_start_policy_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "${EFZ_MSVC_RUNTIME_LIBRARY}")
    add_test(NAME macro_playback_start_policy_tests
             COMMAND macro_playback_start_policy_tests)

    add_executable(collision_box_policy_tests
        tests/collision_box_policy_tests.cpp)
    target_include_directories(collision_box_policy_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    set_property(TARGET collision_box_policy_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "${EFZ_MSVC_RUNTIME_LIBRARY}")
    add_test(NAME collision_box_policy_tests
             COMMAND collision_box_policy_tests)

    add_executable(move_timing_policy_tests
        tests/move_timing_policy_tests.cpp)
    target_include_directories(move_timing_policy_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    set_property(TARGET move_timing_policy_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "${EFZ_MSVC_RUNTIME_LIBRARY}")
    add_test(NAME move_timing_policy_tests
             COMMAND move_timing_policy_tests)

    add_executable(menu_key_policy_tests
        tests/menu_key_policy_tests.cpp)
    target_include_directories(menu_key_policy_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    set_property(TARGET menu_key_policy_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "${EFZ_MSVC_RUNTIME_LIBRARY}")
    add_test(NAME menu_key_policy_tests
             COMMAND menu_key_policy_tests)

    add_executable(visual_freeze_policy_tests
        tests/visual_freeze_policy_tests.cpp)
    target_include_directories(visual_freeze_policy_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    set_property(TARGET visual_freeze_policy_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "${EFZ_MSVC_RUNTIME_LIBRARY}")
    add_test(NAME visual_freeze_policy_tests
             COMMAND visual_freeze_policy_tests)

    add_executable(config_policy_tests
        tests/config_policy_tests.cpp)
    target_include_directories(config_policy_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    set_property(TARGET config_policy_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "${EFZ_MSVC_RUNTIME_LIBRARY}")
    add_test(NAME config_policy_tests
             COMMAND config_policy_tests)

    add_executable(i18n_tests
        tests/i18n_tests.cpp
        src/utils/i18n.cpp)
    target_include_directories(i18n_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    set_property(TARGET i18n_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "${EFZ_MSVC_RUNTIME_LIBRARY}")
    add_test(NAME i18n_tests
             COMMAND i18n_tests)


    add_executable(mission_contact_attribution_policy_tests
        tests/mission_contact_attribution_policy_tests.cpp)
    target_include_directories(mission_contact_attribution_policy_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    set_property(TARGET mission_contact_attribution_policy_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "${EFZ_MSVC_RUNTIME_LIBRARY}")
    add_test(NAME mission_contact_attribution_policy_tests
             COMMAND mission_contact_attribution_policy_tests)

    add_executable(mission_entity_schedule_policy_tests
        tests/mission_entity_schedule_policy_tests.cpp)
    target_include_directories(mission_entity_schedule_policy_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    set_property(TARGET mission_entity_schedule_policy_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "${EFZ_MSVC_RUNTIME_LIBRARY}")
    add_test(NAME mission_entity_schedule_policy_tests
             COMMAND mission_entity_schedule_policy_tests)

    add_executable(mission_entity_lifecycle_policy_tests
        tests/mission_entity_lifecycle_policy_tests.cpp)
    target_include_directories(mission_entity_lifecycle_policy_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    set_property(TARGET mission_entity_lifecycle_policy_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "${EFZ_MSVC_RUNTIME_LIBRARY}")
    add_test(NAME mission_entity_lifecycle_policy_tests
             COMMAND mission_entity_lifecycle_policy_tests)

    add_executable(mission_entity_review_policy_tests
        tests/mission_entity_review_policy_tests.cpp)
    target_include_directories(mission_entity_review_policy_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    set_property(TARGET mission_entity_review_policy_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "${EFZ_MSVC_RUNTIME_LIBRARY}")
    add_test(NAME mission_entity_review_policy_tests
             COMMAND mission_entity_review_policy_tests)

    add_executable(mission_legacy_entity_migration_tests
        tests/mission_legacy_entity_migration_tests.cpp
        src/game/mission/mission_legacy_entity_migration.cpp
        src/game/mission/mission_data.cpp
        src/game/mission/tutorial_support.cpp)
    target_include_directories(mission_legacy_entity_migration_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/json/single_include)
    set_property(TARGET mission_legacy_entity_migration_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "${EFZ_MSVC_RUNTIME_LIBRARY}")
    add_test(NAME mission_legacy_entity_migration_tests
             COMMAND mission_legacy_entity_migration_tests)

    add_executable(mission_entity_presentation_policy_tests
        tests/mission_entity_presentation_policy_tests.cpp)
    target_include_directories(mission_entity_presentation_policy_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    set_property(TARGET mission_entity_presentation_policy_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "${EFZ_MSVC_RUNTIME_LIBRARY}")
    add_test(NAME mission_entity_presentation_policy_tests
             COMMAND mission_entity_presentation_policy_tests)

    add_executable(mission_render_snapshot_tests
        tests/mission_render_snapshot_tests.cpp)
    target_include_directories(mission_render_snapshot_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    set_property(TARGET mission_render_snapshot_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "${EFZ_MSVC_RUNTIME_LIBRARY}")
    add_test(NAME mission_render_snapshot_tests
             COMMAND mission_render_snapshot_tests)

    add_executable(mission_entity_fanout_policy_tests
        tests/mission_entity_fanout_policy_tests.cpp)
    target_include_directories(mission_entity_fanout_policy_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    set_property(TARGET mission_entity_fanout_policy_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "${EFZ_MSVC_RUNTIME_LIBRARY}")
    add_test(NAME mission_entity_fanout_policy_tests
             COMMAND mission_entity_fanout_policy_tests)

    add_executable(snowbunny_transition_policy_tests
        tests/snowbunny_transition_policy_tests.cpp)
    target_include_directories(snowbunny_transition_policy_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    set_property(TARGET snowbunny_transition_policy_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "${EFZ_MSVC_RUNTIME_LIBRARY}")
    add_test(NAME snowbunny_transition_policy_tests
             COMMAND snowbunny_transition_policy_tests)

    add_executable(entity_contact_phase_bridge_policy_tests
        tests/entity_contact_phase_bridge_policy_tests.cpp)
    target_include_directories(entity_contact_phase_bridge_policy_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    set_property(TARGET entity_contact_phase_bridge_policy_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "${EFZ_MSVC_RUNTIME_LIBRARY}")
    add_test(NAME entity_contact_phase_bridge_policy_tests
             COMMAND entity_contact_phase_bridge_policy_tests)

    add_executable(entity_command_origin_policy_tests
        tests/entity_command_origin_policy_tests.cpp)
    target_include_directories(entity_command_origin_policy_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    set_property(TARGET entity_command_origin_policy_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "${EFZ_MSVC_RUNTIME_LIBRARY}")
    add_test(NAME entity_command_origin_policy_tests
             COMMAND entity_command_origin_policy_tests)

    add_executable(tutorial_session_policy_tests
        tests/tutorial_session_policy_tests.cpp)
    target_include_directories(tutorial_session_policy_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    set_property(TARGET tutorial_session_policy_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "${EFZ_MSVC_RUNTIME_LIBRARY}")
    add_test(NAME tutorial_session_policy_tests
             COMMAND tutorial_session_policy_tests)

    add_executable(update_check_policy_tests
        tests/update_check_policy_tests.cpp
        src/utils/version_compare.cpp)
    target_include_directories(update_check_policy_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/json/single_include)
    set_property(TARGET update_check_policy_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "${EFZ_MSVC_RUNTIME_LIBRARY}")
    add_test(NAME update_check_policy_tests
             COMMAND update_check_policy_tests)

    add_executable(version_compare_tests
        tests/version_compare_tests.cpp
        src/utils/version_compare.cpp)
    target_include_directories(version_compare_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    set_property(TARGET version_compare_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "${EFZ_MSVC_RUNTIME_LIBRARY}")
    add_test(NAME version_compare_tests
             COMMAND version_compare_tests)

    add_executable(normal_input_policy_tests
        tests/normal_input_policy_tests.cpp)
    target_include_directories(normal_input_policy_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    set_property(TARGET normal_input_policy_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "${EFZ_MSVC_RUNTIME_LIBRARY}")
    add_test(NAME normal_input_policy_tests
             COMMAND normal_input_policy_tests)

    add_executable(generic_pad_axis_policy_tests
        tests/generic_pad_axis_policy_tests.cpp)
    target_include_directories(generic_pad_axis_policy_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    set_property(TARGET generic_pad_axis_policy_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "${EFZ_MSVC_RUNTIME_LIBRARY}")
    add_test(NAME generic_pad_axis_policy_tests
             COMMAND generic_pad_axis_policy_tests)

    add_executable(efz_input_bindings_tests
        tests/efz_input_bindings_tests.cpp)
    target_include_directories(efz_input_bindings_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    set_property(TARGET efz_input_bindings_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "${EFZ_MSVC_RUNTIME_LIBRARY}")
    add_test(NAME efz_input_bindings_tests
             COMMAND efz_input_bindings_tests)

    add_executable(input_poll_attack_edge_journal_tests
        tests/input_poll_attack_edge_journal_tests.cpp)
    target_include_directories(input_poll_attack_edge_journal_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    set_property(TARGET input_poll_attack_edge_journal_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "${EFZ_MSVC_RUNTIME_LIBRARY}")
    add_test(NAME input_poll_attack_edge_journal_tests
             COMMAND input_poll_attack_edge_journal_tests)

    add_executable(character_action_catalog_tests
        tests/character_action_catalog_tests.cpp)
    target_include_directories(character_action_catalog_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    set_property(TARGET character_action_catalog_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "${EFZ_MSVC_RUNTIME_LIBRARY}")
    add_test(NAME character_action_catalog_tests
             COMMAND character_action_catalog_tests)

    add_executable(move_notation_tables_tests
        tests/move_notation_tables_tests.cpp)
    target_include_directories(move_notation_tables_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    set_property(TARGET move_notation_tables_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "${EFZ_MSVC_RUNTIME_LIBRARY}")
    add_test(NAME move_notation_tables_tests
             COMMAND move_notation_tables_tests)

    add_executable(entity_semantic_catalog_tests
        tests/entity_semantic_catalog_tests.cpp)
    target_include_directories(entity_semantic_catalog_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/json/single_include)
    set_property(TARGET entity_semantic_catalog_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "${EFZ_MSVC_RUNTIME_LIBRARY}")
    add_test(NAME entity_semantic_catalog_tests
             COMMAND entity_semantic_catalog_tests
                     "${CMAKE_CURRENT_SOURCE_DIR}/assets/movedata/entity_manifest.json")

    add_executable(motion_pattern_tests
        tests/motion_pattern_tests.cpp)
    target_include_directories(motion_pattern_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    set_property(TARGET motion_pattern_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "${EFZ_MSVC_RUNTIME_LIBRARY}")
    add_test(NAME motion_pattern_tests
             COMMAND motion_pattern_tests)

    add_executable(auto_action_charge_policy_tests
        tests/auto_action_charge_policy_tests.cpp)
    target_include_directories(auto_action_charge_policy_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    set_property(TARGET auto_action_charge_policy_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "${EFZ_MSVC_RUNTIME_LIBRARY}")
    add_test(NAME auto_action_charge_policy_tests
             COMMAND auto_action_charge_policy_tests)

    add_executable(kaori_recoil_duck_policy_tests
        tests/kaori_recoil_duck_policy_tests.cpp)
    target_include_directories(kaori_recoil_duck_policy_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    set_property(TARGET kaori_recoil_duck_policy_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "${EFZ_MSVC_RUNTIME_LIBRARY}")
    add_test(NAME kaori_recoil_duck_policy_tests
             COMMAND kaori_recoil_duck_policy_tests)

    add_executable(mission_data_tests
        tests/mission_data_tests.cpp
        src/game/mission/mission_data.cpp
        src/game/mission/tutorial_support.cpp)
    target_include_directories(mission_data_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/json/single_include)
    set_property(TARGET mission_data_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "${EFZ_MSVC_RUNTIME_LIBRARY}")
    add_test(NAME mission_data_tests
             COMMAND mission_data_tests
                     "${CMAKE_CURRENT_SOURCE_DIR}/assets/missions/core_tutorial")

    add_executable(mission_authoring_tests
        tests/mission_authoring_tests.cpp
        src/game/mission/mission_authoring.cpp
        src/game/mission/mission_data.cpp
        src/game/mission/tutorial_support.cpp)
    target_include_directories(mission_authoring_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/json/single_include)
    set_property(TARGET mission_authoring_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "${EFZ_MSVC_RUNTIME_LIBRARY}")
    add_test(NAME mission_authoring_tests
             COMMAND mission_authoring_tests)

    add_executable(mission_movedata_union_tests
        tests/mission_movedata_union_tests.cpp)
    target_include_directories(mission_movedata_union_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/json/single_include)
    set_property(TARGET mission_movedata_union_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "${EFZ_MSVC_RUNTIME_LIBRARY}")
    add_test(NAME mission_movedata_union_tests
             COMMAND mission_movedata_union_tests
                     "${CMAKE_CURRENT_SOURCE_DIR}/assets/movedata/nanase.json"
                     "${CMAKE_CURRENT_SOURCE_DIR}/assets/movedata/nanase2.json")

    find_program(EFZ_PYTHON_EXECUTABLE NAMES python python3)
    # tools/ is local-only except the build wrapper.
    if(EFZ_PYTHON_EXECUTABLE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/pat_movedata/entity_census.py")
        add_test(NAME mission_entity_manifest_check
                 COMMAND "${EFZ_PYTHON_EXECUTABLE}"
                         "${CMAKE_CURRENT_SOURCE_DIR}/tools/pat_movedata/entity_census.py"
                         --check)
    endif()
endif()
