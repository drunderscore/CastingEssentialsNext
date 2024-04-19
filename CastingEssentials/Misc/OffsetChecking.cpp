#include "Hooking/IBaseHook.h"
#include "Misc/MissingDefinitions.h"

#include <client/c_baseanimating.h>
#include <client/c_baseplayer.h>
#include <client/c_fire_smoke.h>
#include <client/game_controls/baseviewport.h>
#include <../materialsystem/texturemanager.h>
#include <shared/basecombatweapon_shared.h>
#include <shared/baseviewmodel_shared.h>
#include <shared/usercmd.h>
#include <vgui_controls/ImagePanel.h>
#include <vgui_controls/ProgressBar.h>

template<int A, int B, typename E = void>
struct CheckEqual;

template<int A, int B>
struct CheckEqual<A, B, typename std::enable_if<A == B>::type> {};

#define OFFSET_CHECK(className, memberName, expectedOffset) \
	static_assert(sizeof(CheckEqual<offsetof(className, memberName), expectedOffset>), "Actual size differs from expected");

#define SIZE_CHECK(className, expectedSize) \
	static_assert(sizeof(CheckEqual<sizeof(className), expectedSize>), "Actual size differs from expected");

// Don't trust intellisense... just use the actual errors the compiler emits and
// adjust your padding until you get it right
class OffsetChecking
{
	// FIXME: All of these
	// OFFSET_CHECK(C_BaseEntity, m_EntClientFlags, 90);
	// OFFSET_CHECK(C_BaseEntity, m_lifeState, 165);
	// OFFSET_CHECK(C_BaseEntity, m_bDormant, 426);
	// OFFSET_CHECK(C_BaseEntity, m_Particles, 596);

	// OFFSET_CHECK(C_BaseAnimating, m_nHitboxSet, 1376);
	// OFFSET_CHECK(C_BaseAnimating, m_bDynamicModelPending, 2185);
	// OFFSET_CHECK(C_BaseAnimating, m_pStudioHdr, 2192);
	// OFFSET_CHECK(C_BaseAnimating, m_flFadeScale, 1544);

	// OFFSET_CHECK(C_EconEntity, m_AttributeManager, 2232);
	// OFFSET_CHECK(C_EconEntity, m_bValidatedAttachedEntity, 2560);
	// OFFSET_CHECK(C_EconEntity, m_Something, 2588);

	// OFFSET_CHECK(C_EconEntity::AttributeManager, m_iReapplyProvisionParity, 52);
	// OFFSET_CHECK(C_EconEntity::AttributeManager, m_hOuter, 56);
	// OFFSET_CHECK(C_EconEntity::AttributeManager, m_ProviderType, 64);
	// OFFSET_CHECK(C_EconEntity::AttributeManager, m_Item, 96);

	// OFFSET_CHECK(C_EconEntity::AttributeManager::ScriptCreatedItem, m_iItemDefinitionIndex, 36);
	// OFFSET_CHECK(C_EconEntity::AttributeManager::ScriptCreatedItem, m_iEntityQuality, 40);
	// OFFSET_CHECK(C_EconEntity::AttributeManager::ScriptCreatedItem, m_iEntityLevel, 44);
	// OFFSET_CHECK(C_EconEntity::AttributeManager::ScriptCreatedItem, m_iItemIDHigh, 56);
	// OFFSET_CHECK(C_EconEntity::AttributeManager::ScriptCreatedItem, m_iItemIDLow, 60);
	// OFFSET_CHECK(C_EconEntity::AttributeManager::ScriptCreatedItem, m_iAccountID, 64);
	// OFFSET_CHECK(C_EconEntity::AttributeManager::ScriptCreatedItem, m_iTeamNumber, 152);
	// OFFSET_CHECK(C_EconEntity::AttributeManager::ScriptCreatedItem, m_bInitialized, 156);
	// OFFSET_CHECK(C_EconEntity::AttributeManager::ScriptCreatedItem, m_Attributes, 168);
	// OFFSET_CHECK(C_EconEntity::AttributeManager::ScriptCreatedItem, m_NetworkedDynamicAttributesForDemos, 196);
	// OFFSET_CHECK(C_EconEntity::AttributeManager::ScriptCreatedItem, m_bOnlyIterateItemViewAttributes, 224);

	// OFFSET_CHECK(C_EconEntity::AttributeManager::ScriptCreatedAttribute, m_iAttributeDefinitionIndex, 4);
	// OFFSET_CHECK(C_EconEntity::AttributeManager::ScriptCreatedAttribute, m_iRawValue32, 8);
	// OFFSET_CHECK(C_EconEntity::AttributeManager::ScriptCreatedAttribute, m_flValue, 8);
	// OFFSET_CHECK(C_EconEntity::AttributeManager::ScriptCreatedAttribute, m_nRefundableCurrency, 12);

	// OFFSET_CHECK(C_BaseCombatWeapon, m_hOwner, 2640);

	// SIZE_CHECK(CPlayerLocalData, 740);
	// OFFSET_CHECK(C_BasePlayer, m_vecViewOffset, 252);
	// OFFSET_CHECK(C_BasePlayer, m_flFriction, 648);
	// OFFSET_CHECK(C_BasePlayer, m_Local, 3612);
	// OFFSET_CHECK(C_BasePlayer, m_iFOVStart, 4416);
	// OFFSET_CHECK(C_BasePlayer, m_iDefaultFOV, 4424);
	// OFFSET_CHECK(C_BasePlayer, m_pCurrentCommand, 4484);
	// OFFSET_CHECK(C_BasePlayer, m_hVehicle, 4564);
	// OFFSET_CHECK(C_BasePlayer, m_iv_vecViewOffset, 4588);
	// OFFSET_CHECK(C_BasePlayer, m_hViewModel, 4736);

	// OFFSET_CHECK(C_BaseAnimatingOverlay, m_AnimOverlay, 2216);
	// OFFSET_CHECK(C_BaseFlex, m_flexWeight, 2384);
	// OFFSET_CHECK(C_BaseFlex, m_viewtarget, 2324);

	// OFFSET_CHECK(C_BaseViewModel, m_nViewModelIndex, 2232);
	// OFFSET_CHECK(C_BaseViewModel, m_nAnimationParity, 2248);
	// OFFSET_CHECK(C_BaseViewModel, m_sVMName, 2252);
	// OFFSET_CHECK(C_BaseViewModel, m_sAnimationPrefix, 2256);

	// OFFSET_CHECK(C_EntityFlame, m_hEffect, 1360);

	// OFFSET_CHECK(ConVar, m_fnChangeCallbackClient, 88);

	// OFFSET_CHECK(CUserCmd, mousedx, 56);
	// OFFSET_CHECK(CUserCmd, mousedy, 58);

	// OFFSET_CHECK(vgui::ContinuousProgressBar, _unknown0, 388);
	// OFFSET_CHECK(vgui::ContinuousProgressBar, _unknown1, 392);
	// OFFSET_CHECK(vgui::ContinuousProgressBar, _unknown2, 396);

	// OFFSET_CHECK(studiohdr_t, numbones, 156);

	// OFFSET_CHECK(vgui::PanelMessageMap, baseMap, 24);

	// OFFSET_CHECK(vgui::Panel, m_pDragDrop, 44);
	// OFFSET_CHECK(vgui::Panel, m_bToolTipOverridden, 60);
	// OFFSET_CHECK(vgui::Panel, _panelName, 84);
	// OFFSET_CHECK(vgui::Panel, _border, 88);
	// OFFSET_CHECK(vgui::Panel, _flags, 92);
	// OFFSET_CHECK(vgui::Panel, _cursor, 156);
	// OFFSET_CHECK(vgui::Panel, _buildModeFlags, 160);
	// OFFSET_CHECK(vgui::Panel, m_flAlpha, 296);
	// OFFSET_CHECK(vgui::Panel, m_nPaintBackgroundType, 304);
	// OFFSET_CHECK(vgui::Panel, m_nBgTextureId1, 312);
	// OFFSET_CHECK(vgui::Panel, m_nBgTextureId2, 320);
	// OFFSET_CHECK(vgui::Panel, m_nBgTextureId3, 328);
	// OFFSET_CHECK(vgui::Panel, m_nBgTextureId4, 336);
	// OFFSET_CHECK(vgui::Panel, m_roundedCorners, 340);

	// OFFSET_CHECK(vgui::ImagePanel, m_pImage, 348);
	// OFFSET_CHECK(vgui::ImagePanel, m_pszImageName, 352);
	// OFFSET_CHECK(vgui::ImagePanel, m_FillColor, 376);
	// OFFSET_CHECK(vgui::ImagePanel, m_DrawColor, 380);

	// OFFSET_CHECK(vgui::ProgressBar, m_iProgressDirection, 352);
	// OFFSET_CHECK(vgui::ProgressBar, _progress, 356);
	// OFFSET_CHECK(vgui::ProgressBar, _segmentGap, 364);
	// OFFSET_CHECK(vgui::ProgressBar, _segmentWide, 368);
	// OFFSET_CHECK(vgui::ProgressBar, m_iBarInset, 372);
	// OFFSET_CHECK(vgui::ProgressBar, m_iBarMargin, 376);

	// OFFSET_CHECK(SVC_FixAngle, m_bRelative, 20);
	// OFFSET_CHECK(SVC_FixAngle, m_Angle, 24);

	template<typename T> static void Check(int expectedOffset, T func)
	{
		auto offset = Hooking::VTableOffset(func);
		offset; // Don't generate a warning in release builds
		Assert(offset == expectedOffset);
	}

public:
	OffsetChecking()
	{
#ifdef DEBUG
		int offset;
#endif
		using namespace Hooking;

		// vgui::Panel
		{
			Assert((offset = Hooking::VTableOffset(&Panel::SetVisible)) == 33);
			Assert((offset = Hooking::VTableOffset(&Panel::SetEnabled)) == 54);
			Assert((offset = VTableOffset(&Panel::SetBgColor)) == 58);
			Assert((offset = VTableOffset(&Panel::SetBorder)) == 68);
			Assert((offset = VTableOffset(&Panel::IsCursorNone)) == 78);
			Assert((offset = VTableOffset(&Panel::ApplySchemeSettings)) == 88);
			Assert((offset = VTableOffset(&Panel::OnSetFocus)) == 97);
			Assert((offset = VTableOffset(&Panel::OnCursorMoved)) == 102);
			Assert((offset = VTableOffset(&Panel::OnMouseReleased)) == 107);
			Assert((offset = VTableOffset(&Panel::IsKeyOverridden)) == 117);
			Assert((offset = VTableOffset(&Panel::OnMouseFocusTicked)) == 127);
			Assert((offset = VTableOffset(&Panel::SetKeyBoardInputEnabled)) == 137);
			Assert((offset = VTableOffset(&Panel::SetShowDragHelper)) == 147);
			Assert((offset = VTableOffset(&Panel::OnDropContextHoverHide)) == 157);
			Assert((offset = VTableOffset(&Panel::GetDropTarget)) == 167);
			Assert((offset = VTableOffset(&Panel::NavigateDown)) == 177);
			Assert((offset = VTableOffset(&Panel::OnStartDragging)) == 187);
			Assert((offset = VTableOffset(&Panel::OnRequestFocus)) == 194);
		}

		// vgui::ImagePanel
		Assert((offset = VTableOffset(static_cast<void(ImagePanel::*)(const char*)>(&ImagePanel::SetImage))) == 212);

		// vgui::EditablePanel
		{
			Assert((offset = Hooking::VTableOffset(&EditablePanel::LoadControlSettings)) == 212);
			Assert((offset = Hooking::VTableOffset(&EditablePanel::LoadUserConfig)) == 213);
			Assert((offset = Hooking::VTableOffset(&EditablePanel::SaveUserConfig)) == 214);
			Assert((offset = Hooking::VTableOffset(&EditablePanel::LoadControlSettingsAndUserConfig)) == 215);
			Assert((offset = Hooking::VTableOffset(&EditablePanel::ActivateBuildMode)) == 216);
			Assert((offset = Hooking::VTableOffset(&EditablePanel::GetBuildGroup)) == 217);
			Assert((offset = Hooking::VTableOffset(&EditablePanel::CreateControlByName)) == 218);

			Assert((offset = Hooking::VTableOffset(&EditablePanel::GetControlInt)) == 222);
			Assert((offset = Hooking::VTableOffset(&EditablePanel::SetControlEnabled)) == 225);
			Assert((offset = Hooking::VTableOffset(&EditablePanel::SetControlVisible)) == 226);

			Assert((offset = Hooking::VTableOffset(
				static_cast<void(EditablePanel::*)(const char*, float)>(&EditablePanel::SetDialogVariable))) == 227);
			Assert((offset = Hooking::VTableOffset(
				static_cast<void(EditablePanel::*)(const char*, int)>(&EditablePanel::SetDialogVariable))) == 228);
			Assert((offset = Hooking::VTableOffset(
				static_cast<void(EditablePanel::*)(const char*, const wchar_t*)>(&EditablePanel::SetDialogVariable))) == 229);
			Assert((offset = Hooking::VTableOffset(
				static_cast<void (EditablePanel::*)(const char*, const char*)>(&EditablePanel::SetDialogVariable))) == 230);
		}

		// CBaseViewport
		{
			Assert((offset = Hooking::VTableOffset(&EditablePanel::RegisterControlSettingsFile)) == 231);
			Assert((offset = Hooking::VTableOffset(&CBaseViewport::RegisterControlSettingsFile)) == 231);

			Assert((offset = Hooking::VTableOffset(&CBaseViewport::CreatePanelByName)) == 237);
			//Assert((offset = Hooking::VTableOffset(&CBaseViewport::FindPanelByName)) == 238);
			//Assert((offset = Hooking::VTableOffset(&CBaseViewport::GetActivePanel)) == 239);
			Assert((offset = Hooking::VTableOffset(&CBaseViewport::AddNewPanel)) == 239);
			Assert((offset = Hooking::VTableOffset(&CBaseViewport::CreateDefaultPanels)) == 240);
			Assert((offset = Hooking::VTableOffset(&CBaseViewport::Start)) == 241);
			Assert((offset = Hooking::VTableOffset(&CBaseViewport::ReloadScheme)) == 242);
			Assert((offset = Hooking::VTableOffset(&CBaseViewport::AllowedToPrintText)) == 245);
			Assert((offset = Hooking::VTableOffset(&CBaseViewport::GetAnimationController)) == 248);

			Assert((offset = Hooking::VTableOffset(&ITextureManager::FindNext)) == 32);
		}

		// C_BaseEntity
		{
			Check(74, &C_BaseEntity::GetTeamNumber);
			Check(83, &C_BaseEntity::Simulate);
			Assert((offset = VTableOffset(&C_BaseEntity::DrawBrushModel)) == 101);
			Assert((offset = VTableOffset(&C_BaseEntity::SUB_Remove)) == 114);
			Assert((offset = VTableOffset(&C_BaseEntity::DamageDecal)) == 117);
			Assert((offset = VTableOffset(&C_BaseEntity::PhysicsSimulate)) == 129);
			Assert((offset = VTableOffset(&C_BaseEntity::ShouldCollide)) == 145);
			Assert((offset = VTableOffset(&C_BaseEntity::OnPredictedEntityRemove)) == 153);
			Assert((offset = VTableOffset(&C_BaseEntity::ValidateEntityAttachedToPlayer)) == 160);
		}

		// C_BaseAnimating
		{
			Assert((offset = VTableOffset(&C_BaseAnimating::InternalDrawModel)) == 168);
			Assert((offset = VTableOffset(&C_BaseAnimating::OnInternalDrawModel)) == 169);
			Assert((offset = VTableOffset(&C_BaseAnimating::OnPostInternalDrawModel)) == 170);
			Assert((offset = VTableOffset(&C_BaseAnimating::GetEconWeaponMaterialOverride)) == 171);
			Assert((offset = VTableOffset(&C_BaseAnimating::AttachEntityToBone)) == 181);
			Assert((offset = VTableOffset(&C_BaseAnimating::FrameAdvance)) == 191);
			Assert((offset = VTableOffset(&C_BaseAnimating::GetServerIntendedCycle)) == 201);
			Assert((offset = VTableOffset(&C_BaseAnimating::IsViewModel)) == 203);
			Check(207, &C_BaseAnimating::LastBoneChangedTime);
		}

		// C_EconEntity
		{
			Check(208, &C_EconEntity::ShouldShowToolTip);
			Check(211, &C_EconEntity::UpdateAttachmentModels);
			Check(214, &C_EconEntity::IsOverridingViewmodel);
			Check(215, &C_EconEntity::DrawOverriddenViewmodel);
		}

		// C_BasePlayer
		{
			Assert((offset = VTableOffset(&C_BasePlayer::GetEconWeaponMaterialOverride)) == 171);
			Assert((offset = VTableOffset(&C_BasePlayer::ControlMouth)) == 172);
			Assert((offset = VTableOffset(&C_BasePlayer::DoAnimationEvents)) == 173);
			Assert((offset = VTableOffset(&C_BasePlayer::FireEvent)) == 174);
			Assert((offset = VTableOffset(&C_BasePlayer::BecomeRagdollOnClient)) == 184);
			Assert((offset = VTableOffset(&C_BasePlayer::ComputeClientSideAnimationFlags)) == 194);
			Assert((offset = VTableOffset(&C_BasePlayer::DoMuzzleFlash)) == 198);
			Assert((offset = VTableOffset(&C_BasePlayer::FormatViewModelAttachment)) == 204);
			Assert((offset = VTableOffset(&C_BasePlayer::CalcAttachments)) == 206);
			Assert((offset = VTableOffset(&C_BasePlayer::LastBoneChangedTime)) == 207);
			//Assert((offset = VTableOffset(&C_BasePlayer::OnModelLoadComplete)) == 206);
			Assert((offset = VTableOffset(&C_BasePlayer::InitPhonemeMappings)) == 208);
			Assert((offset = VTableOffset(&C_BasePlayer::SetViewTarget)) == 211);
			Assert((offset = VTableOffset(&C_BasePlayer::ProcessSceneEvents)) == 213);
			Assert((offset = VTableOffset(&C_BasePlayer::ProcessSceneEvent)) == 214);
			Assert((offset = VTableOffset(&C_BasePlayer::ProcessSequenceSceneEvent)) == 215);
			Assert((offset = VTableOffset(&C_BasePlayer::ClearSceneEvent)) == 216);
			Assert((offset = VTableOffset(&C_BasePlayer::CheckSceneEventCompletion)) == 217);
			//Assert((offset = VTableOffset(&C_BasePlayer::EnsureTranslations)) == 216);
			Assert((offset = VTableOffset(&C_BasePlayer::Weapon_Switch)) == 224);
			Assert((offset = VTableOffset(&C_BasePlayer::Weapon_CanSwitchTo)) == 225);
			Assert((offset = VTableOffset(&C_BasePlayer::GetActiveWeapon)) == 226);
			Assert((offset = VTableOffset(&C_BasePlayer::GetGlowEffectColor)) == 227);
			Assert((offset = VTableOffset(&C_BasePlayer::UpdateGlowEffect)) == 228);
			Assert((offset = VTableOffset(&C_BasePlayer::DestroyGlowEffect)) == 229);
			Assert((offset = VTableOffset(&C_BasePlayer::SharedSpawn)) == 230);
			Assert((offset = VTableOffset(&C_BasePlayer::GetSteamID)) == 231);
			Assert((offset = VTableOffset(&C_BasePlayer::GetPlayerMaxSpeed)) == 232);
			Assert((offset = VTableOffset(&C_BasePlayer::CalcView)) == 233);
			Assert((offset = VTableOffset(&C_BasePlayer::CalcViewModelView)) == 234);
			Assert((offset = VTableOffset(&C_BasePlayer::PlayerUse)) == 242);
			Check(251, &C_BasePlayer::IsOverridingViewmodel);
			Assert((offset = VTableOffset(&C_BasePlayer::DrawOverriddenViewmodel)) == 252);
			Assert((offset = VTableOffset(&C_BasePlayer::ItemPreFrame)) == 262);
			Assert((offset = VTableOffset(&C_BasePlayer::GetFOV)) == 272);
		}

		// C_BaseViewModel
		{
			Check(209, &C_BaseViewModel::SetWeaponModel);
		}
	}
};
#ifdef DEBUG
static OffsetChecking s_OffsetChecking;
#endif
