#pragma once
#include "PluginBase/HookDefinitions.h"

#include <memory>

class HookManager final : HookDefinitions
{
    template<HookFunc fn>
    friend class Hook;

public:
    HookManager();

    static bool Load();
    static bool Unload();

    template<HookFunc fn>
    __forceinline typename HookFuncType<fn>::Hook::OriginalFnType GetFunc()
    {
        return GetHook<fn>()->GetOriginal();
    }
    template<HookFunc fn>
    __forceinline typename HookFuncType<fn>::Hook* GetHook()
    {
        return static_cast<HookFuncType<fn>::Hook*>(m_Hooks[(int)fn].get());
    };
    // Null if the function couldn't be found (e.g. a signature broken by a game update).
    template<HookFunc fn>
    static __forceinline typename HookFuncType<fn>::Raw GetRawFunc()
    {
        return (typename HookFuncType<fn>::Raw)s_RawFunctions[(int)fn];
    }

    // Like GetRawFunc, but if the function couldn't be found, returns a stand-in that does nothing and returns a
    // default value (0, false, nullptr), so callers degrade instead of crashing.
    template<HookFunc fn>
    static typename HookFuncType<fn>::Raw GetSafeRawFunc()
    {
        if (auto raw = GetRawFunc<fn>())
            return raw;

        return &MissingFunctionStub<typename HookFuncType<fn>::Raw>::Invoke;
    }

private:
    template<class Fn>
    struct MissingFunctionStub;
    template<class RetVal, class... Args>
    struct MissingFunctionStub<RetVal (*)(Args...)>
    {
        static_assert(!std::is_reference_v<RetVal>, "Can't stand in for a function returning a reference");

        static RetVal Invoke(Args...)
        {
            if constexpr (!std::is_void_v<RetVal>)
                return RetVal{};
        }
    };

public:

    template<HookFunc fn>
    inline int AddHook(const typename HookFuncType<fn>::Hook::Functional& hook)
    {
        auto hkPtr = GetHook<fn>();
        if (!hkPtr)
            return 0;

        return hkPtr->AddHook(hook);
    }
    template<HookFunc fn>
    inline bool RemoveHook(int hookID, const char* funcName)
    {
        auto hkPtr = GetHook<fn>();
        if (!hkPtr)
            return false;

        return hkPtr->RemoveHook(hookID, funcName);
    }
    template<HookFunc fn>
    inline typename HookFuncType<fn>::Hook::Functional GetOriginal()
    {
        auto hkPtr = GetHook<fn>();
        if (!hkPtr)
            return nullptr;

        return hkPtr->GetOriginal();
    }
    template<HookFunc fn>
    inline void SetState(Hooking::HookAction state)
    {
        auto hkPtr = GetHook<fn>();
        if (!hkPtr)
            return;

        hkPtr->SetState(state);
    }
    template<HookFunc fn>
    inline bool IsInHook()
    {
        auto hkPtr = GetHook<fn>();
        if (!hkPtr)
            return false;

        return hkPtr->IsInHook();
    }

private:
    std::unique_ptr<Hooking::IGroupHook> m_Hooks[(int)HookFunc::Count];

    template<HookFunc fn, class... Args>
    void InitHook(Args&&... args);
    template<HookFunc fn>
    void InitGlobalHook();

    static void* s_RawFunctions[(int)HookFunc::Count];
    static void InitRawFunctionsList();
    template<HookFunc fn>
    static void FindFunc(const char* signature, const char* mask, int offset = 0, const char* module = "client");
    static void FindFunc_CNewParticleEffect_SetDormant();

    void IngameStateChanged(bool inGame);
    class Panel;
    std::unique_ptr<Panel> m_Panel;
};

// Finds a byte pattern in a module, falling back to a near-exact match if a game update changed a few of its bytes.
// Giving a name logs failures and approximate matches, and lists the result in ce_signatures_status.
extern std::byte* SignatureScan(const char* moduleName, const char* signature, const char* mask, int offset = 0,
                                const char* name = nullptr);
extern std::byte* SignatureScanMultiple(const char* moduleName, const char* signature, const char* mask,
                                        const std::function<bool(std::byte* found)>& testFunc, int offset = 0);
extern HookManager* GetHooks();

// Lists the outcome of a game lookup in ce_signatures_status (score: 1 exact, 0 not found, else approximate).
extern void RecordSignatureStatus(const char* name, const char* moduleName, float score);

// Strings in a game binary rarely change between updates, unlike the surrounding code, which makes the code that
// references one a robust place to find things from.
extern std::byte* FindModuleString(const char* moduleName, const char* str);
// Finds the first lea r64, [rip+disp32] referencing an address, returning its displacement operand.
extern std::byte* FindRipRelativeReference(const char* moduleName, const void* target);
extern bool IsAddressInModule(const char* moduleName, const void* address, size_t length = 1);

// True if a ModRM byte selects [rip+disp32] addressing.
constexpr bool IsRipRelativeModRM(uint8_t modrm) { return (modrm & 0xC7) == 0x05; }
// The address a RIP-relative displacement refers to. bytesAfter is the number of instruction bytes after it (e.g. an
// immediate operand), as RIP-relative addressing is relative to the end of the instruction.
inline std::byte* ResolveRipRelative(const void* displacement, size_t bytesAfter = 0)
{
    int32_t value;
    memcpy(&value, displacement, sizeof(value));
    return (std::byte*)displacement + sizeof(int32_t) + bytesAfter + value;
}