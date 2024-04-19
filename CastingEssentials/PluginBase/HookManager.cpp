#include "HookManager.h"
#include "Controls/StubPanel.h"
#include "Misc/HLTVCameraHack.h"
#include "PluginBase/Exceptions.h"
#include "PluginBase/Interfaces.h"

#include <PolyHook.hpp>

#include <Windows.h>

#include <Psapi.h>
#include <cdll_int.h>
#include <client/hltvcamera.h>
#include <engine/IStaticPropMgr.h>
#include <game/client/iclientrendertargets.h>
#include <icvar.h>
#include <iprediction.h>
#include <toolframework/iclientenginetools.h>

static std::unique_ptr<HookManager> s_HookManager;
HookManager* GetHooks()
{
    Assert(s_HookManager);
    return s_HookManager.get();
}

void* HookManager::s_RawFunctions[(int)HookFunc::Count];

bool HookManager::Load()
{
    s_HookManager.reset(new HookManager());
    return true;
}
bool HookManager::Unload()
{
    s_HookManager.reset();
    return true;
}

class HookManager::Panel final : vgui::StubPanel
{
public:
    Panel()
    {
        if (!Interfaces::GetEngineClient())
            Error("[CastingEssentials] Interfaces::GetEngineClient() returned nullptr.");

        m_InGame = false;
    }
    void OnTick() override
    {
        if (!m_InGame && Interfaces::GetEngineClient()->IsInGame())
        {
            m_InGame = true;
            GetHooks()->IngameStateChanged(m_InGame);
        }
        else if (m_InGame && !Interfaces::GetEngineClient()->IsInGame())
        {
            m_InGame = false;
            GetHooks()->IngameStateChanged(m_InGame);
        }
    }

private:
    bool m_InGame;
};

static bool DataCompare(const BYTE* pData, const BYTE* bSig, const char* szMask)
{
    for (; *szMask; ++szMask, ++pData, ++bSig)
    {
        if (*szMask == 'x' && *pData != *bSig)
            return false;
    }

    return (*szMask) == NULL;
}

static void* FindPattern(uintptr_t address, size_t size, BYTE* pbSig, const char* szMask)
{
    for (uintptr_t i = NULL; i < size; i++)
    {
        if (DataCompare((BYTE*)(address + i), pbSig, szMask))
            return (void*)(address + i);
    }

    return nullptr;
}

#pragma warning(push)
#pragma warning(disable : 4706) // Assignment within conditional expression
std::byte* SignatureScanMultiple(const char* moduleName, const char* signature, const char* mask,
                                 const std::function<bool(std::byte* found)>& testFunc, int offset)
{
    MODULEINFO clientModInfo;
    const HMODULE clientModule = GetModuleHandle((std::string(moduleName) + ".dll").c_str());
    GetModuleInformation(GetCurrentProcess(), clientModule, &clientModInfo, sizeof(MODULEINFO));

    uintptr_t searchOffset = 0;
    std::byte* found;
    while (found = (std::byte*)FindPattern(reinterpret_cast<uintptr_t>(clientModInfo.lpBaseOfDll) + searchOffset,
                                           clientModInfo.SizeOfImage - searchOffset, (PBYTE)signature, mask))
    {
        if (testFunc(found + offset))
            return found + offset;

        searchOffset += reinterpret_cast<uintptr_t>(found) - reinterpret_cast<uintptr_t>(clientModInfo.lpBaseOfDll) -
                        searchOffset + 1;
    }

    return nullptr;
}
#pragma warning(pop)

std::byte* SignatureScan(const char* moduleName, const char* signature, const char* mask, int offset)
{
    MODULEINFO clientModInfo;
    const HMODULE clientModule = GetModuleHandle((std::string(moduleName) + ".dll").c_str());
    GetModuleInformation(GetCurrentProcess(), clientModule, &clientModInfo, sizeof(MODULEINFO));

    auto found = (std::byte*)FindPattern(reinterpret_cast<uintptr_t>(clientModInfo.lpBaseOfDll),
                                         clientModInfo.SizeOfImage, (PBYTE)signature, mask);
    if (found)
        found += offset;

    return found;
}

void HookManager::FindFunc_C_BasePlayer_GetLocalPlayer()
{
    // We know that C_BasePlayer::GetLocalPlayerIndex() immediately calls C_BasePlayer::GetLocalPlayer().
    // Since GetLocalPlayer just returns a global variable, it's not reliable to find it with a signature.
    // Instead, we find GetLocalPlayer indirectly through GetLocalPlayerIndex.
    auto localPlayerIndexFn = reinterpret_cast<uintptr_t>(GetRawFunc<HookFunc::Global_GetLocalPlayerIndex>());
    if (!localPlayerIndexFn)
        return;

    static constexpr uintptr_t offset_to_call_instruction = 4;
    auto localPlayerCall = localPlayerIndexFn + offset_to_call_instruction;

    // We only handle relative offsets, this needs an update if GetLocalPlayerIndex signature ever changes
    Assert(*reinterpret_cast<uint8_t*>(localPlayerCall) == 0xE8);
    auto localPlayerOffset = *reinterpret_cast<int32_t*>(localPlayerCall + 1);

    s_RawFunctions[(int)HookFunc::C_BasePlayer_GetLocalPlayer] =
        reinterpret_cast<void*>(localPlayerCall + localPlayerOffset + 5);
}

void HookManager::FindFunc_CNewParticleEffect_SetDormant()
{
    // Same deal as C_BasePlayer::GetLocalPlayer -- the function is too short to signature scan for. We calculate it
    // by it's only usage instead.
    auto ownerSetDormantToFn = reinterpret_cast<uintptr_t>(GetRawFunc<HookFunc::CParticleProperty_OwnerSetDormantTo>());
    if (!ownerSetDormantToFn)
        return;

    static constexpr uintptr_t offset_to_call_instruction = 13;
    auto setDormantCall = ownerSetDormantToFn + offset_to_call_instruction;

    Assert(*reinterpret_cast<uint8_t*>(setDormantCall) == 0xE8);
    auto setDormantOffset = *reinterpret_cast<int32_t*>(setDormantCall + 1);

    s_RawFunctions[(int)HookFunc::CNewParticleEffect_SetDormant] =
        reinterpret_cast<void*>(setDormantCall + setDormantOffset + 5);
}

void HookManager::InitRawFunctionsList()
{
    FindFunc<HookFunc::Global_Cmd_Shutdown>("\xA1????\x85\xC0\x74\x2F", "x????xxxx", 0, "engine");
    FindFunc<HookFunc::Global_CreateEntityByName>(
        "\x55\x8B\xEC\xE8????\xFF\x75\x08\x8B\xC8\x8B\x10\xFF??\x85\xC0\x75\x13\xFF\x75\x08\x68????\xFF?????"
        "\x83\xC4\x08\x33\xC0\x5D\xC3",
        "xxxx????xxxxxxxx??xxxxxxxx????x?????xxxxxxx");
    FindFunc<HookFunc::Global_CreateTFGlowObject>(
        "\x48\x89\x5C\x24\x10\x48\x89\x74\x24\x18\x57\x48\x83\xEC\x20\x8B\xF1\x8B\xFA\xB9\xD8\x07\x00\x00\xE8\x00\x00"
        "\x00\x00\x48\x8B\xD8\x48\x85\xC0\x0F\x00\x00\x00\x00\x00\x48\x8B\xC8\x48\x89\x6C\x24\x30\xE8\x00\x00\x00\x00"
        "\x48\x00\x00\x00\x00\x00\x00\x33\xED\x48\x89\x03\x44\x8B\xC7\x48\x00\x00\x00\x00\x00\x00\x48\x89\xAB\xC8\x07"
        "\x00\x00\x48\x89\x43\x08\x8B\xD6",
        "xxxxxxxxxxxxxxxxxxxxxxxxx????xxxxxxx?????xxxxxxxxx????x??????xxxxxxxxx??????xxxxxxxxxxxxx");
    FindFunc<HookFunc::Global_DrawOpaqueRenderable>("\x55\x8B\xEC\x83\xEC\x20\x8B\x0D????\x53\x56\x33\xF6",
                                                    "xxxxxxxx????xxxx");
    FindFunc<HookFunc::Global_DrawTranslucentRenderable>(
        "\x55\x8B\xEC\x83\xEC\x0C\x53\x8B\x5D\x08\x8B\xCB\x8B\x03\xFF\x50\x34", "xxxxxxxxxxxxxxxxx");
    FindFunc<HookFunc::Global_GetLocalPlayerIndex>(
        "\x48\x83\xEC\x28\xE8\x00\x00\x00\x00\x48\x85\xC0\x74\x00\x48\x8D\x48\x10\x48\x8B\x40\x10\x48\x83\xC4\x28\x48"
        "\xFF\x60\x48\x48\x83\xC4\x28\xC3",
        "xxxxx????xxxx?xxxxxxxxxxxxxxxxxxxxx");
    FindFunc<HookFunc::Global_GetSequenceName>(
        "\x55\x8B\xEC\x56\x8B\x75\x08\x57\x85\xF6\x74\x5C\x8B\x7D\x0C\x85\xFF\x78\x1A\x8B\xCE\xE8????"
        "\x3B\xF8\x7D\x0F\x57\x8B\xCE\xE8????\x5F\x5E\x03\x40\x04",
        "xxxxxxxxxxxxxxxxxxxxxx????xxxxxxxx????xxxxx");
    FindFunc<HookFunc::Global_GetVectorInScreenSpace>("\x55\x8B\xEC\x8B\x45\x1C\x83\xEC\x10", "xxxxxxxxx");
    FindFunc<HookFunc::Global_UserInfoChangedCallback>(
        "\x48\x89\x6C\x24\x18\x56\x48\x83\xEC\x20\x48\x8B\x74\x24\x50\x41\x8B\xE8\x48\x85\xF6\x0F\x00\x00\x00\x00\x00",
        "xxxxxxxxxxxxxxxxxxxxxx?????", 0, "engine");
    FindFunc<HookFunc::Global_UTILComputeEntityFade>(
        "\x4C\x8B\xDC\x49\x89\x73\x18\x55\x49\x8D\x6B\xA1\x48\x81\xEC\xB0\x00\x00\x00", "xxxxxxxxxxxxxxxxxxx");
    FindFunc<HookFunc::Global_UTIL_TraceLine>("\x4C\x8B\xDC\x49\x89\x5B\x08\x57\x48\x81\xEC\xB0\x00\x00\x00\xF3\x0F\x10"
                                              "\x42\x08\x49\x8B\xC1\xF3\x0F\x10\x19\x41\x8B\xF8\xF3\x0F\x10\x0A",
                                              "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx");

    FindFunc<HookFunc::C_BaseAnimating_ComputeHitboxSurroundingBox>(
        "\x55\x8B\xEC\x81\xEC????\x56\x8B\xF1\x57\x80\xBE?????\x0F\x85????\x83\xBE?????\x75\x05\xE8????\x8B\xBE????"
        "\x85\xFF\x0F\x84????\x8B\x86????\x8B\x17\x53",
        "xxxxx????xxxxxx?????xx????xx?????xxx????xx????xxxx????xx????xxx");
    FindFunc<HookFunc::C_BaseAnimating_DoInternalDrawModel>("\x55\x8B\xEC\x8B\x55\x0C\x83\xEC\x30\x56", "xxxxxxxxxx");
    FindFunc<HookFunc::C_BaseAnimating_DrawModel>("\x55\x8B\xEC\x83\xEC\x20\x8B\x15????\x53\x33\xDB\x56",
                                                  "xxxxxxxx????xxxx");
    FindFunc<HookFunc::C_BaseAnimating_GetBoneCache>("\x48\x89\x5C\x24\x08\x48\x89\x74\x24\x10\x57\x48\x83\xEC\x50\x48"
                                                     "\x8B\xF9\x48\x8B\xF2\x48\x8B\x89\xA0\x0B\x00\x00",
                                                     "xxxxxxxxxxxxxxxxxxxxxxxxxxxx");
    FindFunc<HookFunc::C_BaseAnimating_GetBonePosition>(
        "\x48\x89\x5C\x24\x08\x48\x89\x6C\x24\x10\x48\x89\x74\x24\x18\x57\x48\x83\xEC\x50\x8B\xDA\x49\x8B\xF1\x33\xD2"
        "\x49\x8B\xE8\x48\x8B\xF9",
        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx");
    FindFunc<HookFunc::C_BaseAnimating_GetSequenceActivityName>("\x55\x8B\xEC\x83\x7D\x08\xFF\x56\x8B\xF1\x75\x0A",
                                                                "xxxxxxxxxxxx");
    FindFunc<HookFunc::C_BaseAnimating_LockStudioHdr>(
        "\x48\x8B\xC4\x56\x48\x83\xEC\x70\x48\x89\x58\x08\x48\x8D\x91\x00\x0C\x00\x00\x48\x8B\xD9",
        "xxxxxxxxxxxxxxxxxxxxxx");
    FindFunc<HookFunc::C_BaseAnimating_LookupBone>("\x40\x53\x48\x83\xEC\x20\x48\x8B\xDA\xE8\x00\x00\x00\x00\x48\x8B"
                                                   "\xC8\x48\x8B\xD3\x48\x83\xC4\x20\x5B\xE9\x00\x00\x00\x00",
                                                   "xxxxxxxxxx????xxxxxxxxxxxx????");

    FindFunc<HookFunc::C_BaseEntity_Init>("\x48\x89\x5C\x24\x08\x48\x89\x74\x24\x18\x57\x48\x83\xEC\x30\x48\x8B\x01\x41"
                                          "\x8B\xF0\x8B\xFA\x89\x91\x88\x00\x00\x00",
                                          "xxxxxxxxxxxxxxxxxxxxxxxxxxxxx");
    FindFunc<HookFunc::C_BaseEntity_CalcAbsolutePosition>("\x55\x8B\xEC\x81\xEC????\x80\x3D?????\x53\x8B\xD9\x0F\x84",
                                                          "xxxxx????xx?????xxxxx");

    FindFunc<HookFunc::C_BasePlayer_GetDefaultFOV>("\x57\x8B\xF9\x8B\x07\xFF\x90????\x83\xF8\x04", "xxxxxxx????xxx");
    FindFunc<HookFunc::C_BasePlayer_GetFOV>("\x55\x8B\xEC\x83\xEC\x10\x56\x8B\xF1\x8B\x0D????\x8B\x01",
                                            "xxxxxxxxxxx????xx");
    FindFunc<HookFunc::C_BasePlayer_ShouldDrawThisPlayer>(
        "\x48\x83\xEC\x28\xE8\x00\x00\x00\x00\x84\xC0\x74\x00\x48\x00\x00\x00\x00\x00\x00\x48\x85\xC9\x74\x00\x48\x8B"
        "\x01\xFF\x50\x30",
        "xxxxx????xxx?x??????xxxx?xxxxxx");

    FindFunc<HookFunc::C_HLTVCamera_SetMode>(
        "\x48\x89\x74\x24\x10\x57\x48\x83\xEC\x20\x8B\x71\x10\x48\x8B\xF9\x3B\xF2\x74\x00\x89\x51\x10\x45\x33\xC0\x48"
        "\x00\x00\x00\x00\x00\x00\x48\x00\x00\x00\x00\x00\x00\x48\x00\x00\x00\x00\x48\x8B\x01\xFF\x50\x00\x48\x8B\xD8"
        "\x48\x85\xC0\x74\x00",
        "xxxxxxxxxxxxxxxxxxx?xxxxxxx??????x??????x????xxxxx?xxxxxxx?");
    FindFunc<HookFunc::C_HLTVCamera_SetPrimaryTarget>(
        "\x48\x89\x5C\x24\x20\x55\x48\x83\xEC\x40\x8B\x69\x30\x48\x8B\xD9\x3B\xEA\x0F\x84\x00\x00\x00\x00\x89\x51\x30"
        "\x8B\x49\x14\x48\x89\x74\x24\x50\x48\x89\x7C\x24\x60\x85\xC9\x7E\x00\xE8\x00\x00\x00\x00\x48\x85\xC0\x74\x00",
        "xxxxxxxxxxxxxxxxxxxx????xxxxxxxxxxxxxxxxxxx?x????xxxx?");

    FindFunc<HookFunc::C_TFPlayer_DrawModel>("\x55\x8B\xEC\x51\x57\x8B\xF9\x80\x7F\x54\x17", "xxxxxxxxxxx");
    FindFunc<HookFunc::C_TFPlayer_GetEntityForLoadoutSlot>(
        "\x48\x89\x5C\x24\x08\x48\x89\x6C\x24\x10\x48\x89\x74\x24\x18\x57\x41\x54\x41\x55\x41\x56\x41\x57\x48\x83\xEC"
        "\x30\x8D\x42\xF9\x33\xF6\x44\x8B\xE2\x4C\x8B\xE9\x41\xBE\xFF\x1F\x00\x00",
        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx");

    FindFunc<HookFunc::C_TFWeaponBase_PostDataUpdate>(
        "\x48\x89\x5C\x24\x08\x48\x89\x6C\x24\x10\x48\x89\x74\x24\x18\x57\x48\x83\xEC\x20\x48\x8B\xF1\x8B\xEA\x48\x83"
        "\xC1\xF0\xE8\x00\x00\x00\x00\x48\x8B\xF8\x48\x85\xC0",
        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx????xxxxxx");

    FindFunc<HookFunc::CAccountPanel_OnAccountValueChanged>("\x55\x8B\xEC\x51\x53\x8B\x5D\x0C\x56\x8B\xF1\x53",
                                                            "xxxxxxxxxxxx");
    FindFunc<HookFunc::CAccountPanel_Paint>("\x55\x8B\xEC\x83\xEC\x74\x56\x8B\xC1", "xxxxxxxxx");
    FindFunc<HookFunc::CBaseClientRenderTargets_InitClientRenderTargets>(
        "\x55\x8B\xEC\x51\x53\x8B\x5D\x08\x56\x57\x6A\x01", "xxxxxxxxxxxx");
    FindFunc<HookFunc::CDamageAccountPanel_FireGameEvent>("\x48\x89\x5C\x24\x10\x48\x89\x6C\x24\x18\x56\x57\x41\x54\x41"
                                                          "\x56\x41\x57\x48\x81\xEC\xC0\x00\x00\x00\x48\x8B\x02",
                                                          "xxxxxxxxxxxxxxxxxxxxxxxxxxxx");
    FindFunc<HookFunc::CDamageAccountPanel_ShouldDraw>(
        "\x40\x53\x48\x83\xEC\x20\x48\x8B\xD9\xE8\x00\x00\x00\x00\x48\x85\xC0\x74\x00\x48\x8B\x10\x48\x8B\xC8\xFF\x92"
        "\x10\x04\x00\x00\x84\xC0\x75\x00\xC7\x83\x98\x02\x00\x00\x00\x00\x00\x00\x48\x00\x00\x00\x00\x00\x00\x83\x78"
        "\x58\x00\x74\x00\xB0\x01\x48\x83\xC4\x20",
        "xxxxxxxxxx????xxxx?xxxxxxxxxxxxxxx?xxxxxxxxxxx??????xxxxx?xxxxxx");

    FindFunc<HookFunc::CParticleProperty_DebugPrintEffects>(
        "\x55\x8B\xEC\x51\x8B\xC1\x53\x56\x33\xF6\x89\x45\xFC\x8B\x58\x14", "xxxxxxxxxxxxxxxx");
    FindFunc<HookFunc::CNewParticleEffect_StopEmission>("\x55\x8B\xEC\x53\x8B\x5D\x08\x56\x8B\xF1\x83\xBE?????",
                                                        "xxxxxxxxxxxx?????");

    FindFunc<HookFunc::CGlowObjectManager_ApplyEntityGlowEffects>(
        "\x48\x8B\xC4\x48\x89\x58\x18\x48\x89\x50\x10\x55\x56\x57\x41\x54\x41\x55\x41\x56\x41\x57\x48\x81\xEC\xB0\x00"
        "\x00\x00\x0F\x29\x70\xB8",
        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx");

    FindFunc<HookFunc::CHudBaseDeathNotice_GetIcon>(
        "\x40\x53\x48\x81\xEC\x20\x01\x00\x00\x48\x8B\xDA\x45\x85\xC0\x74\x00\x41\xB8\x02\x00\x00\x00",
        "xxxxxxxxxxxxxxxx?xxxxxx");

    FindFunc<HookFunc::CStudioHdr_GetNumSeq>(
        "\x48\x8B\x41\x08\x48\x85\xC0\x75\x00\x48\x8B\x01\x8B\x80\xBC\x00\x00\x00\xC3", "xxxxxxxx?xxxxxxxxxx");
    FindFunc<HookFunc::CStudioHdr_pSeqdesc>(
        "\x48\x89\x74\x24\x10\x57\x48\x83\xEC\x20\x33\xF6\x48\x8D\x79\x08\x44\x8B\xCA\x85\xD2\x78\x00\x4C\x8B\x07",
        "xxxxxxxxxxxxxxxxxxxxxx?xxx");

    FindFunc<HookFunc::CViewRender_PerformScreenSpaceEffects>(
        "\x4C\x8B\xDC\x49\x89\x5B\x08\x49\x89\x6B\x10\x49\x89\x73\x18\x49\x89\x7B\x20\x41\x56\x48\x83\xEC\x60\x48\x00"
        "\x00\x00\x00\x00\x00\x33\xFF\x49\x89\x7B\xE8\x41\x8B\xF1\x41\x8B\xE8\x44\x8B\xF2",
        "xxxxxxxxxxxxxxxxxxxxxxxxxx??????xxxxxxxxxxxxxxx");

    FindFunc<HookFunc::CVTFTexture_GetResourceData>("\x55\x8B\xEC\x8B\x45\x08\x56\x25", "xxxxxxxx", 0,
                                                    "MaterialSystem");
    FindFunc<HookFunc::CVTFTexture_ReadHeader>("\x55\x8B\xEC\x56\x8B\x75\x0C\x57\x6A\x50", "xxxxxxxxxx", 0,
                                               "MaterialSystem");

    FindFunc<HookFunc::IGameSystem_Add>(
        "\x48\x89\x5C\x24\x08\x48\x89\x74\x24\x10\x57\x48\x83\xEC\x00\x8B\x00\x00\x00\x00\x00\x48\x8B\xF9\x8B\x00\x00"
        "\x00\x00\x00\x8B\xDA\xBE\x04\x00\x00\x00\x44\x8D\x42\x01\x44\x3B\xC0\x0F\x8E\x00\x00\x00\x00",
        "xxxxxxxxxxxxxx?x?????xxxx?????xxxxxxxxxxxxxxxx????");

    FindFunc<HookFunc::vgui_AnimationController_StartAnimationSequence>(
        "\x44\x88\x4C\x24\x20\x53\x41\x54\x41\x55\x41\x56\x48\x83\xEC\x58\x80\xB9\xEB\x01\x00\x00\x00\x49\x8B\xD8\x4C"
        "\x8B\xEA\x4C\x8B\xF1\x74\x00\xE8\x00\x00\x00\x00\x4C\x8B\xC3",
        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx?x????xxx");
    FindFunc<HookFunc::vgui_Panel_FindChildByName>(
        "\x48\x8B\xC4\x53\x48\x81\xEC\xA0\x00\x00\x00\x48\x89\x70\xF0\x33\xF6\x48\x89\x78\xE8\x4C\x89\x60\xE0\x4C\x8B"
        "\xE1\x4C\x89\x68\xD8\x45\x0F\xB6\xE8",
        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx");
    FindFunc<HookFunc::vgui_ProgressBar_ApplySettings>(
        "\x48\x89\x5C\x24\x08\x48\x89\x74\x24\x10\x57\x48\x83\xEC\x20\x48\x8B\xF2\x48\x8B\xF9\x48\x8B\xCE\x48\x00\x00"
        "\x00\x00\x00\x00\x0F\x57\xD2\xE8\x00\x00\x00\x00\x4C\x00\x00\x00\x00\x00\x00\xF3\x0F\x11\x87\xF4\x01\x00\x00",
        "xxxxxxxxxxxxxxxxxxxxxxxxx??????xxxx????x??????xxxxxxxx");

    FindFunc<HookFunc::CStorePanel_RequestPricesheet>(
        "\x40\x57\x48\x83\xEC\x20\xE8\x00\x00\x00\x00\x48\x8B\xC8\x48\x8B\x10\xFF\x52\x58\xB9\x88\x01\x00\x00\xE8\x00"
        "\x00\x00\x00\x48\x8B\xF8\x48\x85\xC0\x74\x00\x48\x89\x5C\x24\x30\xE8\x00\x00\x00\x00\x48\x8B\xC8\xE8\x00\x00"
        "\x00\x00\x45\x33\xC0\x48\x8B\xCF\x48\x8B\xD8\x48\x8D\x50\x20\xE8\x00\x00\x00\x00\x48\x00\x00\x00\x00\x00\x00"
        "\x48\x89\x9F\x70\x01\x00\x00\x48\x8B\x5C\x24\x30\x33\xD2",
        "xxxxxxx????xxxxxxxxxxxxxxx????xxxxxxx?xxxxxx????xxxx????xxxxxxxxxxxxxx????x??????xxxxxxxxxxxxxx");

    FindFunc<HookFunc::CParticleProperty_OwnerSetDormantTo>(
        "\x48\x8B\x4E\x10\x40\x0F\xB6\xD5\x48\x8B\x4C\x0B\x20\xE8\x00\x00\x00\x00\x48\x8D\x5B\x28\x48\x83\xEF\x01",
        "xxxxxxxxxxxxxx????xxxxxxxx");

    FindFunc<HookFunc::C_BaseAnimating_GetSequenceActivity>(
        "\x48\x89\x5C\x24\x08\x57\x48\x83\xEC\x20\x8B\xDA\x48\x8B\xF9\x83\xFA\xFF\x74", "xxxxxxxxxxxxxxxxxxx");

    FindFunc_C_BasePlayer_GetLocalPlayer();
    FindFunc_CNewParticleEffect_SetDormant();
}

template<HookFunc fn>
void HookManager::FindFunc(const char* signature, const char* mask, int offset, const char* module)
{
    std::byte* result = (std::byte*)SignatureScan(module, signature, mask);
    if (result)
    {
        result += offset;
    }
    else
    {
        Assert(!"Failed to find function!");
        result = nullptr;
    }

    s_RawFunctions[(int)fn] = (void*)result;
}

void HookManager::IngameStateChanged(bool inGame)
{
    if (inGame)
    {
        GetHook<HookFunc::IGameEventManager2_FireEventClientSide>()->AttachHook(
            std::make_shared<HookFuncType<HookFunc::IGameEventManager2_FireEventClientSide>::Hook::Inner>(
                Interfaces::GetGameEventManager(), &IGameEventManager2::FireEventClientSide));
    }
    else
    {
        GetHook<HookFunc::IGameEventManager2_FireEventClientSide>()->DetachHook();
    }
}

template<HookFunc fn, class... Args>
void HookManager::InitHook(Args&&... args)
{
    Assert(!m_Hooks[(int)fn]);
    m_Hooks[(int)fn] = std::make_unique<HookFuncType<fn>::Hook>();
    GetHook<fn>()->AttachHook(std::make_shared<HookFuncType<fn>::Hook::Inner>(args...));
}

template<HookFunc fn>
void HookManager::InitGlobalHook()
{
    InitHook<fn>(GetRawFunc<fn>());
}

HookManager::HookManager()
{
    InitRawFunctionsList();

    Assert(!s_HookManager);
    m_Panel.reset(new Panel());

    InitHook<HookFunc::ICvar_ConsoleColorPrintf>(g_pCVar, &ICvar::ConsoleColorPrintf);
    InitHook<HookFunc::ICvar_ConsoleDPrintf>(g_pCVar, &ICvar::ConsoleDPrintf);
    InitHook<HookFunc::ICvar_ConsolePrintf>(g_pCVar, &ICvar::ConsolePrintf);

    InitHook<HookFunc::IClientEngineTools_InToolMode>(Interfaces::GetClientEngineTools(),
                                                      &IClientEngineTools::InToolMode);
    InitHook<HookFunc::IClientEngineTools_IsThirdPersonCamera>(Interfaces::GetClientEngineTools(),
                                                               &IClientEngineTools::IsThirdPersonCamera);
    InitHook<HookFunc::IClientEngineTools_SetupEngineView>(Interfaces::GetClientEngineTools(),
                                                           &IClientEngineTools::SetupEngineView);

    // Just construct but don't init, gameeventmanager isn't instantiated until we load a map
    m_Hooks[(int)HookFunc::IGameEventManager2_FireEventClientSide] =
        std::make_unique<HookFuncType<HookFunc::IGameEventManager2_FireEventClientSide>::Hook>();

    InitHook<HookFunc::IVEngineClient_GetPlayerInfo>(Interfaces::GetEngineClient(), &IVEngineClient::GetPlayerInfo);

    InitHook<HookFunc::IPrediction_PostEntityPacketReceived>(Interfaces::GetPrediction(),
                                                             &IPrediction::PostEntityPacketReceived);

    InitHook<HookFunc::IStaticPropMgrClient_ComputePropOpacity>(Interfaces::GetStaticPropMgr(),
                                                                &IStaticPropMgrClient::ComputePropOpacity);

    InitHook<HookFunc::IStudioRender_ForcedMaterialOverride>(g_pStudioRender, &IStudioRender::ForcedMaterialOverride);

    InitHook<HookFunc::C_HLTVCamera_SetMode>(Interfaces::GetHLTVCamera(), GetRawFunc<HookFunc::C_HLTVCamera_SetMode>());
    InitHook<HookFunc::C_HLTVCamera_SetPrimaryTarget>(Interfaces::GetHLTVCamera(),
                                                      GetRawFunc<HookFunc::C_HLTVCamera_SetPrimaryTarget>());

    InitGlobalHook<HookFunc::C_BaseAnimating_DoInternalDrawModel>();
    InitGlobalHook<HookFunc::C_BaseAnimating_DrawModel>();
    // InitGlobalHook<HookFunc::C_BaseAnimating_InternalDrawModel>();
    InitGlobalHook<HookFunc::C_BasePlayer_GetDefaultFOV>();
    InitGlobalHook<HookFunc::C_BasePlayer_ShouldDrawThisPlayer>();
    InitGlobalHook<HookFunc::C_TFPlayer_DrawModel>();
    InitGlobalHook<HookFunc::C_TFWeaponBase_PostDataUpdate>();

    InitGlobalHook<HookFunc::C_BaseEntity_Init>();

    InitGlobalHook<HookFunc::CGlowObjectManager_ApplyEntityGlowEffects>();

    InitGlobalHook<HookFunc::CAccountPanel_OnAccountValueChanged>();
    InitGlobalHook<HookFunc::CAccountPanel_Paint>();
    InitGlobalHook<HookFunc::CBaseClientRenderTargets_InitClientRenderTargets>();
    InitGlobalHook<HookFunc::CDamageAccountPanel_FireGameEvent>();
    InitGlobalHook<HookFunc::CDamageAccountPanel_ShouldDraw>();

    InitGlobalHook<HookFunc::CViewRender_PerformScreenSpaceEffects>();

    InitGlobalHook<HookFunc::CVTFTexture_GetResourceData>();
    InitGlobalHook<HookFunc::CVTFTexture_ReadHeader>();

    InitGlobalHook<HookFunc::Global_CreateEntityByName>();
    InitGlobalHook<HookFunc::Global_DrawOpaqueRenderable>();
    InitGlobalHook<HookFunc::Global_DrawTranslucentRenderable>();
    InitGlobalHook<HookFunc::Global_GetLocalPlayerIndex>();
    InitGlobalHook<HookFunc::Global_GetVectorInScreenSpace>();
    InitGlobalHook<HookFunc::Global_UserInfoChangedCallback>();
    InitGlobalHook<HookFunc::Global_UTILComputeEntityFade>();
    InitGlobalHook<HookFunc::Global_UTIL_TraceLine>();

    InitGlobalHook<HookFunc::vgui_Panel_FindChildByName>();
    InitGlobalHook<HookFunc::vgui_ProgressBar_ApplySettings>();

    InitGlobalHook<HookFunc::CStorePanel_RequestPricesheet>();
}
