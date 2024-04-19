#include "HitEvents.h"
#include "Modules/CameraState.h"
#include "PluginBase/HookManager.h"
#include "PluginBase/Interfaces.h"
#include "PluginBase/Player.h"
#include "PluginBase/TFPlayerResource.h"

#include <client/c_baseentity.h>
#include <client/c_baseplayer.h>
#include <client/cdll_client_int.h>
#include <igameevents.h>
#include <toolframework/ienginetool.h>
#include <vgui_controls/EditablePanel.h>

#include <sstream>

MODULE_REGISTER(HitEvents);

HitEvents::HitEvents() :
	ce_hitevents_enabled("ce_hitevents_enabled", "0", FCVAR_NONE, "Enables hitsounds and damage numbers in STVs.",
		[](IConVar* var, const char* oldValue, float fOldValue) { GetModule()->UpdateEnabledState(); }),
	ce_hitevents_dmgnumbers_los("ce_hitevents_dmgnumbers_los", "1", FCVAR_NONE, "Should we require LOS to the target before showing a damage number? For a \"normal\" TF2 experience, this would be set to 1."),
	ce_hitevents_healing_crossbow_only("ce_hitevents_healing_crossbow_only", "0", FCVAR_NONE, "Only show healing events originating from the Crusader's Crossbow."),

	m_FireGameEventHook(std::bind(&HitEvents::FireGameEventOverride, this, std::placeholders::_1, std::placeholders::_2)),

	m_UTILTracelineHook(std::bind(&HitEvents::UTILTracelineOverride, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5, std::placeholders::_6)),

	m_DamageAccountPanelShouldDrawHook(std::bind(&HitEvents::DamageAccountPanelShouldDrawOverride, this, std::placeholders::_1))
{
	m_OverrideUTILTraceline = false;

	m_LastDamageAccount = nullptr;
}

class CAccountPanel : public vgui::EditablePanel
{
public:
	virtual ~CAccountPanel() = default;

	struct Event
	{
		enum eAccountDeltaType_t
		{
			ACCOUNT_DELTA_INVALID,
			ACCOUNT_DELTA_HEALING,
			ACCOUNT_DELTA_DAMAGE,
			ACCOUNT_DELTA_BONUS_POINTS,
			ACCOUNT_DELTA_ROBOT_DESTRUCTION_POINT_RED,
			ACCOUNT_DELTA_ROBOT_DESTRUCTION_POINT_BLUE,
		};

		// amount of delta
		int m_iAmount;

		bool m_bLargeFont;		// display larger font
		eAccountDeltaType_t m_eDataType;

		// die time
		float m_flDieTime;

		// position
		int m_nX;				// X Pos in screen space & world space
		int m_nXEnd;			// Ending X Pos in screen space and world space
		int m_nHStart;			// Starting Y Pos in screen space, Z pos in world space
		int m_nHEnd;			// Ending Y Pos in screen space, Z pos in world space
		int m_nY;				// Y Coord in world space, not used in screen space
		bool m_bWorldSpace;
		float m_flBatchWindow;
		int m_nSourceID;		// Can be entindex, etc
		Color m_color;
		bool m_bShadows;

		// append a bit of extra text to the end
		wchar_t m_wzText[8];
	};

private:
	static constexpr auto EVENT_SIZE = sizeof(Event);

	static_assert(EVENT_SIZE == 72);

	static constexpr auto test = offsetof(Event, m_flDieTime);
	static_assert(offsetof(Event, m_bLargeFont) == 4);
	static_assert(offsetof(Event, m_eDataType) == 8);
	static_assert(offsetof(Event, m_flDieTime) == 12);
	static_assert(offsetof(Event, m_flBatchWindow) == 40);
	static_assert(offsetof(Event, m_color) == 48);

public:
	void* some_vtable;
	CUtlVector<Event> m_Events;

	int m_nBGTexture;
	bool m_bNegativeFlipDir;

	float m_flDeltaItemStartPos;
	float m_flDeltaItemEndPos;

	float m_flDeltaItemX;
	float m_flDeltaItemXEndPos;

	float m_flBGImageX;
	float m_flBGImageY;
	float m_flBGImageWide;
	float m_flBGImageTall;

	Color m_DeltaPositiveColor;
	Color m_DeltaNegativeColor;
	Color m_DeltaEventColor;
	Color m_DeltaRedRobotScoreColor;
	Color m_DeltaBlueRobotScoreColor;

	float m_flDeltaLifetime;

	vgui::HFont m_hDeltaItemFont;
	vgui::HFont m_hDeltaItemFontBig;
};

class CDamageAccountPanel : public CAccountPanel
{
public:
	virtual ~CDamageAccountPanel() = default;

	int garbo;
};

void HitEvents::LevelInit()
{
	m_Panel = nullptr;
	UpdateEnabledState();
}

void HitEvents::LevelShutdown()
{
	UpdateEnabledState();
	m_EventsToIgnore.clear();
	m_Panel = nullptr;
}

void HitEvents::UpdateEnabledState()
{
	const bool enabled = IsInGame() && (ce_hitevents_enabled.GetBool());
	m_FireGameEventHook.SetEnabled(enabled);
	m_UTILTracelineHook.SetEnabled(enabled);
	m_DamageAccountPanelShouldDrawHook.SetEnabled(enabled);
}

void HitEvents::FireGameEventOverride(CDamageAccountPanel* pThis, IGameEvent* event)
{

	if (auto it = std::find(m_EventsToIgnore.cbegin(), m_EventsToIgnore.cend(), event); it != m_EventsToIgnore.cend()) {
		// We injected this event ourselves, ignore it.
		m_FireGameEventHook.SetState(Hooking::HookAction::SUPERCEDE);
		m_EventsToIgnore.erase(it);
		return;
	}

	auto is_player_hurt = stricmp(event->GetName(), "player_hurt") == 0;

	auto crossbow_only = ce_hitevents_healing_crossbow_only.GetBool();

	auto is_player_healed = !crossbow_only && !is_player_hurt && stricmp(event->GetName(), "player_healed") == 0;
	auto is_crossbow = !is_player_healed && crossbow_only && stricmp(event->GetName(), "crossbow_heal") == 0;

	if (is_player_hurt || is_player_healed || is_crossbow) {
		m_FireGameEventHook.SetState(Hooking::HookAction::SUPERCEDE);

		if (auto mode = CameraState::GetLocalObserverMode();
			(mode != ObserverMode::OBS_MODE_CHASE && mode != ObserverMode::OBS_MODE_IN_EYE))
		{
			return;
		}

		auto localPlayer = Player::GetLocalPlayer();
		if (!localPlayer)
			return;
		
		auto inflictor = event->GetInt(is_player_hurt ? "attacker" : "healer"); // 'healer' for both player_healed and crossbow_heal
		auto target = event->GetInt(is_player_hurt ? "userid" : is_player_healed ? "patient" : "target");
		if (inflictor == 0 || target == 0 || target == inflictor) return;

		auto specTarget = Player::AsPlayer(CameraState::GetLocalObserverTarget());
		if (!specTarget || specTarget->GetUserID() != inflictor) return;

		auto localPlayerEnt = C_BasePlayer::GetLocalPlayer();
		Assert(localPlayerEnt->IsPlayer());

		const auto lifeStatePusher = CreateVariablePusher<char>(localPlayerEnt->m_lifeState, LIFE_ALIVE);
		const auto tracelineOverridePusher = CreateVariablePusher(m_OverrideUTILTraceline, !ce_hitevents_dmgnumbers_los.GetBool());

		// Not sure we can just mutate the event, so we'll clone it instead. std::unique_ptr is convenient here for giving
		// us an RAII wrapper around the event.
		auto deleter = [](IGameEvent* event) { gameeventmanager->FreeEvent(event); };
		std::unique_ptr<IGameEvent, decltype(deleter)> newEvent(nullptr, deleter);

		if (!is_crossbow) {
			// For player_hurt and player_healed we can simply duplicate the event
			newEvent.reset(gameeventmanager->DuplicateEvent(event));
		}
		else {
			// CDamageAccountPanel doesn't handle crossbow_heal, so we make a matching player_healed.
			newEvent.reset(gameeventmanager->CreateEvent("player_healed"));
			if (!newEvent) return;
			newEvent->SetInt("patient", event->GetInt("target"));
			newEvent->SetInt("amount", event->GetInt("amount"));
		}

		newEvent->SetInt(is_player_hurt ? "attacker" : "healer", localPlayer->GetUserID());

		m_FireGameEventHook.GetOriginal()(pThis, newEvent.get());

		if (is_player_hurt) {
			// We re-inject player hurt events with the updated info so that stuff like "crit!" sprites will show up.
			m_EventsToIgnore.push_back(newEvent.get());
			gameeventmanager->FireEventClientSide(newEvent.release()); // We need to release ownership here as FireEventClientSide will free the event!
		}
	}
	else {
		m_FireGameEventHook.SetState(Hooking::HookAction::IGNORE);
	}
}

void HitEvents::UTILTracelineOverride(const Vector& vecAbsStart, const Vector& vecAbsEnd, unsigned int mask, const IHandleEntity* ignore, int collisionGroup, trace_t* ptr)
{
	if (m_OverrideUTILTraceline)
	{
		ptr->fraction = 1.0;
		m_UTILTracelineHook.SetState(Hooking::HookAction::SUPERCEDE);
	}
}

bool HitEvents::DamageAccountPanelShouldDrawOverride(CDamageAccountPanel* pThis)
{
	m_DamageAccountPanelShouldDrawHook.SetState(Hooking::HookAction::SUPERCEDE);

	if (pThis != m_Panel) {
		m_Panel = pThis;

		// CDamageAccountPanel is actually an IGameEventListener2, but there's other stuff
		// in there before we get to the VGUI panel. This cast is safe because the IGameEventListener2
		// interface is the first one in the vtable.
		auto ptr = reinterpret_cast<IGameEventListener2*>(pThis);
		if (!Interfaces::GetGameEventManager()->FindListener(ptr, "crossbow_heal")) {
			Interfaces::GetGameEventManager()->AddListener(ptr, "crossbow_heal", false);
		}
	}

	// Not too sure why this offset is required, maybe i'm just an idiot
	CDamageAccountPanel* questionable = (CDamageAccountPanel*)((std::byte*)pThis + 72);

	return questionable->m_Events.Count() > 0;
}