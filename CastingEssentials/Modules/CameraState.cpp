#include "CameraState.h"
#include "Misc/HLTVCameraHack.h"
#include "Modules/CameraSmooths.h"
#include "Modules/CameraTools.h"
#include "Modules/FOVOverride.h"
#include "PluginBase/Interfaces.h"
#include "PluginBase/Player.h"

#include <cdll_int.h>
#include <igameevents.h>
#include <vprof.h>

#undef min
#undef max

MODULE_REGISTER(CameraState);

CameraState::CameraState()
    : m_InToolModeHook(std::bind(&CameraState::InToolModeOverride, this)),
      m_IsThirdPersonCameraHook(std::bind(&CameraState::IsThirdPersonCameraOverride, this)),
      m_SetupEngineViewHook(std::bind(&CameraState::SetupEngineViewOverride, this, std::placeholders::_1,
                                      std::placeholders::_2, std::placeholders::_3))
{
    InitViews();

    m_LastSpecTarget = 0;
    m_ThirdPersonOverride = false;
}

ObserverMode CameraState::GetLocalObserverMode()
{
    if (auto engineClient = Interfaces::GetEngineClient(); engineClient && engineClient->IsHLTV())
    {
        if (auto hltvCamera = Interfaces::GetHLTVCamera())
            return hltvCamera->GetMode();
    }

    if (const auto localPlayer = Player::GetLocalPlayer())
        return localPlayer->GetObserverMode();

    Assert(!"Unable to determine current observer mode");
    return OBS_MODE_NONE;
}

C_BaseEntity* CameraState::GetLocalObserverTarget(bool attachedModesOnly)
{
    if (attachedModesOnly)
    {
        if (auto mode = GetLocalObserverMode();
            mode != ObserverMode::OBS_MODE_CHASE && mode != ObserverMode::OBS_MODE_IN_EYE)
            return nullptr;
    }

    if (auto client = Interfaces::GetEngineClient(); client && client->IsHLTV())
    {
        if (auto hltvcamera = Interfaces::GetHLTVCamera())
            return hltvcamera->GetPrimaryTarget();
    }
    else
    {
        Player* local = Player::GetLocalPlayer();
        if (local)
            return local->GetObserverTarget();
    }

    return nullptr;
}

C_BaseEntity* CameraState::GetLastSpecTarget() const
{
    if (m_LastSpecTarget < 0 || m_LastSpecTarget > MAX_EDICTS)
        return nullptr;

    auto list = Interfaces::GetClientEntityList();
    if (!list)
        return nullptr;

    auto clientEnt = list->GetClientEntity(m_LastSpecTarget);
    return clientEnt ? clientEnt->GetBaseEntity() : nullptr;
}

bool CameraState::InToolModeOverride()
{
    VPROF_BUDGET(__FUNCTION__, VPROF_BUDGETGROUP_CE);
    m_LastFrameInToolMode = m_ThisFrameInToolMode;

    m_ThisFrameInToolMode = false;

    {
        ICameraOverride* module;

        if ((module = CameraTools::GetModule()) != nullptr)
            m_ThisFrameInToolMode = module->InToolModeOverride();

        if (!m_ThisFrameInToolMode && (module = CameraSmooths::GetModule()) != nullptr)
            m_ThisFrameInToolMode = module->InToolModeOverride();

        if (!m_ThisFrameInToolMode && (module = FOVOverride::GetModule()) != nullptr)
            m_ThisFrameInToolMode = module->InToolModeOverride();
    }

    if (m_ThisFrameInToolMode)
        GetHooks()->SetState<HookFunc::IClientEngineTools_InToolMode>(Hooking::HookAction::SUPERCEDE);

    return m_ThisFrameInToolMode;
}

bool CameraState::IsThirdPersonCameraOverride()
{
    VPROF_BUDGET(__FUNCTION__, VPROF_BUDGETGROUP_CE);
    m_LastFrameIsThirdPerson = m_ThisFrameIsThirdPerson;

    m_ThisFrameIsThirdPerson = ModulesWantThirdPerson();

    if (m_ThisFrameIsThirdPerson)
        GetHooks()->SetState<HookFunc::IClientEngineTools_IsThirdPersonCamera>(Hooking::HookAction::SUPERCEDE);

    return m_ThisFrameIsThirdPerson;
}

bool CameraState::ModulesWantThirdPerson() const
{
    ICameraOverride* module;

    if ((module = CameraTools::GetModule()) != nullptr && module->IsThirdPersonCameraOverride())
        return true;

    if ((module = CameraSmooths::GetModule()) != nullptr && module->IsThirdPersonCameraOverride())
        return true;

    if ((module = FOVOverride::GetModule()) != nullptr && module->IsThirdPersonCameraOverride())
        return true;

    return false;
}

void CameraState::UpdateThirdPersonOverride()
{
    const bool thirdPerson = ModulesWantThirdPerson();
    if (thirdPerson == m_ThirdPersonOverride)
        return;

    m_ThirdPersonOverride = thirdPerson;

    // TF2 only decides whether the in-eye target's cosmetics/weapon are drawn when it sees the view change (a
    // spectator mode/target change, or a real first <-> third person switch), and the answer stays cached in the
    // client leaf system until the next one. Our IsThirdPersonCamera override pulls the camera out of (and back into)
    // first person without TF2 noticing -- ce_tplock_taunt_enable on every taunt -- so the target's hats stayed
    // hidden for the whole taunt. Replay the event C_HLTVCamera::SetMode fires so C_TFPlayer refreshes its wearables,
    // weapon attachments and bodygroups itself; it only acts on it when IN_EYE is involved.
    if (GetLocalObserverMode() != OBS_MODE_IN_EYE)
        return;

    auto target = Player::AsPlayer(GetLocalObserverTarget());
    auto eventManager = Interfaces::GetGameEventManager();
    if (!target || !eventManager)
        return;

    // Null when nothing listens to it, e.g. no TF players exist yet.
    if (auto event = eventManager->CreateEvent("hltv_changed_mode"))
    {
        event->SetInt("oldmode", OBS_MODE_IN_EYE);
        event->SetInt("newmode", OBS_MODE_IN_EYE);
        event->SetInt("obs_target", target->GetEntity()->entindex());
        eventManager->FireEventClientSide(event); // Frees the event
    }
}

bool CameraState::SetupEngineViewOverride(Vector& origin, QAngle& angles, float& fov)
{
    VPROF_BUDGET(__FUNCTION__, VPROF_BUDGETGROUP_CE);
    m_LastFrameEngineView = m_ThisFrameEngineView;
    m_LastFramePluginView = m_ThisFramePluginView;

    m_ThisFrameEngineView.Set(origin, angles, fov);
    m_ThisFramePluginView.Init();

    bool retVal = false;
    {
        ICameraOverride* module;

        if ((module = CameraTools::GetModule()) != nullptr)
        {
            retVal = module->SetupEngineViewOverride(origin, angles, fov);
            m_ThisFramePluginView.Set(origin, angles, fov);
        }

        if ((module = CameraSmooths::GetModule()) != nullptr)
        {
            retVal = module->SetupEngineViewOverride(origin, angles, fov) || retVal;
            m_ThisFramePluginView.Set(origin, angles, fov);
        }

        if ((module = FOVOverride::GetModule()) != nullptr)
        {
            retVal = module->SetupEngineViewOverride(origin, angles, fov) || retVal;
            m_ThisFramePluginView.Set(origin, angles, fov);
        }
    }

    if (retVal)
        GetHooks()->SetState<HookFunc::IClientEngineTools_SetupEngineView>(Hooking::HookAction::SUPERCEDE);

    return retVal;
}

void CameraState::OnTick(bool inGame)
{
    VPROF_BUDGET(__FUNCTION__, VPROF_BUDGETGROUP_CE);
    if (inGame)
    {
        SetupHooks(true);

        // Update last spec target
        if (auto primaryTarget = Interfaces::GetHLTVCamera()->m_iTraget1;
            primaryTarget > 0 && primaryTarget < MAX_EDICTS)
            m_LastSpecTarget = primaryTarget;

        UpdateThirdPersonOverride();
    }
    else
    {
        SetupHooks(false);
    }
}

void CameraState::LevelInit() { InitViews(); }

void CameraState::InitViews()
{
    m_LastFrameEngineView.Init();
    m_LastFramePluginView.Init();
    m_ThisFrameEngineView.Init();
    m_ThisFramePluginView.Init();

    Invalidate(m_LastFrameInToolMode);
    Invalidate(m_ThisFrameInToolMode);

    Invalidate(m_LastFrameIsThirdPerson);
    Invalidate(m_ThisFrameIsThirdPerson);
}

void CameraState::Invalidate(bool& b)
{
    static_assert(sizeof(byte) == sizeof(decltype(b)), "Fix this function for a different compiler than VS2015");
    *(byte*)(&b) = std::numeric_limits<byte>::max();
}

void CameraState::SetupHooks(bool connect)
{
    m_InToolModeHook.SetEnabled(connect);
    m_IsThirdPersonCameraHook.SetEnabled(connect);
    m_SetupEngineViewHook.SetEnabled(connect);
}

void CameraState::View::Init()
{
    m_Origin.Invalidate();
    m_Angles.Invalidate();
    m_FOV = VEC_T_NAN;
}

void CameraState::View::Set(const Vector& origin, const QAngle& angles, float fov)
{
    m_Origin = origin;
    m_Angles = angles;
    m_FOV = fov;
}
