cmake_minimum_required(VERSION 3.25)

project(Unleashed VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(UNLEASHED_TEST_SERVER "Build the optional localhost read-only test server" ON)
option(UNLEASHED_BUILD_TESTS "Build offline regression tests" OFF)
option(UNLEASHED_OBSERVATION_ONLY "Disable physical output and target writes for visual validation" OFF)

enable_testing()
# Product modules are shared by the bounded offline checks.
include_directories("${CMAKE_SOURCE_DIR}/src")

if(MSVC)
    add_compile_options(/utf-8 /MP8)
endif()

# ============================================================================
# ImGui source files
# ============================================================================

set(IMGUI_SOURCES
    vendor/imgui/imgui.cpp
    vendor/imgui/imgui_draw.cpp
    vendor/imgui/imgui_tables.cpp
    vendor/imgui/imgui_widgets.cpp
    vendor/imgui/backends/imgui_impl_win32.cpp
    vendor/imgui/backends/imgui_impl_dx11.cpp
)

# ============================================================================
# Game / Engine source files
# ============================================================================

set(GAME_HEADERS
    include/resource.h
    include/Game/AbilityIcons.hpp
    include/Game/AimArchitecture.hpp
    include/Game/AimSmoothingTiming.hpp
    include/Game/Decrypt.hpp
    include/Game/Entity.hpp
    include/Game/EntityRosterPolicy.hpp
    include/Game/HeroGeometrySpec.hpp
    include/Game/HeroProfileSwitch.hpp
    include/Game/HeroPerkLookupSeed.hpp
    include/Game/HeroPerkRuntime.hpp
    include/Game/HeroPerks.hpp
    include/Game/HeroSkills.hpp
    include/output/InputOrchestrator.hpp
    include/Game/LeadPrediction.hpp
    include/Game/Offsets.hpp
    include/Game/Overwatch.hpp
    include/Game/ProductImageIdentity.hpp
    include/output/OutputOwnership.hpp
    include/output/OutputScheduler.hpp
    include/Game/RuntimeState.hpp
    include/Game/SDK.hpp
    include/Game/Structs.hpp
    include/Game/Target.hpp
    include/Game/TriggerBoneSelection.hpp
    include/Game/WeaponCadence.hpp
    include/Game/WeaponSpec.hpp
    include/output/OutputRuntime.hpp
    include/output/TimerResolution.hpp
    include/output/OutputTypes.hpp
    include/output/MockDiagnostics.hpp
    include/Memory/KeyState.hpp
    include/Memory/Memory.h
    include/Memory/MemoryRanges.h
    include/Renderer/IconManager.hpp
    include/Renderer/Overlay.hpp
    include/Renderer/Renderer.hpp
    include/Utils/Config.hpp
    include/Utils/CrashDump.hpp
    include/Utils/Diagnostics.hpp
    include/Utils/InputLabels.hpp
    include/Utils/TestServer.hpp
    include/Utils/Types.hpp
    include/Features/UI.hpp
)

include(cmake/OutputSources.cmake)

# Capture and pixel processing have no DMA or output-device dependency.
add_library(UnContourVision STATIC
    src/vision/CaptureCard.cpp
    src/vision/ContourDetection.cpp
    src/vision/ContourService.cpp
    src/config/ContourSettings.cpp
)
target_include_directories(UnContourVision PUBLIC include PRIVATE src vendor/imgui vendor/rapidjson)
target_link_libraries(UnContourVision PUBLIC strmiids ole32 oleaut32 uuid)

set(UNLEASHED_SOURCES
    src/main.cpp
    src/unleashed.rc
    src/Memory/Memory.cpp
    src/Renderer/IconManager.cpp
    src/Renderer/Overlay.cpp
    src/Renderer/OverlayWindow.cpp
    src/Renderer/Renderer.cpp
    src/Utils/CrashDump.cpp
    src/Utils/Diagnostics.cpp
    src/Utils/TestServer.cpp
    src/Utils/Types.cpp
    src/Utils/Config.cpp
    src/Features/UI.cpp
    src/ui/WorkspaceShell.cpp
    src/ui/SlotSelector.cpp
    src/ui/Text.cpp
    src/ui/Theme.cpp
    src/config/UiPreferences.cpp
    src/config/HeroSettings.cpp
    src/ui/pages/AimPage.cpp
    src/ui/pages/TriggerPage.cpp
    src/ui/pages/SkillPages.cpp
    src/ui/pages/AlertPage.cpp
    src/ui/pages/DisplayPages.cpp
    src/ui/pages/SettingsPages.cpp
    src/ui/vision/ContourPanel.cpp
    src/runtime/ProcessSession.cpp
    src/runtime/WorkerGroup.cpp
    src/features/aim/BehaviorController.cpp
    src/features/aim/Smoothing.cpp
    src/features/aim/SessionState.cpp
    src/features/alerts/Settings.cpp
    src/features/alerts/Speech.cpp
    src/features/alerts/Service.cpp
    src/features/alerts/SkillWorker.cpp
    src/config/HeroProfileWorker.cpp
    src/config/FileTransaction.cpp
    src/config/HeroSlotStore.cpp
    src/features/visuals/EntityVisuals.cpp
    src/features/visuals/SceneRenderer.cpp
    src/Game/camera/CameraWorker.cpp
    src/Game/map/MapReader.cpp
    src/Game/snapshots/PresentationWorker.cpp
    src/Game/entities/EntityDiscovery.cpp
    src/Game/entities/EntityReader.cpp
    src/Game/HeroGeometrySpec.cpp
    src/Game/HeroPerkRuntime.cpp
    src/Game/HeroSkills.cpp
    src/Game/Offsets.cpp
    src/Game/WeaponCadence.cpp
    src/Game/WeaponSpec.cpp
    vendor/stb/stb_image.cpp
    ${UNLEASHED_OUTPUT_BACKEND_SOURCES}
    ${UNLEASHED_OUTPUT_ACTION_SOURCES}
    src/ui/output/OutputPage.cpp
    src/ui/output/DevicePanels.cpp
    src/ui/output/SerialFields.cpp
    src/ui/output/kmbox_net/Panel.cpp
    src/ui/output/kmbox_serial/Panel.cpp
    src/ui/output/makcu/Panel.cpp
    src/ui/output/ferrum/Panel.cpp
    src/ui/output/catbox/Panel.cpp
    src/ui/output/mock/Panel.cpp
    src/output/settings/DeviceMaintenance.cpp
    src/output/devices/kmbox_net/Maintenance.cpp
    src/output/platform/NetworkMaintenance.cpp
)

# ============================================================================
# Executable
# ============================================================================

add_executable(Unleashed
    ${UNLEASHED_SOURCES}
    ${IMGUI_SOURCES}
)
set_target_properties(Unleashed PROPERTIES OUTPUT_NAME "UNDMA_V2")
if(NOT UNLEASHED_BUILD_TESTS)
    # The desktop product should not launch a foreground console/Terminal window.
    set_target_properties(Unleashed PROPERTIES WIN32_EXECUTABLE TRUE)
    target_link_options(Unleashed PRIVATE /ENTRY:mainCRTStartup)
endif()
if(UNLEASHED_OBSERVATION_ONLY)
    target_compile_definitions(Unleashed PRIVATE UNLEASHED_OBSERVATION_ONLY=1)
endif()

# ============================================================================
# Include directories
# ============================================================================

target_include_directories(Unleashed PRIVATE
    src
    include
    vendor/leechcore
    vendor/imgui
    vendor/imgui/backends
    vendor/rapidjson
    vendor/stb
)

# ============================================================================
# Link libraries
# ============================================================================

target_link_libraries(Unleashed PRIVATE
    UnContourVision
    "${CMAKE_SOURCE_DIR}/vendor/leechcore/leechcore.lib"
    "${CMAKE_SOURCE_DIR}/vendor/leechcore/vmm.lib"
    d3d11
    dxgi
    windowscodecs
    ole32
    ws2_32
    winmm
    shell32
    dwmapi
    setupapi
    dbghelp
)

# ============================================================================
# Post-build: copy DMA runtime DLLs
# ============================================================================

function(copy_leechcore_runtime target_name)
    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/vendor/leechcore/info.db"
            "$<TARGET_FILE_DIR:${target_name}>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/vendor/leechcore/leechcore.dll"
            "$<TARGET_FILE_DIR:${target_name}>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/vendor/leechcore/vmm.dll"
            "$<TARGET_FILE_DIR:${target_name}>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/vendor/leechcore/FTD3XX.dll"
            "$<TARGET_FILE_DIR:${target_name}>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/vendor/leechcore/FTD3XXWU.dll"
            "$<TARGET_FILE_DIR:${target_name}>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/vendor/leechcore/leechcore_device_rawtcp.dll"
            "$<TARGET_FILE_DIR:${target_name}>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/vendor/leechcore/leechcore_device_hvsavedstate.dll"
            "$<TARGET_FILE_DIR:${target_name}>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/vendor/leechcore/leechcore_driver.dll"
            "$<TARGET_FILE_DIR:${target_name}>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/vendor/leechcore/Helper64.dll"
            "$<TARGET_FILE_DIR:${target_name}>"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:${target_name}>/config"
        COMMAND ${CMAKE_COMMAND}
            "-DSOURCE_CONFIG=${CMAKE_SOURCE_DIR}/config/dma_device.cfg"
            "-DOUTPUT_CONFIG=$<TARGET_FILE_DIR:${target_name}>/config/dma_device.cfg"
            -P "${CMAKE_SOURCE_DIR}/cmake/CopyDefaultConfig.cmake"
    )
endfunction()

copy_leechcore_runtime(Unleashed)

add_custom_target(V2Assets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets/icons"
        "$<TARGET_FILE_DIR:Unleashed>/assets/icons"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets/heroes"
        "$<TARGET_FILE_DIR:Unleashed>/assets/heroes"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets/ability-icons"
        "$<TARGET_FILE_DIR:Unleashed>/assets/ability-icons"
    DEPENDS Unleashed
    COMMENT "Sync V2 runtime icons and UI/overlay portraits, including asset-only changes"
)

# ============================================================================
# Compiler options
# ============================================================================

if(MSVC)
    function(enable_release_symbols target_name)
        target_compile_options(${target_name} PRIVATE "$<$<CONFIG:Release>:/Zi>")
        target_link_options(${target_name} PRIVATE "$<$<CONFIG:Release>:/DEBUG:FULL>")
    endfunction()

    enable_release_symbols(Unleashed)
endif()

target_compile_options(Unleashed PRIVATE /W3 /wd4244 /wd4267 /wd4305 /wd4200)
target_compile_definitions(Unleashed PRIVATE _CRT_SECURE_NO_WARNINGS)
if(UNLEASHED_TEST_SERVER)
    target_compile_definitions(Unleashed PRIVATE UNLEASHED_TEST_SERVER=1)
endif()

if(UNLEASHED_BUILD_TESTS)
    set(UNLEASHED_TEST_ROOT "${CMAKE_SOURCE_DIR}/../../04_SCENARIO/un-dma-v2" CACHE PATH "Offline regression source root")
    # Header-level targeting/config consumers use the same vision implementation.
    link_libraries(UnContourVision)
    include("${UNLEASHED_TEST_ROOT}/cmake/SelfTests.cmake")
endif()
