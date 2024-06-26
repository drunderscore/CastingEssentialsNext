#pragma once

#include "PluginBase/Hook.h"
#include "PluginBase/Modules.h"

#include <convar.h>
#include <materialsystem/MaterialSystemUtil.h>

#include <optional>

class TextureTools : public Module<TextureTools>
{
public:
    TextureTools();

    static bool CheckDependencies();
    static constexpr __forceinline const char* GetModuleName() { return "Texture Tools"; }

private:
    Hook<HookFunc::CBaseClientRenderTargets_InitClientRenderTargets> m_CreateRenderTargetsHook;

    void ToggleFullResRTs();
    void InitClientRenderTargetsOverride(CBaseClientRenderTargets* pThis, IMaterialSystem* pMaterialSystem,
                                         IMaterialSystemHardwareConfig* config, int waterRes, int cameraRes);

    CTextureReference m_FullFrameDepth;

    ConVar ce_texturetools_full_res_rts;
};
