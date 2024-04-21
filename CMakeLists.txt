cmake_minimum_required(VERSION 3.19)

project(CastingEssentials)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_BUILD_TYPE RelWithDebInfo)
set(HL2SDK_PATH sdk2013/mp/src)

add_compile_options(/Od)

include(cmake/GetGitRevisionDescription.cmake)
get_git_head_revision(GIT_REFSPEC GIT_SHA1)
string(SUBSTRING ${GIT_SHA1} 0 7 GIT_SHA1_SHORT)

configure_file(CastingEssentials/GitVersion.cpp.in GitVersion.cpp @ONLY)
configure_file(CastingEssentials/version.rc.in version.rc @ONLY)

add_library(CastingEssentials SHARED
    ${HL2SDK_PATH}/public/vgui_controls/vgui_controls.cpp
    ${HL2SDK_PATH}/public/tier0/memoverride.cpp

    ${CMAKE_BINARY_DIR}/version.rc
    ${CMAKE_BINARY_DIR}/GitVersion.cpp

    CastingEssentials/Controls/ImageProgressBar.cpp
    CastingEssentials/Controls/VariableLabel.cpp
    CastingEssentials/Hooking/IBaseHook.cpp
    CastingEssentials/Hooking/IGroupHook.cpp
    CastingEssentials/Misc/DebugOverlay.cpp
    CastingEssentials/Misc/OffsetChecking.cpp
    CastingEssentials/Modules/ClientTools.cpp
    CastingEssentials/Modules/HitEvents.cpp
    CastingEssentials/Modules/HUDHacking.cpp
    CastingEssentials/Misc/MissingDefinitions.cpp
    CastingEssentials/Misc/Polyhook.cpp
    CastingEssentials/Modules/Antifreeze.cpp
    CastingEssentials/Modules/CameraAutoSwitch.cpp
    CastingEssentials/Modules/CameraSmooths.cpp
    CastingEssentials/Modules/CameraState.cpp
    CastingEssentials/Modules/CameraTools.cpp
    CastingEssentials/Modules/ConsoleTools.cpp
    CastingEssentials/Modules/FOVOverride.cpp
    CastingEssentials/Modules/Graphics.cpp
    CastingEssentials/Modules/ItemSchema.cpp
    CastingEssentials/Modules/Killfeed.cpp
    CastingEssentials/Modules/Killstreaks.cpp
    CastingEssentials/Modules/LoadoutIcons.cpp
    CastingEssentials/Modules/LocalPlayer.cpp
    CastingEssentials/Modules/MapConfigs.cpp
    CastingEssentials/Modules/AutoCameras.cpp
    CastingEssentials/Modules/MedigunInfo.cpp
    CastingEssentials/Modules/PlayerAliases.cpp
    CastingEssentials/Modules/ProjectileOutlines.cpp
    CastingEssentials/Modules/FreezeInfo.cpp
    CastingEssentials/Modules/SniperLOS.cpp
    CastingEssentials/Modules/SrcTVPlus.cpp
    CastingEssentials/Modules/SteamTools.cpp
    CastingEssentials/Modules/TeamNames.cpp
    CastingEssentials/Modules/IngameTeamScores.cpp
    CastingEssentials/Modules/TextureTools.cpp
    CastingEssentials/Modules/ViewAngles.cpp
    CastingEssentials/Modules/WeaponTools.cpp
    CastingEssentials/PluginBase/CastingPlugin.cpp
    CastingEssentials/PluginBase/Common.cpp
    CastingEssentials/PluginBase/Entities.cpp
    CastingEssentials/PluginBase/Exceptions.cpp
    CastingEssentials/PluginBase/HookManager.cpp
    CastingEssentials/PluginBase/Interfaces.cpp
    CastingEssentials/PluginBase/PlayerStateBase.cpp
    CastingEssentials/PluginBase/Modules.cpp
    CastingEssentials/PluginBase/Player.cpp
    CastingEssentials/Controls/StubPanel.cpp
    CastingEssentials/PluginBase/TFPlayerResource.cpp
    CastingEssentials/PluginBase/TFTeamResource.cpp
)

target_include_directories(CastingEssentials PRIVATE
    CastingEssentials
    ${HL2SDK_PATH}/common
    ${HL2SDK_PATH}/public
    ${HL2SDK_PATH}/public/tier0
    ${HL2SDK_PATH}/public/tier1
    ${HL2SDK_PATH}/game/
    ${HL2SDK_PATH}/game/shared
    polyhook/PolyHook
    $ENV{DXSDK_DIR}/Include
)

target_compile_options(CastingEssentials PRIVATE
    /FI ${CMAKE_SOURCE_DIR}/CastingEssentials/PluginBase/Common.h
    /MT
)

target_compile_definitions(CastingEssentials PRIVATE
    _CRT_NO_VA_START_VALIDATION
    SUPPRESS_INVALID_PARAMETER_NO_INFO
    VERSION_SAFE_STEAM_API_INTERFACES
    CLIENT_DLL
    WIN32
    RAD_TELEMETRY_DISABLED
    TF2_SDK
    TF_CLIENT_DLL
    GLOWS_ENABLE
    MOVE_CONSTRUCTOR_SUPPORT
    USES_ECON_ITEMS
    CE_DLL
    COMPILER_MSVC64
    WIN64
    _WINDOWS
    _CRT_DECLARE_NONSTDC_NAMES=1
    _CRT_NONSTDC_NO_DEPRECATE=1
)

target_link_directories(CastingEssentials PRIVATE
    Libraries
    polyhook/Capstone/msvc/x64/Release
)

target_link_libraries(CastingEssentials PRIVATE
    legacy_stdio_definitions
    tier0
    tier1
    tier2
    tier3
    steam_api64
    vstdlib
    mathlib
    vgui_controls
    capstone
)
