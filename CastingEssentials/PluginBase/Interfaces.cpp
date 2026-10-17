#include "Interfaces.h"
#include "Exceptions.h"
#include "Misc/HLTVCameraHack.h"
#include "PluginBase/HookManager.h"

#include <cdll_int.h>
#include <client/cliententitylist.h>
#include <engine/IEngineTrace.h>
#include <engine/IStaticPropMgr.h>
#include <engine/ivdebugoverlay.h>
#include <engine/ivmodelinfo.h>
#include <entitylist_base.h>
#include <filesystem.h>
#include <game/client/iclientrendertargets.h>
#include <icliententitylist.h>
#include <igameevents.h>
#include <iprediction.h>
#include <ivrenderview.h>
#include <shaderapi/ishaderapi.h>
#include <steam/steam_api.h>
#include <tier1/tier1.h>
#include <tier2/tier2.h>
#include <tier3/tier3.h>
#include <toolframework/iclientenginetools.h>
#include <toolframework/ienginetool.h>
#include <vgui_controls/Controls.h>

#include <Windows.h>

IBaseClientDLL* Interfaces::pClientDLL = nullptr;
IClientEngineTools* Interfaces::pClientEngineTools = nullptr;
IClientEntityList* Interfaces::pClientEntityList = nullptr;
IStaticPropMgrClient* Interfaces::s_StaticPropMgr;
IVEngineClient* Interfaces::pEngineClient = nullptr;
IEngineTool* Interfaces::pEngineTool = nullptr;
IGameEventManager2* Interfaces::pGameEventManager = nullptr;
IPrediction* Interfaces::pPrediction = nullptr;
IVModelInfoClient* Interfaces::pModelInfoClient = nullptr;
IVRenderView* Interfaces::pRenderView = nullptr;
IMaterialSystem* Interfaces::pMaterialSystem = nullptr;
IShaderAPI* Interfaces::s_ShaderAPI = nullptr;
CSteamAPIContext* Interfaces::pSteamAPIContext = nullptr;
IFileSystem* Interfaces::s_FileSystem = nullptr;
IVDebugOverlay* Interfaces::s_DebugOverlay = nullptr;
IEngineTrace* Interfaces::s_EngineTrace = nullptr;
ISpatialPartition* Interfaces::s_SpatialPartition = nullptr;
IClientLeafSystem* Interfaces::s_ClientLeafSystem = nullptr;
IClientRenderTargets* Interfaces::s_ClientRenderTargets = nullptr;

bool Interfaces::steamLibrariesAvailable = false;
bool Interfaces::vguiLibrariesAvailable = false;

IClientMode* Interfaces::s_ClientMode = nullptr;
C_HLTVCamera* Interfaces::s_HLTVCamera = nullptr;
C_HLTVCamera* HLTVCamera() { return Interfaces::GetHLTVCamera(); }

CClientEntityList* cl_entitylist;
CBaseEntityList* g_pEntityList;
IGameEventManager2* gameeventmanager;
IVDebugOverlay* debugoverlay;
IEngineTrace* enginetrace;
IVEngineClient* engine;
IVRenderView* render;
IClientLeafSystem* g_pClientLeafSystem;
IClientRenderTargets* g_pClientRenderTargets;

void Interfaces::Load(CreateInterfaceFn factory)
{
    ConnectTier1Libraries(&factory, 1);
    ConnectTier2Libraries(&factory, 1);
    ConnectTier3Libraries(&factory, 1);

    if (!factory)
        Error(__FUNCTION__ ": factory was null");

    vguiLibrariesAvailable = vgui::VGui_InitInterfacesList("CastingEssentials", &factory, 1);

    pClientEngineTools = (IClientEngineTools*)factory(VCLIENTENGINETOOLS_INTERFACE_VERSION, nullptr);
    pEngineClient = (IVEngineClient*)factory(VENGINE_CLIENT_INTERFACE_VERSION, nullptr);
    pEngineTool = (IEngineTool*)factory(VENGINETOOL_INTERFACE_VERSION, nullptr);
    pGameEventManager = (IGameEventManager2*)factory(INTERFACEVERSION_GAMEEVENTSMANAGER2, nullptr);
    pModelInfoClient = (IVModelInfoClient*)factory(VMODELINFO_CLIENT_INTERFACE_VERSION, nullptr);
    pRenderView = (IVRenderView*)factory(VENGINE_RENDERVIEW_INTERFACE_VERSION, nullptr);
    pMaterialSystem = (IMaterialSystem*)factory(MATERIAL_SYSTEM_INTERFACE_VERSION, nullptr);
    s_ShaderAPI = (IShaderAPI*)factory(SHADERAPI_INTERFACE_VERSION, nullptr);
    s_FileSystem = (IFileSystem*)factory(FILESYSTEM_INTERFACE_VERSION, nullptr);
    s_DebugOverlay = (IVDebugOverlay*)factory(VDEBUG_OVERLAY_INTERFACE_VERSION, nullptr);
    s_EngineTrace = (IEngineTrace*)factory(INTERFACEVERSION_ENGINETRACE_CLIENT, nullptr);
    s_SpatialPartition = (ISpatialPartition*)factory(INTERFACEVERSION_SPATIALPARTITION, nullptr);
    s_ClientRenderTargets = (IClientRenderTargets*)factory(CLIENTRENDERTARGETS_INTERFACE_VERSION, nullptr);
    s_StaticPropMgr = (IStaticPropMgrClient*)factory(INTERFACEVERSION_STATICPROPMGR_CLIENT, nullptr);

    CreateInterfaceFn gameClientFactory;
    pEngineTool->GetClientFactory(gameClientFactory);

    if (!gameClientFactory)
        Error(__FUNCTION__ ": gameClientFactory was null");

    pClientDLL = (IBaseClientDLL*)gameClientFactory(CLIENT_DLL_INTERFACE_VERSION, nullptr);
    pClientEntityList = (IClientEntityList*)gameClientFactory(VCLIENTENTITYLIST_INTERFACE_VERSION, nullptr);
    pPrediction = (IPrediction*)gameClientFactory(VCLIENT_PREDICTION_INTERFACE_VERSION, nullptr);
    s_ClientLeafSystem = (IClientLeafSystem*)gameClientFactory(CLIENTLEAFSYSTEM_INTERFACE_VERSION, nullptr);
    s_ClientRenderTargets = (IClientRenderTargets*)gameClientFactory(CLIENTRENDERTARGETS_INTERFACE_VERSION, nullptr);

    pSteamAPIContext = new CSteamAPIContext();
    steamLibrariesAvailable = SteamAPI_InitSafe() && pSteamAPIContext->Init();

    // Built in declarations
    {
        cl_entitylist = dynamic_cast<CClientEntityList*>(Interfaces::GetClientEntityList());
        g_pEntityList = dynamic_cast<CBaseEntityList*>(Interfaces::GetClientEntityList());
        debugoverlay = Interfaces::GetDebugOverlay();
        enginetrace = Interfaces::GetEngineTrace();
        engine = Interfaces::GetEngineClient();
        render = Interfaces::GetRenderView();
        materials = Interfaces::GetMaterialSystem();
        g_pClientLeafSystem = Interfaces::GetClientLeafSystem();
        gameeventmanager = Interfaces::GetGameEventManager();
    }
}

void Interfaces::Unload()
{
    vguiLibrariesAvailable = false;
    steamLibrariesAvailable = false;

    s_ClientMode = nullptr;
    s_HLTVCamera = nullptr;

    pClientEngineTools = nullptr;
    pEngineClient = nullptr;
    pEngineTool = nullptr;
    pGameEventManager = nullptr;
    pModelInfoClient = nullptr;
    pRenderView = nullptr;

    pClientDLL = nullptr;
    pClientEntityList = nullptr;
    pPrediction = nullptr;

    pSteamAPIContext = nullptr;

    // Built in declarations
    cl_entitylist = nullptr;
    g_pEntityList = nullptr;
    debugoverlay = nullptr;
    enginetrace = nullptr;
    engine = nullptr;
    engine = nullptr;
    render = nullptr;
    materials = nullptr;
    g_pStudioRender = nullptr;
    g_pClientLeafSystem = nullptr;
    gameeventmanager = nullptr;

    DisconnectTier3Libraries();
    DisconnectTier2Libraries();
    DisconnectTier1Libraries();
}

IClientMode* Interfaces::GetClientMode()
{
    if (!s_ClientMode)
    {
        constexpr const char* SIG = "\x40\x53\x48\x83\xEC\x30\x8B\x00\x00\x00\x00\x00\xA8\x01\x0F\x00\x00\x00\x00\x00"
                                    "\x83\xC8\x01\x48\x00\x00\x00\x00\x00\x00\x48\x8B\xCB\x89\x00\x00\x00\x00\x00\xE8"
                                    "\x00\x00\x00\x00\x33\xC9\x48\x00\x00\x00\x00\x00\x00\x0F\x57\xC0";
        constexpr const char* MASK = "xxxxxxx?????xxx?????xxxx??????xxxx?????x????xxx??????xxx";
        constexpr auto OFFSET = 0;

        typedef IClientMode* (*GetClientModeFn)();

        auto fn = (GetClientModeFn)SignatureScan("client", SIG, MASK, OFFSET, "GetClientModeNormal");

        if (!fn)
            throw bad_pointer("IClientMode");

        IClientMode* icm = fn();
        if (!icm)
            throw bad_pointer("IClientMode");

        s_ClientMode = icm;
    }

    return s_ClientMode;
}

HLTVCameraOverride* Interfaces::GetHLTVCamera()
{
    if (!s_HLTVCamera)
    {
        constexpr const char* SIG =
            "\x83\x3B\x02\x74\x00\x48\x00\x00\x00\x00\x00\x00\x48\x83\xC4\x20\x5B\x48\x00\x00\x00\x00\x00\x00\x48\x00"
            "\x00\x00\x00\x00\x00\x48\x8B\x01\xFF\x90\xB0\x02\x00\x00\x84\xC0\x74\x00\xE8\x00\x00\x00\x00\x48\x8B\xC8";
        constexpr const char* MASK = "xxxx?x??????xxxxxx??????x??????xxxxxxxxxxxx?x????xxx";
        constexpr uintptr_t OFFSET = 44;

        auto address_of_call = (uint8_t*)SignatureScan("client", SIG, MASK, OFFSET, "HLTVCamera()");
        if (!address_of_call || address_of_call[0] != 0xE8) // call rel32
            throw bad_pointer("HLTVCamera()");

        typedef C_HLTVCamera* (*C_HLTVCameraSingletonGetterFn)();
        auto hltv_camera_getter = reinterpret_cast<C_HLTVCameraSingletonGetterFn>(ResolveRipRelative(address_of_call + 1));
        s_HLTVCamera = hltv_camera_getter();
    }

    if (!s_HLTVCamera)
        throw bad_pointer("C_HLTVCamera");

    return (HLTVCameraOverride*)s_HLTVCamera;
}

cmdalias_t** Interfaces::GetCmdAliases()
{
    static cmdalias_t** s_CmdAliases = []() -> cmdalias_t** {
        // With no arguments, the engine's "alias" command prints this, then walks the alias list:
        //     lea rcx, "Current alias commands:\n"; call Msg; mov rbx, cmd_alias
        cmdalias_t** aliases = nullptr;
        if (const auto str = FindModuleString("engine", "Current alias commands:\n"))
        {
            if (const auto reference = (const uint8_t*)FindRipRelativeReference("engine", str))
            {
                for (auto p = reference + sizeof(int32_t); p < reference + 32; p++)
                {
                    // mov r64, [rip+disp32]
                    if ((p[0] & 0xFB) == 0x48 && p[1] == 0x8B && IsRipRelativeModRM(p[2]))
                    {
                        const auto found = (cmdalias_t**)ResolveRipRelative(p + 3);
                        if (IsAddressInModule("engine", found, sizeof(*found)))
                            aliases = found;

                        break;
                    }
                }
            }
        }

        RecordSignatureStatus("cmd_alias", "engine", aliases ? 1.0f : 0.0f);
        if (!aliases)
            PluginWarning("Unable to find cmd_alias in engine.dll, likely due to a game update. Alias management "
                          "commands will not work.\n");

        return aliases;
    }();

    return s_CmdAliases;
}

C_GameRules* Interfaces::GetGameRules()
{
    static C_GameRules* s_GameRules = nullptr;
    if (!s_GameRules)
    {
        constexpr const char* SIG =
            "\x48\x89\x9C\x24\x88\x00\x00\x00\x48\x8B\x1D\x00\x00\x00\x00\x48\x89\x7C\x24\x78\x4C\x89\x7C\x24\x60";
        constexpr const char* MASK = "xxxxxxxxxxx????xxxxxxxxxx";
        constexpr uintptr_t OFFSET = 8;

        // mov rbx, [rip+disp32] (g_pGameRules)
        const auto gr = (uint8_t*)SignatureScan("client", SIG, MASK, OFFSET, "g_pGameRules");
        if (!gr || gr[0] != 0x48 || gr[1] != 0x8B || !IsRipRelativeModRM(gr[2]))
            throw bad_pointer("C_GameRules");

        s_GameRules = *reinterpret_cast<C_GameRules**>(ResolveRipRelative(gr + 3));
    }

    return s_GameRules;
}

// IGameSystem::Add appends to s_GameSystems, then (for per-frame systems) to s_GameSystemsPerFrame. Each append starts by
// loading that vector's m_Size (mov r32, [rip+disp32]), so the first such load is in s_GameSystems, and the first one
// outside it is in s_GameSystemsPerFrame.
static std::pair<std::byte*, std::byte*> FindGameSystemLists()
{
    // CUtlVector<T*> is { CUtlMemory { T** m_pMemory; int m_nAllocationCount; int m_nGrowSize; } int m_Size; T**
    // m_pElements; }
    constexpr size_t VECTOR_SIZE = sizeof(CUtlVector<IGameSystem*>);
    constexpr size_t SIZE_OFFSET = sizeof(CUtlMemory<IGameSystem*>);
    static_assert(VECTOR_SIZE == 0x20 && SIZE_OFFSET == 0x10);

    const auto looksLikeVector = [](const std::byte* vec) {
        if (!IsAddressInModule("client", vec, VECTOR_SIZE))
            return false;

        const auto memory = *(void* const*)vec;
        const auto allocated = *(const int*)(vec + 8);
        const auto size = *(const int*)(vec + SIZE_OFFSET);
        const auto elements = *(void* const*)(vec + 0x18);
        return size >= 0 && size <= allocated && elements == memory;
    };

    std::byte* lists[2] = {};
    if (const auto add = (const uint8_t*)HookManager::GetRawFunc<HookFunc::IGameSystem_Add>())
    {
        size_t found = 0;
        for (auto p = add; p < add + 0x200 && found < 2; p++)
        {
            const auto op = (p[0] & 0xF8) == 0x40 ? p + 1 : p; // Optional REX prefix (not REX.W)
            if (op[0] != 0x8B || !IsRipRelativeModRM(op[1]))
                continue;

            const auto vec = ResolveRipRelative(op + 2) - SIZE_OFFSET;
            if (found == 1 && vec >= lists[0] - SIZE_OFFSET && vec < lists[0] + VECTOR_SIZE)
                continue; // Another member of the first vector

            if (!looksLikeVector(vec))
                break;

            lists[found++] = vec;
        }

        if (found < 2)
            lists[0] = lists[1] = nullptr;
    }

    RecordSignatureStatus("s_GameSystems", "client", lists[0] ? 1.0f : 0.0f);
    if (!lists[0])
        PluginWarning("Unable to find the game system lists in client.dll, likely due to a game update.\n");

    return {lists[0], lists[1]};
}

static const std::pair<std::byte*, std::byte*>& GetGameSystemLists()
{
    static const auto s_Lists = FindGameSystemLists();
    return s_Lists;
}

CUtlVector<IGameSystem*>* Interfaces::GetGameSystems()
{
    return (CUtlVector<IGameSystem*>*)GetGameSystemLists().first;
}

CUtlVector<IGameSystemPerFrame*>* Interfaces::GetGameSystemsPerFrame()
{
    return (CUtlVector<IGameSystemPerFrame*>*)GetGameSystemLists().second;
}

ITextureManager* Interfaces::GetTextureManager()
{
    static ITextureManager* s_TextureManager = []() -> ITextureManager* {
        // First find the CTextureManager constructor
        void* constructor = SignatureScan("MaterialSystem", "\x56\x8B\xF1\x57\xC7\x06????\xC7\x46", "xxxxxx????xx");
        if (!constructor)
            return nullptr;

        constexpr const char* SIG = "\xB9????\xE8????\x68????\xE8????\x59\xC3";
        constexpr const char* MASK = "x????x????x????x????xx";

        // Then find the static initialization function for the s_TextureManager variable
        auto initFn = SignatureScanMultiple("MaterialSystem", SIG, MASK, [&](std::byte* found) {
            int offset2 = *(int*)(found + 6);
            Assert(true);

            return (found + offset2 + 10) == constructor;
        });

        if (!initFn)
            return nullptr;

        // First instruction of the init function is mov ecx, offset s_TextureManager
        int offset = *(int*)(initFn + 1); // Get the offset of the B9 (mov relative w/ 32-bit address) instruction
        auto mgr = (ITextureManager*)offset;
        Assert(mgr);

        return mgr;
    }();

    return s_TextureManager;
}
