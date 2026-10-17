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

#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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

namespace
{
struct ModuleRange
{
    std::byte* m_Base = nullptr;
    size_t m_Size = 0;

    bool Contains(const void* address, size_t length = 1) const
    {
        return address >= m_Base && (const std::byte*)address + length <= m_Base + m_Size;
    }
};

struct SignatureStatus
{
    std::string m_Name;
    std::string m_Module;
    float m_Score; // 1 for an exact match, 0 if not found, otherwise the fraction of an approximate match
};
}

static ModuleRange GetModuleRange(const char* moduleName)
{
    const HMODULE module = GetModuleHandle((std::string(moduleName) + ".dll").c_str());

    MODULEINFO info;
    if (!module || !GetModuleInformation(GetCurrentProcess(), module, &info, sizeof(info)))
        return {};

    return {(std::byte*)info.lpBaseOfDll, info.SizeOfImage};
}

bool IsAddressInModule(const char* moduleName, const void* address, size_t length)
{
    return GetModuleRange(moduleName).Contains(address, length);
}

static void* FindPattern(uintptr_t address, size_t size, BYTE* pbSig, const char* szMask)
{
    const size_t length = strlen(szMask);
    if (size < length)
        return nullptr;

    for (uintptr_t i = NULL; i <= size - length; i++)
    {
        if (DataCompare((BYTE*)(address + i), pbSig, szMask))
            return (void*)(address + i);
    }

    return nullptr;
}

// Game updates usually change just a few operands (struct offsets, allocation sizes) in otherwise identical code. When
// an exact match fails, accept the one place that matches nearly all of the pattern's fixed bytes and clearly beats
// every other candidate. Calibrated against the real game binaries: this recovers both signatures broken by the October
// 17th 2026 update (one of which has a sibling function matching 82% of it), while no stale signature gets near.
static constexpr size_t APPROX_MIN_FIXED_BYTES = 24;
static constexpr float APPROX_MIN_SCORE = 0.9f;
static constexpr float APPROX_MIN_MARGIN = 0.1f;

static std::byte* FindApproximatePattern(const ModuleRange& module, const BYTE* sig, const char* mask, float& score)
{
    score = 0;

    const size_t length = strlen(mask);
    std::vector<size_t> fixed;
    for (size_t i = 0; i < length; i++)
    {
        if (mask[i] == 'x')
            fixed.push_back(i);
    }

    if (fixed.size() < APPROX_MIN_FIXED_BYTES || module.m_Size < length + sizeof(uint32_t))
        return nullptr;

    // Every run of 4 fixed bytes is an anchor. Anywhere one of them matches exactly is a candidate.
    std::unordered_multimap<uint32_t, size_t> anchors;
    std::vector<bool> maybeAnchor(1 << 16);
    for (size_t i = 0; i + sizeof(uint32_t) <= length; i++)
    {
        if (strncmp(mask + i, "xxxx", 4))
            continue;

        uint32_t value;
        memcpy(&value, sig + i, sizeof(value));
        anchors.emplace(value, i);
        maybeAnchor[value & 0xFFFF] = true;
    }

    std::unordered_set<size_t> candidates;
    for (size_t pos = 0; pos + sizeof(uint32_t) <= module.m_Size; pos++)
    {
        uint32_t value;
        memcpy(&value, module.m_Base + pos, sizeof(value));
        if (!maybeAnchor[value & 0xFFFF])
            continue;

        const auto [first, last] = anchors.equal_range(value);
        for (auto it = first; it != last; ++it)
        {
            if (pos >= it->second && pos - it->second + length <= module.m_Size)
                candidates.insert(pos - it->second);
        }
    }

    size_t best = 0;
    size_t second = 0;
    size_t bestPos = 0;
    for (const auto candidate : candidates)
    {
        size_t matches = 0;
        for (const auto i : fixed)
            matches += (BYTE)module.m_Base[candidate + i] == sig[i];

        if (matches > best)
        {
            second = best;
            best = matches;
            bestPos = candidate;
        }
        else if (matches > second)
        {
            second = matches;
        }
    }

    score = float(best) / fixed.size();
    if (best < APPROX_MIN_SCORE * fixed.size() || best - second < (std::max)(4.0f, APPROX_MIN_MARGIN * fixed.size()))
        return nullptr;

    return module.m_Base + bestPos;
}

static std::mutex s_SignatureStatusesMutex;
static std::vector<SignatureStatus> s_SignatureStatuses;

void RecordSignatureStatus(const char* name, const char* moduleName, float score)
{
    std::lock_guard lock(s_SignatureStatusesMutex);

    for (auto& status : s_SignatureStatuses)
    {
        if (status.m_Name == name)
        {
            status.m_Score = score;
            return;
        }
    }

    s_SignatureStatuses.push_back({name, moduleName, score});
}

static ConCommand ce_signatures_status(
    "ce_signatures_status",
    []() {
        std::lock_guard lock(s_SignatureStatusesMutex);

        size_t approximate = 0;
        size_t missing = 0;
        for (const auto& status : s_SignatureStatuses)
        {
            if (status.m_Score >= 1)
            {
                ConColorMsg(Color(100, 255, 100, 255), "  found        ");
            }
            else if (status.m_Score > 0)
            {
                ConColorMsg(Color(255, 200, 0, 255), "  approx %3.0f%% ", status.m_Score * 100);
                approximate++;
            }
            else
            {
                ConColorMsg(Color(255, 80, 80, 255), "  NOT FOUND    ");
                missing++;
            }

            Msg("%s (%s.dll)\n", status.m_Name.c_str(), status.m_Module.c_str());
        }

        PluginMsg("%zu game functions/variables looked up: %zu approximate (still work, but their signatures should be "
                  "updated), %zu not found (features relying on them are disabled).\n",
                  s_SignatureStatuses.size(), approximate, missing);
    },
    "Lists the game functions and variables CastingEssentials looks up, and whether each was found. Use this after a "
    "game update to see what broke.");

#pragma warning(push)
#pragma warning(disable : 4706) // Assignment within conditional expression
std::byte* SignatureScanMultiple(const char* moduleName, const char* signature, const char* mask,
                                 const std::function<bool(std::byte* found)>& testFunc, int offset)
{
    const auto module = GetModuleRange(moduleName);
    if (!module.m_Base)
        return nullptr;

    const auto base = reinterpret_cast<uintptr_t>(module.m_Base);
    uintptr_t searchOffset = 0;
    std::byte* found;
    while (searchOffset < module.m_Size &&
           (found = (std::byte*)FindPattern(base + searchOffset, module.m_Size - searchOffset, (PBYTE)signature, mask)))
    {
        if (testFunc(found + offset))
            return found + offset;

        searchOffset = reinterpret_cast<uintptr_t>(found) - base + 1;
    }

    return nullptr;
}
#pragma warning(pop)

std::byte* SignatureScan(const char* moduleName, const char* signature, const char* mask, int offset, const char* name)
{
    const auto module = GetModuleRange(moduleName);
    if (!module.m_Base)
        return nullptr;

    auto found = (std::byte*)FindPattern(reinterpret_cast<uintptr_t>(module.m_Base), module.m_Size, (PBYTE)signature,
                                         mask);
    float score = 1;
    if (!found)
    {
        found = FindApproximatePattern(module, (const BYTE*)signature, mask, score);
        if (found)
        {
            PluginWarning("%s was found in %s.dll by approximate match (%.0f%% of its signature). It works for now, "
                          "but its signature should be updated.\n",
                          name ? name : "A game function", moduleName, score * 100);
        }
        else
        {
            score = 0;
            if (name)
            {
                PluginWarning("Unable to find %s in %s.dll, likely due to a game update. Features relying on it will "
                              "not work.\n",
                              name, moduleName);
            }
        }
    }

    if (name)
        RecordSignatureStatus(name, moduleName, score);

    if (found)
        found += offset;

    return found;
}

std::byte* FindModuleString(const char* moduleName, const char* str)
{
    const auto module = GetModuleRange(moduleName);
    const auto length = strlen(str) + 1; // Including the null terminator, so we don't match the start of a longer string
    if (!module.m_Base || module.m_Size <= length)
        return nullptr;

    const auto end = module.m_Base + module.m_Size;
    for (auto it = module.m_Base + 1;
         (it = std::search(it, end, (const std::byte*)str, (const std::byte*)str + length)) != end; ++it)
    {
        // Also require a null before it, so we don't match the end of a longer string
        if (it[-1] == std::byte{0})
            return it;
    }

    return nullptr;
}

std::byte* FindRipRelativeReference(const char* moduleName, const void* target)
{
    const auto module = GetModuleRange(moduleName);
    if (!module.m_Base || !module.Contains(target))
        return nullptr;

    // Any 4 bytes can happen to resolve to the target, so also require them to be the operand of a lea r64,
    // [rip+disp32], which is how code takes the address of something.
    for (auto p = (const uint8_t*)module.m_Base + 3; p + sizeof(int32_t) <= (const uint8_t*)module.m_Base + module.m_Size;
         p++)
    {
        if ((p[-3] & 0xFB) != 0x48 || p[-2] != 0x8D || !IsRipRelativeModRM(p[-1]))
            continue;

        if (ResolveRipRelative(p) == target)
            return (std::byte*)p;
    }

    return nullptr;
}

void HookManager::FindFunc_CNewParticleEffect_SetDormant()
{
    // Too short to signature scan for -- find a usage and calculate its address from there.
    auto ownerSetDormantToFn = reinterpret_cast<uintptr_t>(GetRawFunc<HookFunc::CParticleProperty_OwnerSetDormantTo>());
    if (!ownerSetDormantToFn)
        return;

    static constexpr uintptr_t offset_to_call_instruction = 13;
    auto setDormantCall = ownerSetDormantToFn + offset_to_call_instruction;

    const bool found = *reinterpret_cast<uint8_t*>(setDormantCall) == 0xE8; // call rel32
    RecordSignatureStatus("CNewParticleEffect_SetDormant", "client", found ? 1.0f : 0.0f);
    if (!found)
    {
        PluginWarning("Unable to find CNewParticleEffect_SetDormant in client.dll, likely due to a game update. "
                      "Features relying on it will not work.\n");
        return;
    }

    s_RawFunctions[(int)HookFunc::CNewParticleEffect_SetDormant] = ResolveRipRelative((std::byte*)setDormantCall + 1);
}

void HookManager::InitRawFunctionsList()
{
    FindFunc<HookFunc::Global_CreateTFGlowObject>(
        "\x48\x89\x5C\x24\x10\x48\x89\x74\x24\x18\x57\x48\x83\xEC\x20\x8B\xF1\x8B\xFA\xB9\xD8\x07\x00\x00\xE8\x00\x00"
        "\x00\x00\x48\x8B\xD8\x48\x85\xC0\x0F\x00\x00\x00\x00\x00\x48\x8B\xC8\x48\x89\x6C\x24\x30\xE8\x00\x00\x00\x00"
        "\x48\x00\x00\x00\x00\x00\x00\x33\xED\x48\x89\x03\x44\x8B\xC7\x48\x00\x00\x00\x00\x00\x00\x48\x89\xAB\xC8\x07"
        "\x00\x00\x48\x89\x43\x08\x8B\xD6",
        "xxxxxxxxxxxxxxxxxxxxxxxxx????xxxxxxx?????xxxxxxxxx????x??????xxxxxxxxx??????xxxxxxxxxxxxx");
    FindFunc<HookFunc::Global_GetLocalPlayerIndex>(
        "\x48\x83\xEC\x28\xE8\x00\x00\x00\x00\x48\x85\xC0\x74\x00\x48\x8D\x48\x10\x48\x8B\x40\x10\x48\x83\xC4\x28\x48"
        "\xFF\x60\x48\x48\x83\xC4\x28\xC3",
        "xxxxx????xxxx?xxxxxxxxxxxxxxxxxxxxx");
    FindFunc<HookFunc::Global_UserInfoChangedCallback>(
        "\x48\x89\x6C\x24\x18\x56\x48\x83\xEC\x20\x48\x8B\x74\x24\x50\x41\x8B\xE8\x48\x85\xF6\x0F\x00\x00\x00\x00\x00",
        "xxxxxxxxxxxxxxxxxxxxxx?????", 0, "engine");
    FindFunc<HookFunc::Global_UTILComputeEntityFade>(
        "\x4C\x8B\xDC\x49\x89\x73\x18\x55\x49\x8D\x6B\xA1\x48\x81\xEC\xB0\x00\x00\x00", "xxxxxxxxxxxxxxxxxxx");
    FindFunc<HookFunc::Global_UTIL_TraceLine>("\x4C\x8B\xDC\x49\x89\x5B\x08\x57\x48\x81\xEC\xB0\x00\x00\x00\xF3\x0F\x10"
                                              "\x42\x08\x49\x8B\xC1\xF3\x0F\x10\x19\x41\x8B\xF8\xF3\x0F\x10\x0A",
                                              "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx");

    FindFunc<HookFunc::C_BaseAnimating_GetBoneCache>("\x48\x89\x5C\x24\x08\x48\x89\x74\x24\x10\x57\x48\x83\xEC\x50\x48"
                                                     "\x8B\xF9\x48\x8B\xF2\x48\x8B\x89\xA0\x0B\x00\x00",
                                                     "xxxxxxxxxxxxxxxxxxxxxxxxxxxx");
    FindFunc<HookFunc::C_BaseAnimating_GetBonePosition>(
        "\x48\x89\x5C\x24\x08\x48\x89\x6C\x24\x10\x48\x89\x74\x24\x18\x57\x48\x83\xEC\x50\x8B\xDA\x49\x8B\xF1\x33\xD2"
        "\x49\x8B\xE8\x48\x8B\xF9",
        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx");
    FindFunc<HookFunc::C_BaseAnimating_LockStudioHdr>(
        "\x48\x8B\xC4\x56\x48\x83\xEC\x70\x48\x89\x58\x08\x48\x8D\x91\x00\x0C\x00\x00\x48\x8B\xD9",
        "xxxxxxxxxxxxxxxxxxxxxx");
    FindFunc<HookFunc::C_BaseAnimating_LookupBone>("\x40\x53\x48\x83\xEC\x20\x48\x8B\xDA\xE8\x00\x00\x00\x00\x48\x8B"
                                                   "\xC8\x48\x8B\xD3\x48\x83\xC4\x20\x5B\xE9\x00\x00\x00\x00",
                                                   "xxxxxxxxxx????xxxxxxxxxxxx????");

    FindFunc<HookFunc::C_BaseEntity_Init>("\x48\x89\x5C\x24\x08\x48\x89\x74\x24\x18\x57\x48\x83\xEC\x30\x48\x8B\x01\x41"
                                          "\x8B\xF0\x8B\xFA\x89\x91\x88\x00\x00\x00",
                                          "xxxxxxxxxxxxxxxxxxxxxxxxxxxxx");

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

    FindFunc<HookFunc::C_TFPlayer_GetEntityForLoadoutSlot>(
        "\x48\x89\x5C\x24\x08\x48\x89\x6C\x24\x10\x48\x89\x74\x24\x18\x57\x41\x54\x41\x55\x41\x56\x41\x57\x48\x83\xEC"
        "\x30\x8D\x42\xF9\x33\xF6\x44\x8B\xE2\x4C\x8B\xE9\x41\xBE\xFF\x1F\x00\x00",
        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx");

    FindFunc<HookFunc::C_TFWeaponBase_PostDataUpdate>(
        "\x48\x89\x5C\x24\x08\x48\x89\x6C\x24\x10\x48\x89\x74\x24\x18\x57\x48\x83\xEC\x20\x48\x8B\xF1\x8B\xEA\x48\x83"
        "\xC1\xF0\xE8\x00\x00\x00\x00\x48\x8B\xF8\x48\x85\xC0",
        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx????xxxxxx");

    FindFunc<HookFunc::CBaseClientRenderTargets_InitClientRenderTargets>(
        "\x48\x89\x5C\x24\x08\x48\x89\x6C\x24\x10\x48\x89\x74\x24\x18\x48\x89\x7C\x24\x20\x41\x56\x48\x83\xEC\x50\x48"
        "\x8B\x1A\x48\x8B\xE9\x48\x8B\xCA\x41\x8B\xF9\x48\x8B\xF2",
        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx");
    FindFunc<HookFunc::CDamageAccountPanel_FireGameEvent>("\x48\x89\x5C\x24\x10\x48\x89\x6C\x24\x18\x56\x57\x41\x54\x41"
                                                          "\x56\x41\x57\x48\x81\xEC\xC0\x00\x00\x00\x48\x8B\x02",
                                                          "xxxxxxxxxxxxxxxxxxxxxxxxxxxx");
    FindFunc<HookFunc::CDamageAccountPanel_ShouldDraw>(
        "\x40\x53\x48\x83\xEC\x20\x48\x8B\xD9\xE8\x00\x00\x00\x00\x48\x85\xC0\x74\x00\x48\x8B\x10\x48\x8B\xC8\xFF\x92"
        "\x00\x00\x00\x00\x84\xC0\x75\x00\xC7\x83",
        "xxxxxxxxxx????xxxx?xxxxxxxx????xxx?xx");

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

    FindFunc<HookFunc::IGameSystem_Add>(
        "\x48\x89\x5C\x24\x08\x48\x89\x74\x24\x10\x57\x48\x83\xEC\x00\x8B\x00\x00\x00\x00\x00\x48\x8B\xF9\x8B\x00\x00"
        "\x00\x00\x00\x8B\xDA\xBE\x04\x00\x00\x00\x44\x8D\x42\x01\x44\x3B\xC0\x0F\x8E\x00\x00\x00\x00",
        "xxxxxxxxxxxxxx?x?????xxxx?????xxxxxxxxxxxxxxxx????");

    FindFunc<HookFunc::vgui_AnimationController_StartAnimationSequence>(
        "\x44\x88\x4C\x24\x20\x53\x41\x54\x41\x55\x41\x56\x48\x83\xEC\x58\x80\xB9\x00\x00\x00\x00\x00\x49\x8B\xD8\x4C"
        "\x8B\xEA\x4C\x8B\xF1\x74\x00\xE8\x00\x00\x00\x00\x4C\x8B\xC3",
        "xxxxxxxxxxxxxxxxxx??xxxxxxxxxxxxx?x????xxx");
    FindFunc<HookFunc::vgui_Panel_FindChildByName>(
        "\x48\x8B\xC4\x53\x48\x81\xEC\xA0\x00\x00\x00\x48\x89\x70\xF0\x33\xF6\x48\x89\x78\xE8\x4C\x89\x60\xE0\x4C\x8B"
        "\xE1\x4C\x89\x68\xD8\x45\x0F\xB6\xE8",
        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx");
    FindFunc<HookFunc::vgui_ProgressBar_ApplySettings>(
        "\x48\x89\x5C\x24\x08\x48\x89\x74\x24\x10\x57\x48\x83\xEC\x20\x48\x8B\xF2\x48\x8B\xF9\x48\x8B\xCE\x48\x8D\x15"
        "\x00\x00\x00\x00\x0F\x57\xD2\xE8\x00\x00\x00\x00",
        "xxxxxxxxxxxxxxxxxxxxxxxxxxx????xxxx????");

    FindFunc<HookFunc::CStorePanel_RequestPricesheet>(
        "\x40\x57\x48\x83\xEC\x20\xE8\x00\x00\x00\x00\x48\x8B\xC8\x48\x8B\x10\xFF\x52\x58\xB9\x00\x00\x00\x00\xE8\x00"
        "\x00\x00\x00\x48\x8B\xF8\x48\x85\xC0\x74\x00\x48\x89\x5C\x24\x30\xE8\x00\x00\x00\x00\x48\x8B\xC8\xE8\x00\x00"
        "\x00\x00\x45\x33\xC0\x48\x8B\xCF\x48\x8B\xD8\x48\x8D\x50\x20\xE8\x00\x00\x00\x00\x48\x00\x00\x00\x00\x00\x00"
        "\x48\x89\x9F\x00\x00\x00\x00\x48\x8B\x5C\x24\x30\x33\xD2",
        "xxxxxxx????xxxxxxxxxx??xxx????xxxxxxx?xxxxxx????xxxx????xxxxxxxxxxxxxx????x??????xxx??xxxxxxxxx");

    FindFunc<HookFunc::CParticleProperty_OwnerSetDormantTo>(
        "\x48\x8B\x4E\x10\x40\x0F\xB6\xD5\x48\x8B\x4C\x0B\x20\xE8\x00\x00\x00\x00\x48\x8D\x5B\x28\x48\x83\xEF\x01",
        "xxxxxxxxxxxxxx????xxxxxxxx");

    FindFunc<HookFunc::C_BaseAnimating_GetSequenceActivity>(
        "\x48\x89\x5C\x24\x08\x57\x48\x83\xEC\x20\x8B\xDA\x48\x8B\xF9\x83\xFA\xFF\x74", "xxxxxxxxxxxxxxxxxxx");

    FindFunc_CNewParticleEffect_SetDormant();
}

template<HookFunc fn>
void HookManager::FindFunc(const char* signature, const char* mask, int offset, const char* module)
{
    static constexpr auto name = EnumValueName<fn>();
    static const std::string nameStr(name);

    s_RawFunctions[(int)fn] = SignatureScan(module, signature, mask, offset, nameStr.c_str());
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
    m_Hooks[(int)fn] = std::make_unique<typename HookFuncType<fn>::Hook>();
    GetHook<fn>()->AttachHook(std::make_shared<typename HookFuncType<fn>::Hook::Inner>(args...));
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
