#include "common.h"

#include "RwHelper.h"
#include "PlayerPed.h"
#include "Wanted.h"
#include "Fire.h"
#include "DMAudio.h"
#include "Pad.h"
#include "Camera.h"
#include "WeaponEffects.h"
#include "ModelIndices.h"
#include "World.h"
#include "RpAnimBlend.h"
#include "AnimBlendAssociation.h"
#include "General.h"
#include "Pools.h"
#include "PedModelInfo.h"
#include "Darkel.h"
#include "CarCtrl.h"
#include "MBlur.h"
#include "Streaming.h"
#include "Population.h"
#include "Script.h"
#include "Replay.h"
#include "PedPlacement.h"
#include "VarConsole.h"
#include "SaveBuf.h"

#define PAD_MOVE_TO_GAME_WORLD_MOVE 60.0f
// how much the on foot move speed may climb per time step, 0.07 in the original
#define PLAYER_MOVE_ACCELERATION (0.35f)

float CPlayerPed::m_fAimAssistFactor = 1.0f;
float CPlayerPed::m_fAimRaiseSpeed = 2.0f;
bool  CPlayerPed::bAimAssist = true;
bool  CPlayerPed::bMoveWhileAiming = true;
float CPlayerPed::m_fAimAssistStrength = 0.45f;
bool  CPlayerPed::bIsAimPosed = false;

// Whether the stick is pushed far enough this frame that the player means to run, which is
// where the walk and run animations cross over.  Set by both on foot controls just before
// SetRealMoveAnim() reads it.
static bool bPadAsksToRun = false;

// which on foot control ran last, for the StickDebug log: 1 sniper, 2 first person run around, 3 fighter, 4 zelda
int gControlPath = 0;

// one line for the StickDebug overlay about the player's aim and movement, filled each frame
char gPlayerDebugLine[160];
bool CPlayerPed::bDontAllowWeaponChange;
#ifndef MASTER
bool CPlayerPed::bDebugPlayerInfo;
#endif

const uint32 CPlayerPed::nSaveStructSize =
#ifdef COMPATIBLE_SAVES
	1752;
#else
	sizeof(CPlayerPed);
#endif

int32 idleAnimBlockIndex;

CPad*
GetPadFromPlayer(CPlayerPed*)
{
	return CPad::GetPad(0);
}

CPlayerPed::~CPlayerPed()
{
	delete m_pWanted;
}

CPlayerPed::CPlayerPed(void) : CPed(PEDTYPE_PLAYER1)
{
	m_fMoveSpeed = 0.0f;
	SetModelIndex(MI_PLAYER);
#ifdef FIX_BUGS
	m_fCurrentStamina = m_fMaxStamina = 150.0f;
#endif
	SetInitialState();

	m_pWanted = new CWanted();
	m_pWanted->Initialise();
	m_pArrestingCop = nil;
	m_currentWeapon = WEAPONTYPE_UNARMED;
	m_nSelectedWepSlot = WEAPONSLOT_UNARMED;
	m_nSpeedTimer = 0;
	m_bSpeedTimerFlag = false;
	SetWeaponLockOnTarget(nil);
	SetPedState(PED_IDLE);
#ifndef FIX_BUGS
	m_fCurrentStamina = m_fMaxStamina = 150.0f;
#endif
	m_fStaminaProgress = 0.0f;
	m_nEvadeAmount = 0;
	m_pEvadingFrom = nil;
	m_nHitAnimDelayTimer = 0;
	m_fAttackButtonCounter = 0.0f;
	m_bHaveTargetSelected = false;
	m_bHasLockOnTarget = false;
	m_bCanBeDamaged = true;
	m_bNoPosForMeleeAttack = false;
	m_fWalkAngle = 0.0f;
	m_fFPSMoveHeading = 0.0f;
	m_pMinigunTopAtomic = nil;
	m_fGunSpinSpeed = 0.0;
	m_fGunSpinAngle = 0.0;
	m_nPadDownPressedInMilliseconds = 0;
	m_nTargettableObjects[0] = m_nTargettableObjects[1] = m_nTargettableObjects[2] = m_nTargettableObjects[3] = -1;
	unk1 = false;
	for (int i = 0; i < 6; i++) {
		m_vecSafePos[i] = CVector(0.0f, 0.0f, 0.0f);
		m_pPedAtSafePos[i] = nil;
		m_pMeleeList[i] = nil;
	}
	m_nAttackDirToCheck = 0;
	m_nLastBusFareCollected = 0;
	idleAnimBlockIndex = CAnimManager::GetAnimationBlockIndex("playidles");
#ifdef FREE_CAM
	m_bFreeAimActive = false;
#endif
}

void
CPlayerPed::ClearWeaponTarget()
{
	if (m_nPedType == PEDTYPE_PLAYER1) {
		SetWeaponLockOnTarget(nil);
		TheCamera.ClearPlayerWeaponMode();
		CWeaponEffects::ClearCrossHair();
	}
 	ClearPointGunAt();
}

void
CPlayerPed::SetWantedLevel(int32 level)
{
	m_pWanted->SetWantedLevel(level);
}

void
CPlayerPed::SetWantedLevelNoDrop(int32 level)
{
	m_pWanted->SetWantedLevelNoDrop(level);
}

void
CPlayerPed::MakeObjectTargettable(int32 handle)
{
	for (int i = 0; i < ARRAY_SIZE(m_nTargettableObjects); i++) {
		if (CPools::GetObjectPool()->GetAt(m_nTargettableObjects[i]) == nil) {
			m_nTargettableObjects[i] = handle;
			return;
		}
	}
}

// I don't know the actual purpose of parameter
void
CPlayerPed::AnnoyPlayerPed(bool annoyedByPassingEntity)
{
	if (m_pedStats->m_temper < 52) {
		m_pedStats->m_temper++;
	} else if (annoyedByPassingEntity && m_pedStats->m_temper < 55) {
		m_pedStats->m_temper++;
	} else if (annoyedByPassingEntity) {
		m_pedStats->m_temper = 46;
	}
}

void
CPlayerPed::ClearAdrenaline(void)
{
	if (m_bAdrenalineActive && m_nAdrenalineTime != 0) {
		m_nAdrenalineTime = 0;
		CTimer::SetTimeScale(1.0f);
	}
}

CPlayerInfo *
CPlayerPed::GetPlayerInfoForThisPlayerPed()
{
	if (CWorld::Players[0].m_pPed == this)
		return &CWorld::Players[0];

	return nil;
}

void
CPlayerPed::SetupPlayerPed(int32 index)
{
	CPlayerPed *player = new CPlayerPed();
	CWorld::Players[index].m_pPed = player;
#ifdef FIX_BUGS
	player->RegisterReference((CEntity**)&CWorld::Players[index].m_pPed);
#endif

	player->SetOrientation(0.0f, 0.0f, 0.0f);

	CWorld::Add(player);
	player->m_wepAccuracy = 100;

#ifndef MASTER
	VarConsole.Add("Debug PlayerPed", &CPlayerPed::bDebugPlayerInfo, true);
	VarConsole.Add("Tweak Vehicle Handling", &CVehicle::m_bDisplayHandlingInfo, true);
#endif
}

void
CPlayerPed::DeactivatePlayerPed(int32 index)
{
	CWorld::Remove(CWorld::Players[index].m_pPed);
}

void
CPlayerPed::ReactivatePlayerPed(int32 index)
{
	CWorld::Add(CWorld::Players[index].m_pPed);
}

void
CPlayerPed::UseSprintEnergy(void)
{
	if (m_fCurrentStamina > -150.0f && !CWorld::Players[CWorld::PlayerInFocus].m_bInfiniteSprint
		&& !m_bAdrenalineActive) {
		m_fCurrentStamina = m_fCurrentStamina - CTimer::GetTimeStep();
		m_fStaminaProgress = m_fStaminaProgress + CTimer::GetTimeStep();
	}

	if (m_fStaminaProgress >= 500.0f) {
		m_fStaminaProgress = 0;
		if (m_fMaxStamina < 1000.0f)
			m_fMaxStamina += 10.0f;
	}
}

void
CPlayerPed::MakeChangesForNewWeapon(eWeaponType weapon)
{
	if (m_nPedState == PED_SNIPER_MODE) {
		RestorePreviousState();
		TheCamera.ClearPlayerWeaponMode();
	}
	SetCurrentWeapon(weapon);
	m_nSelectedWepSlot = m_currentWeapon;

	GetWeapon()->m_nAmmoInClip = Min(GetWeapon()->m_nAmmoTotal, CWeaponInfo::GetWeaponInfo(GetWeapon()->m_eWeaponType)->m_nAmountofAmmunition);

	if (CWeaponInfo::GetWeaponInfo(GetWeapon()->m_eWeaponType)->IsFlagSet(WEAPONFLAG_CANAIM))
		ClearWeaponTarget();

	// WEAPONTYPE_SNIPERRIFLE? Wut?
	CAnimBlendAssociation* weaponAnim = RpAnimBlendClumpGetAssociation(GetClump(), GetPrimaryFireAnim(CWeaponInfo::GetWeaponInfo(WEAPONTYPE_SNIPERRIFLE)));
	if (weaponAnim) {
		weaponAnim->SetRun();
		weaponAnim->flags |= ASSOC_FADEOUTWHENDONE;
	}
	TheCamera.ClearPlayerWeaponMode();
}

void
CPlayerPed::MakeChangesForNewWeapon(int32 slot)
{
	if(slot != -1)
		MakeChangesForNewWeapon(m_weapons[slot].m_eWeaponType);
}

void
CPlayerPed::ReApplyMoveAnims(void)
{
	static AnimationId moveAnims[] = { ANIM_STD_WALK, ANIM_STD_RUN, ANIM_STD_RUNFAST, ANIM_STD_IDLE, ANIM_STD_STARTWALK };

	for(int i = 0; i < ARRAY_SIZE(moveAnims); i++) {
		CAnimBlendAssociation *curMoveAssoc = RpAnimBlendClumpGetAssociation(GetClump(), moveAnims[i]);
		if (curMoveAssoc) {
			if (CGeneral::faststrcmp(CAnimManager::GetAnimAssociation(m_animGroup, moveAnims[i])->hierarchy->name, curMoveAssoc->hierarchy->name)) {
				CAnimBlendAssociation *newMoveAssoc = CAnimManager::AddAnimation(GetClump(), m_animGroup, moveAnims[i]);
				newMoveAssoc->blendDelta = curMoveAssoc->blendDelta;
				newMoveAssoc->blendAmount = curMoveAssoc->blendAmount;
				curMoveAssoc->blendDelta = -1000.0f;
				curMoveAssoc->flags |= ASSOC_DELETEFADEDOUT;
			}
		}
	}
}

void
CPlayerPed::SetInitialState(void)
{
	m_nDrunkenness = 0;
	m_nFadeDrunkenness = 0;
	CMBlur::ClearDrunkBlur();
	m_nDrunkCountdown = 0;
	m_bAdrenalineActive = false;
	m_nAdrenalineTime = 0;
	CTimer::SetTimeScale(1.0f);
	m_pSeekTarget = nil;
	m_vecSeekPos = CVector(0.0f, 0.0f, 0.0f);
	m_fleeFromPos = CVector2D(0.0f, 0.0f);
	m_fleeFrom = nil;
	m_fleeTimer = 0;
	m_objective = OBJECTIVE_NONE;
	m_prevObjective = OBJECTIVE_NONE;
	bUsesCollision = true;
	ClearAimFlag();
	ClearLookFlag();
	bIsPointingGunAt = false;
	bRenderPedInCar = true;
	if (m_pFire)
		m_pFire->Extinguish();

	RpAnimBlendClumpRemoveAllAssociations(GetClump());
	SetPedState(PED_IDLE);
	SetMoveState(PEDMOVE_STILL);
	m_nLastPedState = PED_NONE;
	m_animGroup = ASSOCGRP_PLAYER;
	m_fMoveSpeed = 0.0f;
	m_nSelectedWepSlot = WEAPONSLOT_UNARMED;
	m_nEvadeAmount = 0;
	m_pEvadingFrom = nil;
	bIsPedDieAnimPlaying = false;
	SetRealMoveAnim();
	m_bCanBeDamaged = true;
	m_pedStats->m_temper = 50;
	m_fWalkAngle = 0.0f;
	if (m_attachedTo && !bUsesCollision)
		bUsesCollision = true;

	m_attachedTo = nil;
	m_attachWepAmmo = 0;
}

void
CPlayerPed::SetRealMoveAnim(void)
{
	CAnimBlendAssociation *curWalkAssoc = RpAnimBlendClumpGetAssociation(GetClump(), ANIM_STD_WALK);
	CAnimBlendAssociation *curRunAssoc = RpAnimBlendClumpGetAssociation(GetClump(), ANIM_STD_RUN);
	CAnimBlendAssociation *curSprintAssoc = RpAnimBlendClumpGetAssociation(GetClump(), ANIM_STD_RUNFAST);
	CAnimBlendAssociation *curWalkStartAssoc = RpAnimBlendClumpGetAssociation(GetClump(), ANIM_STD_STARTWALK);
	CAnimBlendAssociation *curIdleAssoc = RpAnimBlendClumpGetAssociation(GetClump(), ANIM_STD_IDLE);
	CAnimBlendAssociation *curRunStopAssoc = RpAnimBlendClumpGetAssociation(GetClump(), ANIM_STD_RUNSTOP1);
	CAnimBlendAssociation *curRunStopRAssoc = RpAnimBlendClumpGetAssociation(GetClump(), ANIM_STD_RUNSTOP2);
	if (bResetWalkAnims) {
		if (curWalkAssoc)
			curWalkAssoc->SetCurrentTime(0.0f);
		if (curRunAssoc)
			curRunAssoc->SetCurrentTime(0.0f);
		if (curSprintAssoc)
			curSprintAssoc->SetCurrentTime(0.0f);
		bResetWalkAnims = false;
	}

	if (!curIdleAssoc)
		curIdleAssoc = RpAnimBlendClumpGetAssociation(GetClump(), ANIM_STD_IDLE_TIRED);
	if (!curIdleAssoc)
		curIdleAssoc = RpAnimBlendClumpGetAssociation(GetClump(), ANIM_STD_FIGHT_IDLE);
	if (!curIdleAssoc)
		curIdleAssoc = RpAnimBlendClumpGetAssociation(GetClump(), ANIM_MELEE_IDLE_FIGHTMODE);

	if (!((curRunStopAssoc && curRunStopAssoc->IsRunning()) || (curRunStopRAssoc && curRunStopRAssoc->IsRunning()))) {

		if (curRunStopAssoc && curRunStopAssoc->blendDelta >= 0.0f || curRunStopRAssoc && curRunStopRAssoc->blendDelta >= 0.0f) {
			if (curRunStopAssoc) {
				curRunStopAssoc->flags |= ASSOC_DELETEFADEDOUT;
				curRunStopAssoc->blendAmount = 1.0f;
				curRunStopAssoc->blendDelta = -8.0f;
			} else if (curRunStopRAssoc) {
				curRunStopRAssoc->flags |= ASSOC_DELETEFADEDOUT;
				curRunStopRAssoc->blendAmount = 1.0f;
				curRunStopRAssoc->blendDelta = -8.0f;
			}
			
			RestoreHeadingRate();
			if (!curIdleAssoc) {
				if (m_fCurrentStamina < 0.0f && !bIsAimingGun && !CWorld::TestSphereAgainstWorld(GetPosition(), 0.5f,
						nil, true, false, false, false, false, false)) {
					curIdleAssoc = CAnimManager::BlendAnimation(GetClump(), ASSOCGRP_STD, ANIM_STD_IDLE_TIRED, 8.0f);

				} else {
					curIdleAssoc = CAnimManager::BlendAnimation(GetClump(), m_animGroup, ANIM_STD_IDLE, 8.0f);
				}
				m_nWaitTimer = CTimer::GetTimeInMilliseconds() + CGeneral::GetRandomNumberInRange(2500, 4000);
			}
			curIdleAssoc->blendAmount = 0.0f;
			curIdleAssoc->blendDelta = 8.0f;

		} else if (m_fMoveSpeed == 0.0f && !curSprintAssoc) {
			if (!curIdleAssoc) {
				if (m_fCurrentStamina < 0.0f && !bIsAimingGun && !CWorld::TestSphereAgainstWorld(GetPosition(), 0.5f,
						nil, true, false, false, false, false, false)) {
					curIdleAssoc = CAnimManager::BlendAnimation(GetClump(), ASSOCGRP_STD, ANIM_STD_IDLE_TIRED, 4.0f);
					
				} else {
					curIdleAssoc = CAnimManager::BlendAnimation(GetClump(), m_animGroup, ANIM_STD_IDLE, 4.0f);
				}

				m_nWaitTimer = CTimer::GetTimeInMilliseconds() + CGeneral::GetRandomNumberInRange(2500, 4000);
			}

			if ((m_fCurrentStamina > 0.0f || bIsAimingGun) && curIdleAssoc->animId == ANIM_STD_IDLE_TIRED) {
				CAnimManager::BlendAnimation(GetClump(), m_animGroup, ANIM_STD_IDLE, 4.0f);

			} else if (m_nPedState != PED_FIGHT) {
				if (m_fCurrentStamina < 0.0f && !bIsAimingGun && curIdleAssoc->animId != ANIM_STD_IDLE_TIRED
					&& !CWorld::TestSphereAgainstWorld(GetPosition(), 0.5f, nil, true, false, false, false, false, false)) {
					CAnimManager::BlendAnimation(GetClump(), ASSOCGRP_STD, ANIM_STD_IDLE_TIRED, 4.0f);

				} else if (curIdleAssoc->animId != ANIM_STD_IDLE) {
					CAnimManager::BlendAnimation(GetClump(), m_animGroup, ANIM_STD_IDLE, 4.0f);
				}
			}
			m_nMoveState = PEDMOVE_STILL;

		} else {
			if (curIdleAssoc) {
				if (curWalkStartAssoc) {
					curWalkStartAssoc->blendAmount = 1.0f;
					curWalkStartAssoc->blendDelta = 0.0f;
				} else if (!bPadAsksToRun) {
					// Walk and run only start once the walk start has played out, and Vice City's is a
					// full step long, so pushing the stick all the way from standing still walked for
					// most of a second before the run came.  It is kept for setting off gently and left
					// out when the stick asks to run.
					curWalkStartAssoc = CAnimManager::AddAnimation(GetClump(), m_animGroup, ANIM_STD_STARTWALK);
				}
				if (curWalkAssoc)
					curWalkAssoc->SetCurrentTime(0.0f);
				if (curRunAssoc)
					curRunAssoc->SetCurrentTime(0.0f);

				delete curIdleAssoc;
				delete RpAnimBlendClumpGetAssociation(GetClump(), ANIM_STD_IDLE_TIRED);
				CAnimBlendAssociation *fightIdleAnim = RpAnimBlendClumpGetAssociation(GetClump(), ANIM_STD_FIGHT_IDLE);
				if (!fightIdleAnim)
					fightIdleAnim = RpAnimBlendClumpGetAssociation(GetClump(), ANIM_MELEE_IDLE_FIGHTMODE);
				delete fightIdleAnim;
				delete curSprintAssoc;

				curSprintAssoc = nil;
				m_nMoveState = PEDMOVE_WALK;
			}
			if (curRunStopAssoc) {
				delete curRunStopAssoc;
				RestoreHeadingRate();
			}
			if (curRunStopRAssoc) {
				delete curRunStopRAssoc;
				RestoreHeadingRate();
			}
			if (!curWalkAssoc) {
				curWalkAssoc = CAnimManager::AddAnimation(GetClump(), m_animGroup, ANIM_STD_WALK);
				curWalkAssoc->blendAmount = 0.0f;
			}
			if (!curRunAssoc) {
				curRunAssoc = CAnimManager::AddAnimation(GetClump(), m_animGroup, ANIM_STD_RUN);
				curRunAssoc->blendAmount = 0.0f;
			}
			// The stick is still on its way out on the first frame of a push, so the walk start is
			// begun more often than not even when it goes all the way.  Measured in play it then
			// played its full quarter second at a tenth of the running speed, and a frame of none
			// after it.  Once the stick asks to run the walk start is cut short and the walk and run
			// take over on the same frame.
			if (curWalkStartAssoc && (!(curWalkStartAssoc->IsRunning()) || bPadAsksToRun)) {
				delete curWalkStartAssoc;
				curWalkStartAssoc = nil;
				curWalkAssoc->SetRun();
				curRunAssoc->SetRun();
			}
			if (m_nMoveState == PEDMOVE_SPRINT) {
				if (m_fCurrentStamina < 0.0f && (m_fCurrentStamina <= -150.0f || !curSprintAssoc || curSprintAssoc->blendDelta < 0.0f))
					m_nMoveState = PEDMOVE_STILL;

				if (curWalkStartAssoc)
					m_nMoveState = PEDMOVE_STILL;
			}

			if (curSprintAssoc && (m_nMoveState != PEDMOVE_SPRINT || m_fMoveSpeed < 0.4f)) {
				// Stop sprinting in various conditions
				if (curSprintAssoc->blendAmount == 0.0f) {
					curSprintAssoc->blendDelta = -1000.0f;
					curSprintAssoc->flags |= ASSOC_DELETEFADEDOUT;

				} else if (curSprintAssoc->blendDelta >= 0.0f || curSprintAssoc->blendAmount >= 0.8f) {
					if (m_fMoveSpeed < 0.4f) {
						AnimationId runStopAnim;
						if (curSprintAssoc->GetProgress() < 0.5) // double
							runStopAnim = ANIM_STD_RUNSTOP1;
						else
							runStopAnim = ANIM_STD_RUNSTOP2;
						CAnimBlendAssociation* newRunStopAssoc = CAnimManager::AddAnimation(GetClump(), ASSOCGRP_STD, runStopAnim);
						newRunStopAssoc->blendAmount = 1.0f;
						newRunStopAssoc->SetDeleteCallback(RestoreHeadingRateCB, this);
						m_headingRate = 0.0f;
						curSprintAssoc->flags |= ASSOC_DELETEFADEDOUT;
						curSprintAssoc->blendDelta = -1000.0f;
						curWalkAssoc->flags &= ~ASSOC_RUNNING;
						curWalkAssoc->blendAmount = 0.0f;
						curWalkAssoc->blendDelta = 0.0f;
						curRunAssoc->flags &= ~ASSOC_RUNNING;
						curRunAssoc->blendAmount = 0.0f;
						curRunAssoc->blendDelta = 0.0f;

					} else if (curSprintAssoc->blendDelta >= 0.0f) { // this condition is absent on mobile
						// Stop sprinting when tired
						curSprintAssoc->flags |= ASSOC_DELETEFADEDOUT;
						curSprintAssoc->blendDelta = -1.0f;
						curRunAssoc->blendDelta = 1.0f;
					}
				} else if (m_fMoveSpeed < 1.0f) {
					curSprintAssoc->blendDelta = -8.0f;
					curRunAssoc->blendDelta = 8.0f;
				}

			} else if (curWalkStartAssoc) {
				// Walk start and walk/run shouldn't run at the same time
				curWalkAssoc->flags &= ~ASSOC_RUNNING;
				curRunAssoc->flags &= ~ASSOC_RUNNING;
				curWalkAssoc->blendAmount = 0.0f;
				curRunAssoc->blendAmount = 0.0f;

			} else if (m_nMoveState == PEDMOVE_SPRINT) {
				if (curSprintAssoc) {
					// We have anim, do it
					if (curSprintAssoc->blendDelta < 0.0f) {
						curSprintAssoc->blendDelta = 2.0f;
						curRunAssoc->blendDelta = -2.0f;
					}
				} else {
					// Transition between run-sprint
					curWalkAssoc->blendAmount = 0.0f;
					curRunAssoc->blendAmount = 1.0f;
					curSprintAssoc = CAnimManager::BlendAnimation(GetClump(), m_animGroup, ANIM_STD_RUNFAST, 2.0f);
				}
				UseSprintEnergy();
			} else {
				if (m_fMoveSpeed < 1.0f) {
					curWalkAssoc->blendAmount = 1.0f;
					curRunAssoc->blendAmount = 0.0f;
					m_nMoveState = PEDMOVE_WALK;
				} else if (m_fMoveSpeed < 2.0f) {
					curWalkAssoc->blendAmount = 2.0f - m_fMoveSpeed;
					curRunAssoc->blendAmount = m_fMoveSpeed - 1.0f;
					m_nMoveState = PEDMOVE_RUN;
				} else {
					curWalkAssoc->blendAmount = 0.0f;
					curRunAssoc->blendAmount = 1.0f;
					m_nMoveState = PEDMOVE_RUN;
				}
				curWalkAssoc->blendDelta = 0.0f;
				curRunAssoc->blendDelta = 0.0f;
			}
		}
	}
	if (m_bAdrenalineActive) {
		if (CTimer::GetTimeInMilliseconds() > m_nAdrenalineTime) {
			m_bAdrenalineActive = false;
			CTimer::SetTimeScale(1.0f);
			if (curWalkStartAssoc)
				curWalkStartAssoc->speed = 1.0f;
			if (curWalkAssoc)
				curWalkAssoc->speed = 1.0f;
			if (curRunAssoc)
				curRunAssoc->speed = 1.0f;
			if (curSprintAssoc)
				curSprintAssoc->speed = 1.0f;
		} else {
			CTimer::SetTimeScale(1.0f / 3);
			if (curWalkStartAssoc)
				curWalkStartAssoc->speed = 2.0f;
			if (curWalkAssoc)
				curWalkAssoc->speed = 2.0f;
			if (curRunAssoc)
				curRunAssoc->speed = 2.0f;
			if (curSprintAssoc)
				curSprintAssoc->speed = 2.0f;
		}
	} else if (curSprintAssoc) {
		if (TheCamera.Cams[TheCamera.ActiveCam].Mode == CCam::MODE_FIXED) {
			curSprintAssoc->speed = 0.7f;
		} else
			curSprintAssoc->speed = 1.0f;
	}
}

void
CPlayerPed::RestoreSprintEnergy(float restoreSpeed)
{
	if (m_fCurrentStamina < m_fMaxStamina)
		m_fCurrentStamina += restoreSpeed * CTimer::GetTimeStep() * 0.5f;
}

float
CPlayerPed::DoWeaponSmoothSpray(void)
{
	if (m_nPedState == PED_ATTACK && !m_pPointGunAt) {
		CWeaponInfo *weaponInfo = CWeaponInfo::GetWeaponInfo(GetWeapon()->m_eWeaponType);
		switch (GetWeapon()->m_eWeaponType) {
			case WEAPONTYPE_GOLFCLUB:
			case WEAPONTYPE_NIGHTSTICK:
			case WEAPONTYPE_BASEBALLBAT:
				if (GetFireAnimGround(weaponInfo, false) && RpAnimBlendClumpGetAssociation(GetClump(), GetFireAnimGround(weaponInfo, false)))
					return PI / 176.f;
				else
					return -1.0f;

			case WEAPONTYPE_CHAINSAW:
				if (GetMeleeStartAnim(weaponInfo) && RpAnimBlendClumpGetAssociation(GetClump(), GetMeleeStartAnim(weaponInfo))) {
#ifdef FREE_CAM
					if (TheCamera.Cams[0].Using3rdPersonMouseCam()) return -1.0f;
#endif
					return PI / 128.0f;
				}
				else if (GetFireAnimGround(weaponInfo, false) && RpAnimBlendClumpGetAssociation(GetClump(), GetFireAnimGround(weaponInfo, false)))
					return PI / 176.f;
				else
					return PI / 80.f;

			case WEAPONTYPE_PYTHON:
				return PI / 112.f;
			case WEAPONTYPE_SHOTGUN:
			case WEAPONTYPE_SPAS12_SHOTGUN:
			case WEAPONTYPE_STUBBY_SHOTGUN:
				return PI / 112.f;
			case WEAPONTYPE_UZI:
			case WEAPONTYPE_MP5:
				return PI / 112.f;
			case WEAPONTYPE_M4:
			case WEAPONTYPE_RUGER:
				return PI / 112.f;
			case WEAPONTYPE_FLAMETHROWER:
				return PI / 80.f;
			case WEAPONTYPE_M60:
			case WEAPONTYPE_MINIGUN:
			case WEAPONTYPE_HELICANNON:
				return PI / 176.f;
			default:
				return -1.0f;
		}
	} else if (bIsDucking)
		return PI / 112.f;
	else
		return -1.0f;
}

void
CPlayerPed::DoStuffToGoOnFire(void)
{
	if (m_nPedState == PED_SNIPER_MODE)
		TheCamera.ClearPlayerWeaponMode();
}

bool
CPlayerPed::DoesTargetHaveToBeBroken(CVector target, CWeapon *weaponUsed)
{
	CVector distVec = target - GetPosition();

	if (distVec.Magnitude() > CWeaponInfo::GetWeaponInfo(weaponUsed->m_eWeaponType)->m_fRange)
		return true;

	return false;
}

// Cancels landing anim while running & jumping? I think
void
CPlayerPed::RunningLand(CPad *padUsed)
{
	CAnimBlendAssociation *landAssoc = RpAnimBlendClumpGetAssociation(GetClump(), ANIM_STD_FALL_LAND);
	if (landAssoc && landAssoc->currentTime == 0.0f && m_fMoveSpeed > 1.5f
		&& padUsed && (padUsed->GetPedWalkLeftRight() != 0.0f || padUsed->GetPedWalkUpDown() != 0.0f)) {

		landAssoc->blendDelta = -1000.0f;
		landAssoc->flags |= ASSOC_DELETEFADEDOUT;

		CAnimManager::AddAnimation(GetClump(), ASSOCGRP_STD, ANIM_STD_JUMP_LAND)->SetFinishCallback(FinishJumpCB, this);

		if (m_nPedState == PED_JUMP)
			RestorePreviousState();
	}
}

bool
CPlayerPed::IsThisPedAnAimingPriority(CPed *suspect)
{
	if (!suspect->bIsPlayerFriend)
		return true;

	if (suspect->m_pPointGunAt == this)
		return true;

	switch (suspect->m_objective) {
		case OBJECTIVE_KILL_CHAR_ON_FOOT:
		case OBJECTIVE_KILL_CHAR_ANY_MEANS:
			if (suspect->m_pedInObjective == this)
				return true;

			break;
		default:
			break;
	}
	return suspect->m_nPedState == PED_ABSEIL;
}

void
CPlayerPed::PlayerControlSniper(CPad *padUsed)
{
	gControlPath = 1;
	ProcessWeaponSwitch(padUsed);
	TheCamera.PlayerExhaustion = (1.0f - (m_fCurrentStamina - -150.0f) / 300.0f) * 0.9f + 0.1f;

	if (padUsed->DuckJustDown() && !bIsDucking && m_nMoveState != PEDMOVE_SPRINT) {
		bCrouchWhenShooting = true;
		SetDuck(60000, true);
	} else if (bIsDucking && (padUsed->DuckJustDown() || m_nMoveState == PEDMOVE_SPRINT)) {
		ClearDuck(true);
		bCrouchWhenShooting = false;
	}

	if (!padUsed->GetTarget() && !m_attachedTo) {
		RestorePreviousState();
		TheCamera.ClearPlayerWeaponMode();
		return;
	}

	int firingRate = GetWeapon()->m_eWeaponType == WEAPONTYPE_LASERSCOPE ? 333 : 266;
	if (padUsed->WeaponJustDown() && CTimer::GetTimeInMilliseconds() > GetWeapon()->m_nTimer) {
		CVector firePos(0.0f, 0.0f, 0.6f);
		firePos = GetMatrix() * firePos;
		GetWeapon()->Fire(this, &firePos);
		m_nPadDownPressedInMilliseconds = CTimer::GetTimeInMilliseconds();
	} else if (CTimer::GetTimeInMilliseconds() > m_nPadDownPressedInMilliseconds + firingRate &&
		CTimer::GetTimeInMilliseconds() - CTimer::GetTimeStepInMilliseconds() < m_nPadDownPressedInMilliseconds + firingRate && padUsed->GetWeapon()) {
		
		if (GetWeapon()->m_nAmmoTotal > 0) {
			DMAudio.PlayFrontEndSound(SOUND_WEAPON_AK47_BULLET_ECHO, GetWeapon()->m_eWeaponType);
		}
	}
	GetWeapon()->Update(m_audioEntityId, nil);
}

// I think R* also used goto in here.
void
CPlayerPed::ProcessWeaponSwitch(CPad *padUsed)
{
	if (CDarkel::FrenzyOnGoing() || m_attachedTo)
		goto switchDetectDone;

	if (!m_pPointGunAt && !bDontAllowWeaponChange && GetWeapon()->m_eWeaponType != WEAPONTYPE_DETONATOR) {
		if (padUsed->CycleWeaponRightJustDown()) {

			if (TheCamera.PlayerWeaponMode.Mode != CCam::MODE_M16_1STPERSON
				&& TheCamera.PlayerWeaponMode.Mode != CCam::MODE_M16_1STPERSON_RUNABOUT
				&& TheCamera.PlayerWeaponMode.Mode != CCam::MODE_SNIPER
				&& TheCamera.PlayerWeaponMode.Mode != CCam::MODE_SNIPER_RUNABOUT
				&& TheCamera.PlayerWeaponMode.Mode != CCam::MODE_ROCKETLAUNCHER
				&& TheCamera.PlayerWeaponMode.Mode != CCam::MODE_ROCKETLAUNCHER_RUNABOUT
				&& TheCamera.PlayerWeaponMode.Mode != CCam::MODE_CAMERA) {

				for (m_nSelectedWepSlot = m_currentWeapon + 1; m_nSelectedWepSlot < TOTAL_WEAPON_SLOTS; ++m_nSelectedWepSlot) {
					if (HasWeaponSlot(m_nSelectedWepSlot) && GetWeapon(m_nSelectedWepSlot).HasWeaponAmmoToBeUsed()) {
#ifdef FIX_BUGS
						goto switchDetectDone;
#else
						goto spentAmmoCheck;
#endif
					}
				}
				m_nSelectedWepSlot = 0;
#ifdef FIX_BUGS
				goto switchDetectDone;
#endif
			}
		} else if (padUsed->CycleWeaponLeftJustDown()) {
			if (TheCamera.PlayerWeaponMode.Mode != CCam::MODE_M16_1STPERSON
				&& TheCamera.PlayerWeaponMode.Mode != CCam::MODE_SNIPER
				&& TheCamera.PlayerWeaponMode.Mode != CCam::MODE_ROCKETLAUNCHER
				&& TheCamera.PlayerWeaponMode.Mode != CCam::MODE_CAMERA) {

				// I don't know what kind of loop that was
				m_nSelectedWepSlot = m_currentWeapon - 1;
				do {
					if (m_nSelectedWepSlot < 0)
						m_nSelectedWepSlot = TOTAL_WEAPON_SLOTS - 1;

					if (m_nSelectedWepSlot == WEAPONSLOT_UNARMED)
						break;

					if (HasWeaponSlot(m_nSelectedWepSlot) && GetWeapon(m_nSelectedWepSlot).HasWeaponAmmoToBeUsed())
						break;
					
					--m_nSelectedWepSlot;
				} while (m_nSelectedWepSlot != WEAPONSLOT_UNARMED);
#ifdef FIX_BUGS
				goto switchDetectDone;
#endif

			}
		}
	}
	
spentAmmoCheck:
	if (CWeaponInfo::GetWeaponInfo(GetWeapon()->m_eWeaponType)->m_eWeaponFire != WEAPON_FIRE_MELEE
		&& (!padUsed->GetWeapon() || GetWeapon()->m_eWeaponType != WEAPONTYPE_MINIGUN)) {
		if (GetWeapon()->m_nAmmoTotal <= 0) {
			if (TheCamera.PlayerWeaponMode.Mode == CCam::MODE_M16_1STPERSON
				|| TheCamera.PlayerWeaponMode.Mode == CCam::MODE_SNIPER
				|| TheCamera.PlayerWeaponMode.Mode == CCam::MODE_ROCKETLAUNCHER)
				return;

			if (GetWeapon()->m_eWeaponType == WEAPONTYPE_DETONATOR
				&& GetWeapon(WEAPONSLOT_PROJECTILE).m_eWeaponType == WEAPONTYPE_DETONATOR_GRENADE)
				m_nSelectedWepSlot = WEAPONSLOT_PROJECTILE;
			else
				m_nSelectedWepSlot = m_currentWeapon - 1;

			for (; m_nSelectedWepSlot >= WEAPONSLOT_UNARMED; --m_nSelectedWepSlot) {

				// BUG: m_nSelectedWepSlot and GetWeapon(..) takes slot in VC but they compared them against weapon types in whole condition! jeez
#ifdef FIX_BUGS
				if (m_nSelectedWepSlot == WEAPONSLOT_MELEE ||
					GetWeapon(m_nSelectedWepSlot).m_nAmmoTotal > 0 && (m_nSelectedWepSlot != WEAPONSLOT_PROJECTILE || GetWeapon(WEAPONSLOT_PROJECTILE).m_eWeaponType == WEAPONTYPE_DETONATOR_GRENADE)) {
#else
				if (m_nSelectedWepSlot == WEAPONTYPE_BASEBALLBAT && GetWeapon(WEAPONTYPE_BASEBALLBAT).m_eWeaponType == WEAPONTYPE_BASEBALLBAT
					|| GetWeapon(m_nSelectedWepSlot).m_nAmmoTotal > 0
					&& m_nSelectedWepSlot != WEAPONTYPE_MOLOTOV && m_nSelectedWepSlot != WEAPONTYPE_GRENADE && m_nSelectedWepSlot != WEAPONTYPE_TEARGAS) {
#endif
					goto switchDetectDone;
				}
			}
			m_nSelectedWepSlot = WEAPONSLOT_UNARMED;
		}
	}

switchDetectDone:
	if (m_nSelectedWepSlot != m_currentWeapon) {
		if (m_nPedState != PED_ATTACK && m_nPedState != PED_AIM_GUN && m_nPedState != PED_FIGHT) {
			RemoveWeaponAnims(m_currentWeapon, -1000.0f);
			MakeChangesForNewWeapon(m_nSelectedWepSlot);
		}
	}
}

void
CPlayerPed::PlayerControlM16(CPad *padUsed)
{
	ProcessWeaponSwitch(padUsed);
	TheCamera.PlayerExhaustion = (1.0f - (m_fCurrentStamina - -150.0f) / 300.0f) * 0.9f + 0.1f;

	if (padUsed->DuckJustDown() && !bIsDucking && m_nMoveState != PEDMOVE_SPRINT) {
		bCrouchWhenShooting = true;
		SetDuck(60000, true);
	} else if (bIsDucking && (padUsed->DuckJustDown() || m_nMoveState == PEDMOVE_SPRINT)) {
		ClearDuck(true);
		bCrouchWhenShooting = false;
	}

	if (!padUsed->GetTarget() && !m_attachedTo) {
		RestorePreviousState();
		TheCamera.ClearPlayerWeaponMode();
	}

	if (padUsed->GetWeapon() && CTimer::GetTimeInMilliseconds() > GetWeapon()->m_nTimer) {
		if (GetWeapon()->m_eWeaponState == WEAPONSTATE_OUT_OF_AMMO) {
			DMAudio.PlayFrontEndSound(SOUND_WEAPON_SNIPER_SHOT_NO_ZOOM, 0.f);
			GetWeapon()->m_nTimer = CWeaponInfo::GetWeaponInfo(GetWeapon()->m_eWeaponType)->m_nFiringRate + CTimer::GetTimeInMilliseconds();
		} else {
			CVector firePos(0.0f, 0.0f, 0.6f);
			firePos = GetMatrix() * firePos;
			GetWeapon()->Fire(this, &firePos);
			m_nPadDownPressedInMilliseconds = CTimer::GetTimeInMilliseconds();
		}
	} else if (CTimer::GetTimeInMilliseconds() > GetWeapon()->m_nTimer &&
		CTimer::GetTimeInMilliseconds() - CTimer::GetTimeStepInMilliseconds() < GetWeapon()->m_nTimer && GetWeapon()->m_eWeaponState != WEAPONSTATE_OUT_OF_AMMO) {
		DMAudio.PlayFrontEndSound(SOUND_WEAPON_AK47_BULLET_ECHO, GetWeapon()->m_eWeaponType);
	}
	GetWeapon()->Update(m_audioEntityId, nil);
}

void
CPlayerPed::PlayerControlFighter(CPad *padUsed)
{
	gControlPath = 3;
	float leftRight = padUsed->GetPedWalkLeftRight();
	float upDown = padUsed->GetPedWalkUpDown();
	float padMove = CVector2D(leftRight, upDown).Magnitude();

	if (padMove > 0.0f) {
		m_fRotationDest = CGeneral::GetRadianAngleBetweenPoints(0.0f, 0.0f, -leftRight, upDown) - TheCamera.Orientation;
		m_takeAStepAfterAttack = padMove > (2 * PAD_MOVE_TO_GAME_WORLD_MOVE);
		if (padUsed->GetSprint() && padMove > (1 * PAD_MOVE_TO_GAME_WORLD_MOVE))
			bIsAttacking = false;
	}

	if (!CWeaponInfo::GetWeaponInfo(GetWeapon()->m_eWeaponType)->IsFlagSet(WEAPONFLAG_HEAVY) && padUsed->JumpJustDown()) {
		if (m_nEvadeAmount != 0 && m_pEvadingFrom) {
			SetEvasiveDive((CPhysical*)m_pEvadingFrom, 1);
			m_nEvadeAmount = 0;
			m_pEvadingFrom = nil;
		} else {
			SetJump();
		}
	}
}

void
CPlayerPed::PlayerControl1stPersonRunAround(CPad *padUsed)
{
	gControlPath = 2;
	float leftRight = padUsed->GetPedWalkLeftRight();
	float upDown = padUsed->GetPedWalkUpDown();
	float padMove = CVector2D(leftRight, upDown).Magnitude();
	float padMoveInGameUnit = padMove / PAD_MOVE_TO_GAME_WORLD_MOVE;
	bPadAsksToRun = padMoveInGameUnit >= 1.0f;
	if (padMoveInGameUnit > 0.0f) {
		m_fRotationDest = CGeneral::LimitRadianAngle(TheCamera.Orientation);
		m_fMoveSpeed = Min(padMoveInGameUnit, PLAYER_MOVE_ACCELERATION * CTimer::GetTimeStep() + m_fMoveSpeed);
	} else {
		m_fMoveSpeed = 0.0f;
	}

	if (m_nPedState == PED_JUMP) {
		if (bIsInTheAir) {
			if (bUsesCollision && !bHitSteepSlope && (!bHitSomethingLastFrame || m_vecDamageNormal.z > 0.6f)
				&& m_fDistanceTravelled < CTimer::GetTimeStepInSeconds() && m_vecMoveSpeed.MagnitudeSqr() < 0.01f) {

				float angleSin = Sin(m_fRotationCur); // originally sin(DEGTORAD(RADTODEG(m_fRotationCur))) o_O
				float angleCos = Cos(m_fRotationCur);
				ApplyMoveForce(-angleSin * 3.0f, 3.0f * angleCos, 0.05f);
			}
		} else if (bIsLanding) {
			m_fMoveSpeed = 0.0f;
		}
	}

	if (m_nPedState == PED_ANSWER_MOBILE) {
		SetRealMoveAnim();
		return;
	}

	if (!CWeaponInfo::GetWeaponInfo(GetWeapon()->m_eWeaponType)->IsFlagSet(WEAPONFLAG_HEAVY) && padUsed->GetSprint() &&
		!MovesWhileAiming()) {
		m_nMoveState = PEDMOVE_SPRINT;
	}
	if (m_nPedState != PED_FIGHT)
		SetRealMoveAnim();

	if (!bIsInTheAir && !(CWeaponInfo::GetWeaponInfo(GetWeapon()->m_eWeaponType)->IsFlagSet(WEAPONFLAG_HEAVY)) &&
		padUsed->JumpJustDown() && m_nPedState != PED_JUMP) {

		ClearAttack();
		ClearWeaponTarget();
		if (m_nEvadeAmount != 0 && m_pEvadingFrom) {
			SetEvasiveDive((CPhysical*)m_pEvadingFrom, 1);
			m_nEvadeAmount = 0;
			m_pEvadingFrom = nil;
		} else {
			SetJump();
		}
	}

	// FIX: Fact that PlayIdleAnimations only called through PlayerControlZelda was making it visible to only Classic control players. This isn't fair!
#ifdef FIX_BUGS
	if (m_nPedState != PED_FIGHT)
		PlayIdleAnimations(padUsed);
#endif
}

void
CPlayerPed::KeepAreaAroundPlayerClear(void)
{
	BuildPedLists();
	for (int i = 0; i < m_numNearPeds; ++i) {
		CPed *nearPed = m_nearPeds[i];
		if (nearPed->CharCreatedBy == RANDOM_CHAR && nearPed->m_nPedState != PED_DRIVING && !nearPed->DyingOrDead()) {
			if (nearPed->GetIsOnScreen()) {
				if (nearPed->m_objective == OBJECTIVE_NONE) {
					nearPed->SetFindPathAndFlee(this, 5000, true);
				} else {
					if (nearPed->EnteringCar())
						nearPed->QuitEnteringCar();

					nearPed->ClearObjective();
				}
			} else {
				nearPed->FlagToDestroyWhenNextProcessed();
			}
		}
	}
	CVector playerPos = (InVehicle() ? m_pMyVehicle->GetPosition() : GetPosition());

	CVector pos = GetPosition();
	int16 lastVehicle;
	CEntity *vehicles[8];
	CWorld::FindObjectsInRange(pos, CHECK_NEARBY_THINGS_MAX_DIST, true, &lastVehicle, 6, vehicles, false, true, false, false, false);

	for (int i = 0; i < lastVehicle; i++) {
		CVehicle *veh = (CVehicle*)vehicles[i];
		if (veh->VehicleCreatedBy != MISSION_VEHICLE) {
			if (veh->GetStatus() != STATUS_PLAYER && veh->GetStatus() != STATUS_PLAYER_DISABLED) {
				if ((veh->GetPosition() - playerPos).MagnitudeSqr() > 25.0f) {
					veh->AutoPilot.m_nTempAction = TEMPACT_WAIT;
					veh->AutoPilot.m_nTimeTempAction = CTimer::GetTimeInMilliseconds() + 5000;
				} else {
					if (DotProduct2D(playerPos - veh->GetPosition(), veh->GetForward()) > 0.0f)
						veh->AutoPilot.m_nTempAction = TEMPACT_REVERSE;
					else
						veh->AutoPilot.m_nTempAction = TEMPACT_GOFORWARD;

					veh->AutoPilot.m_nTimeTempAction = CTimer::GetTimeInMilliseconds() + 2000;
				}
				CCarCtrl::PossiblyRemoveVehicle(veh);
			}
		}
	}
}

void
CPlayerPed::EvaluateNeighbouringTarget(CEntity *candidate, CEntity **targetPtr, float *lastCloseness, float distLimit, float angleOffset, bool lookToLeft, bool priority)
{
	// priority param is unused
	CVector distVec = candidate->GetPosition() - GetPosition();
	if (distVec.Magnitude2D() <= distLimit) {
		if (!DoesTargetHaveToBeBroken(candidate->GetPosition(), GetWeapon())) {
			float angleBetweenUs = CGeneral::GetATanOfXY(candidate->GetPosition().x - TheCamera.GetPosition().x,
				candidate->GetPosition().y - TheCamera.GetPosition().y);

			angleBetweenUs = CGeneral::LimitAngle(angleBetweenUs - angleOffset);
			float closeness;
			if (lookToLeft) {
				closeness = angleBetweenUs > 0.0f ? -Abs(angleBetweenUs) : -100000.0f;
			} else {
				closeness = angleBetweenUs > 0.0f ? -100000.0f : -Abs(angleBetweenUs);
			}

			if (closeness > *lastCloseness) {
				*targetPtr = candidate;
				*lastCloseness = closeness;
			}
		}
	}
}

void
CPlayerPed::EvaluateTarget(CEntity *candidate, CEntity **targetPtr, float *lastCloseness, float distLimit, float angleOffset, bool priority)
{
	CVector distVec = candidate->GetPosition() - GetPosition();
	float dist = distVec.Magnitude2D();
	if (dist <= distLimit) {
		if (!DoesTargetHaveToBeBroken(candidate->GetPosition(), GetWeapon())) {
			float angleBetweenUs = CGeneral::GetATanOfXY(distVec.x, distVec.y);
			angleBetweenUs = CGeneral::LimitAngle(angleBetweenUs - angleOffset);

			float closeness = -dist - 5.0f * Abs(angleBetweenUs);
			if (priority) {
				closeness += 30.0f;
			}

			if (closeness > *lastCloseness) {
				*targetPtr = candidate;
				*lastCloseness = closeness;
			}
		}
	}
}

bool
CPlayerPed::CanIKReachThisTarget(CVector target, CWeapon* weapon, bool zRotImportant)
{
	float angleToFace = CGeneral::GetRadianAngleBetweenPoints(target.x, target.y, GetPosition().x, GetPosition().y);
	float angleDiff = CGeneral::LimitRadianAngle(angleToFace - m_fRotationCur);

	return (!zRotImportant || CWeaponInfo::GetWeaponInfo(weapon->m_eWeaponType)->IsFlagSet(WEAPONFLAG_CANAIM_WITHARM) || Abs(angleDiff) <= HALFPI) &&
		(CWeaponInfo::GetWeaponInfo(weapon->m_eWeaponType)->IsFlagSet(WEAPONFLAG_CANAIM_WITHARM) || Abs(target.z - GetPosition().z) <= (target - GetPosition()).Magnitude2D());
}

void
CPlayerPed::RotatePlayerToTrackTarget(void)
{
	if (CWeaponInfo::GetWeaponInfo(GetWeapon()->m_eWeaponType)->IsFlagSet(WEAPONFLAG_CANAIM_WITHARM))
		return;

	float angleToFace = CGeneral::GetRadianAngleBetweenPoints(
		m_pPointGunAt->GetPosition().x, m_pPointGunAt->GetPosition().y,
		GetPosition().x, GetPosition().y);

	float angleDiff = CGeneral::LimitRadianAngle(m_fRotationCur - angleToFace);
	if (angleDiff < -DEGTORAD(25.0f)) {
		m_fRotationCur -= angleDiff + DEGTORAD(25.0f);
		m_fRotationDest -= angleDiff + DEGTORAD(25.0f);

	} else if (angleDiff > DEGTORAD(25.0f)) {
		m_fRotationCur -= angleDiff - DEGTORAD(25.0f);
		m_fRotationDest -= angleDiff - DEGTORAD(25.0f);
	}
}

bool
CPlayerPed::FindNextWeaponLockOnTarget(CEntity *previousTarget, bool lookToLeft)
{
	CEntity *nextTarget = nil;
	float weaponRange = CWeaponInfo::GetWeaponInfo(GetWeapon()->m_eWeaponType)->m_fRange;
	// nextTarget = nil; // duplicate
	float lastCloseness = -10000.0f;
	// CGeneral::GetATanOfXY(GetForward().x, GetForward().y); // unused
	CVector distVec = previousTarget->GetPosition() - TheCamera.GetPosition();
	float referenceBeta = CGeneral::GetATanOfXY(distVec.x, distVec.y);

	for (int h = CPools::GetPedPool()->GetSize() - 1; h >= 0; h--) {
		CPed *pedToCheck = CPools::GetPedPool()->GetSlot(h);
		if (pedToCheck) {
			if (pedToCheck != this && pedToCheck != previousTarget) {
				if (!pedToCheck->DyingOrDead()
#ifndef AIMING_VEHICLE_OCCUPANTS // Mobile thing
					&& (!pedToCheck->bInVehicle || (pedToCheck->m_pMyVehicle && pedToCheck->m_pMyVehicle->IsBike()))
#endif
					&& pedToCheck->m_leader != this && !pedToCheck->bNeverEverTargetThisPed
					&& OurPedCanSeeThisOne(pedToCheck, true) && CanIKReachThisTarget(pedToCheck->GetPosition(), GetWeapon(), true)) {

					EvaluateNeighbouringTarget(pedToCheck, &nextTarget, &lastCloseness,
						weaponRange, referenceBeta, lookToLeft, IsThisPedAnAimingPriority(pedToCheck));
				}
			}
		}
	}
	for (int i = 0; i < ARRAY_SIZE(m_nTargettableObjects); i++) {
		CObject *obj = CPools::GetObjectPool()->GetAt(m_nTargettableObjects[i]);
		if (obj && !obj->bHasBeenDamaged && CanIKReachThisTarget(obj->GetPosition(), GetWeapon(), true))
			EvaluateNeighbouringTarget(obj, &nextTarget, &lastCloseness, weaponRange, referenceBeta, lookToLeft, true);
	}
	if (!nextTarget)
		return false;

	SetWeaponLockOnTarget(nextTarget);
	bDontAllowWeaponChange = true;
	SetPointGunAt(nextTarget);
	return true;
}

bool
CPlayerPed::FindWeaponLockOnTarget(void)
{
	CEntity *nextTarget = nil;
	float weaponRange = CWeaponInfo::GetWeaponInfo(GetWeapon()->m_eWeaponType)->m_fRange;

	if (m_pPointGunAt) {
		CVector distVec = m_pPointGunAt->GetPosition() - GetPosition();
		if (distVec.Magnitude2D() > weaponRange) {
			SetWeaponLockOnTarget(nil);
			return false;
		} else {
			return true;
		}
	}

	// nextTarget = nil; // duplicate
	float lastCloseness = -10000.0f;
	float referenceBeta = CGeneral::GetATanOfXY(GetForward().x, GetForward().y);
	for (int h = CPools::GetPedPool()->GetSize() - 1; h >= 0; h--) {
		CPed *pedToCheck = CPools::GetPedPool()->GetSlot(h);
		if (pedToCheck) {
			if (pedToCheck != this) {
				if (!pedToCheck->DyingOrDead()
#ifndef AIMING_VEHICLE_OCCUPANTS // Mobile thing
					&& (!pedToCheck->bInVehicle || (pedToCheck->m_pMyVehicle && pedToCheck->m_pMyVehicle->IsBike()))
#endif
					&& pedToCheck->m_leader != this && !pedToCheck->bNeverEverTargetThisPed
					&& OurPedCanSeeThisOne(pedToCheck) && CanIKReachThisTarget(pedToCheck->GetPosition(), GetWeapon(), true)) {

					EvaluateTarget(pedToCheck, &nextTarget, &lastCloseness,
						weaponRange, referenceBeta, IsThisPedAnAimingPriority(pedToCheck));
				}
			}
		}
	}
	for (int i = 0; i < ARRAY_SIZE(m_nTargettableObjects); i++) {
		CObject *obj = CPools::GetObjectPool()->GetAt(m_nTargettableObjects[i]);
		if (obj && !obj->bHasBeenDamaged && CanIKReachThisTarget(obj->GetPosition(), GetWeapon(), true))
			EvaluateTarget(obj, &nextTarget, &lastCloseness, weaponRange, referenceBeta, true);
	}
	if (!nextTarget)
		return false;

	SetWeaponLockOnTarget(nextTarget);
	bDontAllowWeaponChange = true;
	SetPointGunAt(nextTarget);
	Say(SOUND_PED_AIMING);
	return true;
}

void
CPlayerPed::ProcessAnimGroups(void)
{
	AssocGroupId groupToSet;
#ifdef PC_PLAYER_CONTROLS
	if ((m_fWalkAngle <= -DEGTORAD(50.0f) || m_fWalkAngle >= DEGTORAD(50.0f))
		&& TheCamera.Cams[TheCamera.ActiveCam].Using3rdPersonMouseCam()
		&& CanStrafeOrMouseControl()) {

		if (m_fWalkAngle >= -DEGTORAD(130.0f) && m_fWalkAngle <= DEGTORAD(130.0f)) {
			if (m_fWalkAngle > 0.0f) {
				if (GetWeapon()->m_eWeaponType == WEAPONTYPE_ROCKETLAUNCHER)
					groupToSet = ASSOCGRP_ROCKETLEFT;
				else if (GetWeapon()->m_eWeaponType == WEAPONTYPE_CHAINSAW ||
					GetWeapon()->m_eWeaponType == WEAPONTYPE_FLAMETHROWER ||
					GetWeapon()->m_eWeaponType == WEAPONTYPE_MINIGUN)
					groupToSet = ASSOCGRP_CHAINSAWLEFT;
				else
					groupToSet = ASSOCGRP_PLAYERLEFT;
			} else {
				if (GetWeapon()->m_eWeaponType == WEAPONTYPE_ROCKETLAUNCHER)
					groupToSet = ASSOCGRP_ROCKETRIGHT;
				else if (GetWeapon()->m_eWeaponType == WEAPONTYPE_CHAINSAW ||
					GetWeapon()->m_eWeaponType == WEAPONTYPE_FLAMETHROWER ||
					GetWeapon()->m_eWeaponType == WEAPONTYPE_MINIGUN)
					groupToSet = ASSOCGRP_CHAINSAWRIGHT;
				else
					groupToSet = ASSOCGRP_PLAYERRIGHT;
			}
		} else {
			if (GetWeapon()->m_eWeaponType == WEAPONTYPE_ROCKETLAUNCHER)
				groupToSet = ASSOCGRP_ROCKETBACK;
			else if (GetWeapon()->m_eWeaponType == WEAPONTYPE_CHAINSAW ||
				GetWeapon()->m_eWeaponType == WEAPONTYPE_FLAMETHROWER ||
				GetWeapon()->m_eWeaponType == WEAPONTYPE_MINIGUN)
				groupToSet = ASSOCGRP_CHAINSAWBACK;
			else
				groupToSet = ASSOCGRP_PLAYERBACK;
		}
	} else
#endif
	{
		if (GetWeapon()->m_eWeaponType == WEAPONTYPE_ROCKETLAUNCHER) {
			groupToSet = ASSOCGRP_PLAYERROCKET;
		} else {
			if (GetWeapon()->m_eWeaponType == WEAPONTYPE_BASEBALLBAT
				 || GetWeapon()->m_eWeaponType == WEAPONTYPE_MACHETE)
				groupToSet = ASSOCGRP_PLAYERBBBAT;
			else if (GetWeapon()->m_eWeaponType == WEAPONTYPE_CHAINSAW ||
				GetWeapon()->m_eWeaponType == WEAPONTYPE_FLAMETHROWER ||
				GetWeapon()->m_eWeaponType == WEAPONTYPE_MINIGUN)
				groupToSet = ASSOCGRP_PLAYERCHAINSAW;
			else if (GetWeapon()->m_eWeaponType != WEAPONTYPE_COLT45 && GetWeapon()->m_eWeaponType != WEAPONTYPE_UZI
				// I hope this is an inlined function...
				&& GetWeapon()->m_eWeaponType != WEAPONTYPE_PYTHON && GetWeapon()->m_eWeaponType != WEAPONTYPE_TEC9
				&& GetWeapon()->m_eWeaponType != WEAPONTYPE_SILENCED_INGRAM && GetWeapon()->m_eWeaponType != WEAPONTYPE_MP5
				&& GetWeapon()->m_eWeaponType != WEAPONTYPE_GOLFCLUB && GetWeapon()->m_eWeaponType != WEAPONTYPE_KATANA
				&& GetWeapon()->m_eWeaponType != WEAPONTYPE_CAMERA) {
				if (!GetWeapon()->IsType2Handed()) {
					groupToSet = ASSOCGRP_PLAYER;
				} else {
					groupToSet = ASSOCGRP_PLAYER2ARMED;
				}
			} else {
				groupToSet = ASSOCGRP_PLAYER1ARMED;
			}
		}
	}

	if (m_animGroup != groupToSet) {
		m_animGroup = groupToSet;
		ReApplyMoveAnims();
	}
}

void
CPlayerPed::ProcessPlayerWeapon(CPad *padUsed)
{
	CWeaponInfo *weaponInfo = CWeaponInfo::GetWeaponInfo(GetWeapon()->m_eWeaponType);
	if (m_bHasLockOnTarget && !m_pPointGunAt) {
		TheCamera.ClearPlayerWeaponMode();
		CWeaponEffects::ClearCrossHair();
		ClearPointGunAt();
	}

	if (padUsed->DuckJustDown() && !bIsDucking && m_nMoveState != PEDMOVE_SPRINT) {
#ifdef FIX_BUGS
		// fix tommy being locked into looking at the same spot if you duck just after starting to shoot
		if(!m_pPointGunAt)
			ClearPointGunAt();
#endif
		bCrouchWhenShooting = true;
		SetDuck(60000, true);
	} else if (bIsDucking && (padUsed->DuckJustDown() || m_nMoveState == PEDMOVE_SPRINT ||
		padUsed->GetSprint() || padUsed->JumpJustDown() || padUsed->ExitVehicleJustDown())) {

#ifdef FIX_BUGS
		// same fix as above except for standing up
		if(!m_pPointGunAt)
			ClearPointGunAt();
#endif
		ClearDuck(true);
		bCrouchWhenShooting = false;
	}

	if(weaponInfo->IsFlagSet(WEAPONFLAG_CANAIM))
		m_wepAccuracy = 95;
	else
		m_wepAccuracy = 100;

	if (!m_pFire) {
		eWeaponType weapon = GetWeapon()->m_eWeaponType;
		if (CWeaponInfo::UsesScopeAim(weapon)) {

			if (padUsed->TargetJustDown() || TheCamera.m_bJustJumpedOutOf1stPersonBecauseOfTarget) {
				// The sights start from wherever the body happens to face, so a player turned
				// towards the camera aimed a full half turn backwards.  The free cam did line the
				// two up, but only for the mouse camera - a pad never reached it.  Turn the player
				// to the camera whenever we come off the ordinary follow camera, which is the
				// only place the two can disagree.
				if (TheCamera.Cams[TheCamera.ActiveCam].Mode == CCam::MODE_FOLLOWPED) {
					m_fRotationCur = CGeneral::LimitRadianAngle(-TheCamera.Orientation);
					m_fRotationDest = m_fRotationCur;
					SetHeading(m_fRotationCur);
				}
				if (weapon == WEAPONTYPE_ROCKETLAUNCHER)
					TheCamera.SetNewPlayerWeaponMode(CCam::MODE_ROCKETLAUNCHER, 0, 0);
				else if (weapon == WEAPONTYPE_SNIPERRIFLE || weapon == WEAPONTYPE_LASERSCOPE)
					TheCamera.SetNewPlayerWeaponMode(CCam::MODE_SNIPER, 0, 0);
				else if (weapon == WEAPONTYPE_CAMERA)
					TheCamera.SetNewPlayerWeaponMode(CCam::MODE_CAMERA, 0, 0);
				else
					TheCamera.SetNewPlayerWeaponMode(CCam::MODE_M16_1STPERSON, 0, 0);

				m_fMoveSpeed = 0.0f;
				CAnimManager::BlendAnimation(GetClump(), ASSOCGRP_STD, ANIM_STD_IDLE, 1000.0f);
				SetPedState(PED_SNIPER_MODE);
				return;
			}
			if (!TheCamera.Using1stPersonWeaponMode())
				if (weapon == WEAPONTYPE_ROCKETLAUNCHER || weapon == WEAPONTYPE_SNIPERRIFLE || weapon == WEAPONTYPE_LASERSCOPE || weapon == WEAPONTYPE_CAMERA)
					return;
		}
	}

	// A shot from the hip plays the weapon animation from its very start and lets it run its
	// tail out afterwards, and the ready pose covers neither, so the arm goes up and comes
	// down at a pace of its own - and letting go of aim in the middle of a burst drops the
	// player onto that path mid-shot.  Asking for aim keeps every shot on the one path.
	// Fists, melee and thrown weapons never aim and are left alone.
	bool mayFire = true;
	if (!bInVehicle && !padUsed->GetTarget() &&
		weaponInfo->IsFlagSet(WEAPONFLAG_CANAIM) && weaponInfo->m_eWeaponFire != WEAPON_FIRE_MELEE)
		mayFire = false;

	if (padUsed->GetWeapon() && m_nMoveState != PEDMOVE_SPRINT && mayFire) {
		if (m_nSelectedWepSlot == m_currentWeapon) {
			if (m_pPointGunAt) {
				if (m_nPedState == PED_ATTACK) {
					m_fAttackButtonCounter *= Pow(0.94f, CTimer::GetTimeStep());
				} else {
					m_fAttackButtonCounter = 0.0f;
				}
				SetAttack(m_pPointGunAt);
			} else {
				if (m_nPedState == PED_ATTACK) {
					if (padUsed->WeaponJustDown()) {
						m_bHaveTargetSelected = true;
					} else if (!m_bHaveTargetSelected) {
						m_fAttackButtonCounter += CTimer::GetTimeStepNonClipped();
					}
				} else {
					m_fAttackButtonCounter = 0.0f;
					m_bHaveTargetSelected = false;
				}
				if (GetWeapon()->m_eWeaponType != WEAPONTYPE_UNARMED && GetWeapon()->m_eWeaponType != WEAPONTYPE_BRASSKNUCKLE &&
					!weaponInfo->IsFlagSet(WEAPONFLAG_FIGHTMODE)) {

					if (GetWeapon()->m_eWeaponType != WEAPONTYPE_DETONATOR && GetWeapon()->m_eWeaponType != WEAPONTYPE_DETONATOR_GRENADE ||
						padUsed->WeaponJustDown())
						SetAttack(nil);

				} else if (padUsed->WeaponJustDown()) {
					if (m_fMoveSpeed < 1.0f || m_nPedState == PED_FIGHT)
						StartFightAttack(padUsed->GetWeapon());
					else
						SetAttack(nil);
				}
			}
		}
	} else {
		m_pedIK.m_flags &= ~CPedIK::LOOKAROUND_HEAD_ONLY;
		if (m_nPedState == PED_ATTACK) {
			m_bHaveTargetSelected = true;
			bIsAttacking = false;
		}
	}

#ifdef FREE_CAM
	static int8 changedHeadingRate = 0;
	static int8 pointedGun = 0;
	if (changedHeadingRate == 2) changedHeadingRate = 1;
	if (pointedGun == 2) pointedGun = 1;

	// Rotate player/arm when shooting. We don't have auto-rotation anymore
	if (CCamera::m_bUseMouse3rdPerson && CCamera::bFreeCam &&
		m_nSelectedWepSlot == m_currentWeapon && m_nMoveState != PEDMOVE_SPRINT) {

#define CAN_AIM_WITH_ARM (weaponInfo->IsFlagSet(WEAPONFLAG_CANAIM_WITHARM) && !bIsDucking && !bCrouchWhenShooting)
		// Weapons except throwable and melee ones
		if (weaponInfo->m_nWeaponSlot > 2) {
			if ((padUsed->GetTarget() && CAN_AIM_WITH_ARM) || padUsed->GetWeapon()) {
				float limitedCam = CGeneral::LimitRadianAngle(-TheCamera.Orientation);

				m_cachedCamSource = TheCamera.Cams[TheCamera.ActiveCam].Source;
				m_cachedCamFront = TheCamera.Cams[TheCamera.ActiveCam].Front;
				m_cachedCamUp = TheCamera.Cams[TheCamera.ActiveCam].Up;
				
				// On this one we can rotate arm.
				if (CAN_AIM_WITH_ARM) {
					pointedGun = 2;
					m_bFreeAimActive = true;
					SetLookFlag(limitedCam, true, true);
					SetAimFlag(limitedCam);
					SetLookTimer(INT32_MAX);
					((CPlayerPed*)this)->m_fFPSMoveHeading = TheCamera.Find3rdPersonQuickAimPitch();
					if (m_nPedState != PED_ATTACK && m_nPedState != PED_AIM_GUN) {
						// This is a seperate ped state just for pointing gun. Used for target button
						SetPointGunAt(nil);
					}
				} else {
					m_fRotationDest = limitedCam;
					changedHeadingRate = 2;
					m_headingRate = 12.5f;

					// Anim. fix for shotgun, ak47 and m16 (we must finish rot. it quickly)
					if (weaponInfo->IsFlagSet(WEAPONFLAG_CANAIM) && padUsed->WeaponJustDown()) {
						m_fRotationCur = CGeneral::LimitRadianAngle(m_fRotationCur);
						float limitedRotDest = m_fRotationDest;

						if (m_fRotationCur - PI > m_fRotationDest) {
							limitedRotDest += 2 * PI;
						} else if (PI + m_fRotationCur < m_fRotationDest) {
							limitedRotDest -= 2 * PI;
						}

						m_fRotationCur += (limitedRotDest - m_fRotationCur) / 2;
					}
				}
			}
		}
#undef CAN_AIM_WITH_ARM
	}
	if (changedHeadingRate == 1) {
		changedHeadingRate = 0;
		RestoreHeadingRate();
	}
	if (pointedGun == 1) {
		if (m_nPedState == PED_ATTACK) {
			if (!padUsed->GetWeapon() && (m_pedIK.m_flags & CPedIK::GUN_POINTED_SUCCESSFULLY) == 0) {
				float limitedCam = CGeneral::LimitRadianAngle(-TheCamera.Orientation);

				SetAimFlag(limitedCam);
				((CPlayerPed*)this)->m_fFPSMoveHeading = TheCamera.Find3rdPersonQuickAimPitch();
				m_bFreeAimActive = true;
			}
		} else {
			pointedGun = 0;
			ClearPointGunAt();
		}
	}
#endif

	if (padUsed->GetTarget() && m_nSelectedWepSlot == m_currentWeapon && m_nMoveState != PEDMOVE_SPRINT && !TheCamera.Using1stPersonWeaponMode() && weaponInfo->IsFlagSet(WEAPONFLAG_CANAIM)) {
		if (m_pPointGunAt) {
			// what??
			if (!m_pPointGunAt
#ifdef FREE_CAM
				|| (!CCamera::bFreeCam && CCamera::m_bUseMouse3rdPerson)
#else
				|| CCamera::m_bUseMouse3rdPerson
#endif		
			) {
				ClearWeaponTarget();
				return;
			}

			if (m_pPointGunAt->IsPed() && (
#ifndef AIMING_VEHICLE_OCCUPANTS
				(((CPed*)m_pPointGunAt)->bInVehicle && (!((CPed*)m_pPointGunAt)->m_pMyVehicle || !((CPed*)m_pPointGunAt)->m_pMyVehicle->IsBike())) ||
#endif
				!CGame::nastyGame && ((CPed*)m_pPointGunAt)->DyingOrDead())) {
				ClearWeaponTarget();
				return;
			}
			if (CPlayerPed::DoesTargetHaveToBeBroken(m_pPointGunAt->GetPosition(), GetWeapon()) || 
				(!bCanPointGunAtTarget && !weaponInfo->IsFlagSet(WEAPONFLAG_CANAIM_WITHARM))) { // this line isn't on Mobile, idk why
				ClearWeaponTarget();
				return;
			}

			if (m_pPointGunAt) {
				RotatePlayerToTrackTarget();
			}

			if (m_pPointGunAt) {
				if (padUsed->ShiftTargetLeftJustDown())
					FindNextWeaponLockOnTarget(m_pPointGunAt, true);
				if (padUsed->ShiftTargetRightJustDown())
					FindNextWeaponLockOnTarget(m_pPointGunAt, false);
			}
			TheCamera.SetNewPlayerWeaponMode(CCam::MODE_SYPHON, 0, 0);
			TheCamera.UpdateAimingCoors(m_pPointGunAt->GetPosition());

		} else if (!CCamera::m_bUseMouse3rdPerson) {
			if (padUsed->TargetJustDown() || TheCamera.m_bJustJumpedOutOf1stPersonBecauseOfTarget)
				FindWeaponLockOnTarget();
		}
	} else if (m_pPointGunAt) {
		ClearWeaponTarget();
	}

	if (m_pPointGunAt) {
		CVector markPos;
		if (m_pPointGunAt->IsPed()) {
			((CPed*)m_pPointGunAt)->m_pedIK.GetComponentPosition(markPos, PED_MID);
		} else {
			markPos = m_pPointGunAt->GetPosition();
		}
		if (bCanPointGunAtTarget) {
			CWeaponEffects::MarkTarget(markPos, 64, 0, 0, 255, 0.8f);
		} else {
			CWeaponEffects::MarkTarget(markPos, 64, 32, 0, 255, 0.8f);
		}
	}
	m_bHasLockOnTarget = m_pPointGunAt != nil;
}

// Walking with a two handed gun raised.  Vice City only lets the player walk and aim at once with
// its mouse camera and the handguns; the rifles, the shotguns and the SMGs hold their firing
// animation over the whole body and stand him still.  With this on, holding aim with one of them
// walks the way the mouse camera does - facing where the camera looks, stepping sideways and back
// - and the weapon animation keeps only the body above the hips, see ProcessUpperBodyWeaponAnims.
// The handguns aim with the arm alone and already walk; the heavy weapons are left as they are.
bool
CPlayerPed::MovesWhileAiming(void)
{
	if (!bMoveWhileAiming || bInVehicle || bIsDucking || m_pPointGunAt != nil)
		return false;
	if (!CPad::GetPad(0)->GetTarget() || TheCamera.Using1stPersonWeaponMode() ||
	    !TheCamera.Cams[0].Using3rdPersonMouseCam())
		return false;
	CWeaponInfo *info = CWeaponInfo::GetWeaponInfo(GetWeapon()->m_eWeaponType);
	return info->IsFlagSet(WEAPONFLAG_CANAIM) && !info->IsFlagSet(WEAPONFLAG_CANAIM_WITHARM) &&
		!info->IsFlagSet(WEAPONFLAG_HEAVY) && info->m_eWeaponFire != WEAPON_FIRE_MELEE &&
		info->m_AnimToPlay != ASSOCGRP_STD;
}

// The weapon's own animations are partial ones that still carry the hips and the legs.  Marked,
// the frame update leaves those bones to the walk; unmarked again the moment aim is let go.
void
CPlayerPed::ProcessUpperBodyWeaponAnims(void)
{
	bool upperOnly = MovesWhileAiming();
	AssocGroupId weaponGroup = CWeaponInfo::GetWeaponInfo(GetWeapon()->m_eWeaponType)->m_AnimToPlay;
	for (CAnimBlendAssociation *assoc = RpAnimBlendClumpGetFirstAssociation(GetClump()); assoc; assoc = RpAnimBlendGetNextAssociation(assoc)) {
		if (upperOnly && assoc->IsPartial() && assoc->groupId == weaponGroup)
			assoc->flags |= ASSOC_UPPERBODY;
		else
			assoc->flags &= ~ASSOC_UPPERBODY;
	}
}

bool
CPlayerPed::MovementDisabledBecauseOfTargeting(void)
{
	return m_pPointGunAt && !CWeaponInfo::GetWeaponInfo(GetWeapon()->m_eWeaponType)->IsFlagSet(WEAPONFLAG_CANAIM_WITHARM);
}

void
CPlayerPed::PlayerControlZelda(CPad *padUsed)
{
	gControlPath = 4;
	float smoothSprayRate = DoWeaponSmoothSpray();
	float camOrientation = TheCamera.Orientation;
	float leftRight = padUsed->GetPedWalkLeftRight();
	float upDown = padUsed->GetPedWalkUpDown();
	float padMoveInGameUnit;
	bool smoothSprayWithoutMove = false;

	if (MovementDisabledBecauseOfTargeting()) {
		upDown = 0.0f;
		leftRight = 0.0f;
	}

	if (smoothSprayRate > 0.0f && upDown > 0.0f) {
		padMoveInGameUnit = 0.0f;
		smoothSprayWithoutMove = true;
	} else {
		padMoveInGameUnit = CVector2D(leftRight, upDown).Magnitude() / PAD_MOVE_TO_GAME_WORLD_MOVE;
	}

#ifdef FREE_CAM
	if (TheCamera.Cams[0].Using3rdPersonMouseCam() && smoothSprayRate > 0.0f) {
		padMoveInGameUnit = 0.0f;
		smoothSprayWithoutMove = false;
	}
#endif

	bPadAsksToRun = padMoveInGameUnit >= 1.0f;

	if (padMoveInGameUnit > 0.0f || smoothSprayWithoutMove) {
		float padHeading = CGeneral::GetRadianAngleBetweenPoints(0.0f, 0.0f, -leftRight, upDown);
		float neededTurn = CGeneral::LimitRadianAngle(padHeading - camOrientation);
		if (smoothSprayRate > 0.0f) {
			m_fRotationDest = m_fRotationCur - leftRight / 128.0f * smoothSprayRate * CTimer::GetTimeStep();
		} else {
			m_fRotationDest = neededTurn;
		}

		// The speed climbed from nothing by 0.07 a step, and walking only becomes running at 1,
		// so a stick pushed all the way from standing still walked for a good half second first.
		// Five times the climb reaches a run in about a tenth of a second and keeps a short ramp,
		// so the animations still blend over rather than jump.
		float maxAcc = PLAYER_MOVE_ACCELERATION * CTimer::GetTimeStep();
		m_fMoveSpeed = Min(padMoveInGameUnit, m_fMoveSpeed + maxAcc);

	} else {
		m_fMoveSpeed = 0.0f;
	}

	if (m_nPedState == PED_JUMP) {
		if (bIsInTheAir) {
			if (bUsesCollision && !bHitSteepSlope && (!bHitSomethingLastFrame || m_vecDamageNormal.z > 0.6f)
				&& m_fDistanceTravelled < CTimer::GetTimeStepInSeconds() && m_vecMoveSpeed.MagnitudeSqr() < 0.01f) {

				float angleSin = Sin(m_fRotationCur); // originally sin(DEGTORAD(RADTODEG(m_fRotationCur))) o_O
				float angleCos = Cos(m_fRotationCur);
				ApplyMoveForce(-angleSin * 3.0f, 3.0f * angleCos, 0.05f);
			}
		} else if (bIsLanding) {
			m_fMoveSpeed = 0.0f;
		}
	}

	if (m_nPedState == PED_ANSWER_MOBILE) {
		SetRealMoveAnim();
		return;
	}

	if (!CWeaponInfo::GetWeaponInfo(GetWeapon()->m_eWeaponType)->IsFlagSet(WEAPONFLAG_HEAVY) && padUsed->GetSprint()) {
		if (!m_pCurrentPhysSurface || (!m_pCurrentPhysSurface->bInfiniteMass || m_pCurrentPhysSurface->m_phy_flagA08))
			m_nMoveState = PEDMOVE_SPRINT;
	}

	if (m_nPedState != PED_FIGHT)
		SetRealMoveAnim();

	if (!bIsInTheAir && !CWeaponInfo::GetWeaponInfo(GetWeapon()->m_eWeaponType)->IsFlagSet(WEAPONFLAG_HEAVY)
		&& padUsed->JumpJustDown() && m_nPedState != PED_JUMP) {
		ClearAttack();
		ClearWeaponTarget();
		if (m_nEvadeAmount != 0 && m_pEvadingFrom) {
			SetEvasiveDive((CPhysical*)m_pEvadingFrom, 1);
			m_nEvadeAmount = 0;
			m_pEvadingFrom = nil;
		} else {
			SetJump();
		}
	}
	PlayIdleAnimations(padUsed);
}

// Finds nice positions for peds to duck and shoot player. And it's inside PlayerPed, this is treachery!
void
CPlayerPed::FindNewAttackPoints(void)
{
	for (int i=0; i<ARRAY_SIZE(m_pPedAtSafePos); i++) {
		CPed *safeNeighbour = m_pPedAtSafePos[i];
		if (safeNeighbour) {
			if (safeNeighbour->m_nPedState == PED_DEAD || safeNeighbour->m_pedInObjective != this) {
				m_vecSafePos[i].x = 0.0f;
				m_vecSafePos[i].y = 0.0f;
				m_vecSafePos[i].z = 0.0f;
				m_pPedAtSafePos[i] = nil;
			}
		} else {
			m_vecSafePos[i].x = 0.0f;
			m_vecSafePos[i].y = 0.0f;
			m_vecSafePos[i].z = 0.0f;
		}
	}
	CEntity *entities[6];
	int16 numEnts;
	float rightMult, fwdMult;
	CWorld::FindObjectsInRange(GetPosition(), 18.0f, true, &numEnts, 6, entities, true, false, false, true, false);
	for (int i = 0; i < numEnts; ++i) {
		CEntity *ent = entities[i];
		int16 mi = ent->GetModelIndex();
		if (!ent->IsObject() || ((CObject*)ent)->m_nSpecialCollisionResponseCases == COLLRESPONSE_FENCEPART)
			if (!IsTreeModel(mi))
				continue;

		if (mi == MI_TRAFFICLIGHTS) {
			rightMult = 2.957f;
			fwdMult = 0.147f;

		} else if (mi == MI_SINGLESTREETLIGHTS1) {
			rightMult = 0.744f;
			fwdMult = 0.0f;

		} else if (mi == MI_SINGLESTREETLIGHTS2) {
			rightMult = 0.043f;
			fwdMult = 0.0f;

		} else if (mi == MI_SINGLESTREETLIGHTS3) {
			rightMult = 1.143f;
			fwdMult = 0.145f;

		} else if (mi == MI_DOUBLESTREETLIGHTS) {
			rightMult = 0.744f;
			fwdMult = 0.0f;

		} else if (mi == MI_LAMPPOST1) {
			rightMult = 0.744f;
			fwdMult = 0.0f;

		} else if (mi == MI_TRAFFICLIGHT01) {
			rightMult = 2.957f;
			fwdMult = 0.147f;

		} else if (mi == MI_LITTLEHA_POLICE) {
			rightMult = 0.0f;
			fwdMult = 0.0f;

		} else if (mi == MI_PARKBENCH) {
			rightMult = 0.0f;
			fwdMult = 0.0f;

		} else if (IsTreeModel(mi)) {
			rightMult = 0.0f;
			fwdMult = 0.0f;
		} else
			continue;

		CVector entAttackPoint(rightMult * ent->GetRight().x + fwdMult * ent->GetForward().x + ent->GetPosition().x,
			rightMult * ent->GetRight().y + fwdMult * ent->GetForward().y + ent->GetPosition().y,
			ent->GetPosition().z);
		CVector attackerPos = GetPosition() - entAttackPoint; // for now it's dist, not attackerPos
		CVector dirTowardsUs = attackerPos;
		dirTowardsUs.Normalise();
		dirTowardsUs *= 2.0f;
		attackerPos = entAttackPoint - dirTowardsUs; // to make cop farther from us
		CPedPlacement::FindZCoorForPed(&attackerPos);
		if (CPedPlacement::IsPositionClearForPed(attackerPos))
			m_vecSafePos[i] = attackerPos;
	}
}


// StickDebug=1 writes what the player does frame by frame to reVC_debug.log in the game folder:
// forty five frames from the moment the stick is pushed hard from rest, and forty five from the
// moment Target/Aim goes down.  A soft blend or an arm that settles over a few frames cannot be
// read off a still image, so it is written down instead.  Nothing here changes how the game plays.
extern float gIKDebugUaYaw;
extern float gIKDebugUaPitch;
extern float gIKDebugClavYaw;
extern int gIKDebugUaStatus;

static void
WritePlayerDebugLog(CPlayerPed *ped, CPad *pad)
{
	static int32 moveFrames = 0;
	static int32 aimFrames = 0;
	static bool stickWasAtRest = true;
	static CVector lastPos;

	if (pad == nil)
		return;

	float sx = pad->GetPedWalkLeftRight();
	float sy = pad->GetPedWalkUpDown();
	float stickLen = Sqrt(SQR(sx) + SQR(sy));

	const char *startReason = nil;
	if (moveFrames == 0 && aimFrames == 0) {
		if (stickWasAtRest && stickLen >= 100.0f && ped->m_fMoveSpeed == 0.0f) {
			moveFrames = 45;
			startReason = "MOVE";
		} else if (pad->TargetJustDown()) {
			aimFrames = 45;
			startReason = "AIM";
		}
	}
	stickWasAtRest = stickLen < 20.0f;

	if (moveFrames == 0 && aimFrames == 0)
		return;

	FILE *f = fopen("reVC_debug.log", "a");
	if (f == nil)
		return;

	CVector pos = ped->GetPosition();
	if (startReason) {
		fprintf(f, "\n=== %s start, time %u ms, weapon %d ===\n", startReason, CTimer::GetTimeInMilliseconds(),
			(int)ped->GetWeapon()->m_eWeaponType);
		lastPos = pos;
	}

	float moved2d = (pos - lastPos).Magnitude2D();
	lastPos = pos;

	fprintf(f, "t %u ts %.3f path %d freecam %d mouse3rd %d state %d move %d spd %.3f stick %.0f,%.0f "
		"posdelta %.4f animdelta %.4f,%.4f moved %.4f,%.4f rot %.3f dest %.3f camori %.3f look %.3f "
		"ik %d ua %.3f,%.3f la %.3f torso %.3f,%.3f uareq %.3f,%.3f clav %.3f uastat %d aimpitch %.3f "
		"aiming %d pointing %d looking %d | ",
		CTimer::GetTimeInMilliseconds(), CTimer::GetTimeStep(), gControlPath, (int)CCamera::bFreeCam,
		(int)TheCamera.Cams[0].Using3rdPersonMouseCam(), (int)ped->m_nPedState, (int)ped->m_nMoveState, ped->m_fMoveSpeed,
		sx, sy, moved2d, ped->m_vecAnimMoveDelta.x, ped->m_vecAnimMoveDelta.y, ped->m_moved.x, ped->m_moved.y,
		ped->m_fRotationCur, ped->m_fRotationDest, TheCamera.Orientation, ped->m_fLookDirection,
		ped->m_pedIK.m_flags, ped->m_pedIK.m_upperArmOrient.yaw, ped->m_pedIK.m_upperArmOrient.pitch,
		ped->m_pedIK.m_lowerArmOrient.yaw, ped->m_pedIK.m_torsoOrient.yaw, ped->m_pedIK.m_torsoOrient.pitch,
		gIKDebugUaYaw, gIKDebugUaPitch, gIKDebugClavYaw, gIKDebugUaStatus, ped->m_fFPSMoveHeading,
		(int)ped->bIsAimingGun, (int)ped->bIsPointingGunAt, (int)ped->bIsLooking);

	for (CAnimBlendAssociation *a = RpAnimBlendClumpGetFirstAssociation(ped->GetClump()); a; a = RpAnimBlendGetNextAssociation(a))
		fprintf(f, "a%d:b%.2f/d%.1f/t%.3f/s%.2f/f%x ", (int)a->animId, a->blendAmount, a->blendDelta, a->currentTime, a->speed, (int)a->flags);
	fprintf(f, "\n");
	fclose(f);

	if (moveFrames > 0)
		moveFrames--;
	if (aimFrames > 0)
		aimFrames--;
}

void
CPlayerPed::ProcessControl(void)
{
	// Mobile has some debug/abandoned cheat thing in here: "gbFrankenTommy"

	if (m_nEvadeAmount != 0)
		--m_nEvadeAmount;

	if (m_nEvadeAmount == 0)
		m_pEvadingFrom = nil;

	if (m_pWanted->GetWantedLevel() > 0)
		FindNewAttackPoints();
	
	UpdateMeleeAttackers();

	if (m_pCurrentPhysSurface && m_pCurrentPhysSurface->IsVehicle() && ((CVehicle*)m_pCurrentPhysSurface)->IsBoat()) {
		bTryingToReachDryLand = true;

	} else if (!(((uint8)CTimer::GetFrameCounter() + m_randomSeed) & 0xF)) {
		CVehicle *nearVeh = (CVehicle*)CWorld::TestSphereAgainstWorld(GetPosition(), 7.0f, nil, false, true, false, false, false, false);
		if (nearVeh && nearVeh->IsBoat())
			bTryingToReachDryLand = true;
		else
			bTryingToReachDryLand = false;
	}

	if (m_nFadeDrunkenness) {
		if (m_nDrunkenness - 1 > 0) {
			--m_nDrunkenness;
		} else {
			m_nDrunkenness = 0;
			CMBlur::ClearDrunkBlur();
			m_nFadeDrunkenness = 0;
		}
	}
	if (m_nDrunkenness != 0) {
		CMBlur::SetDrunkBlur(m_nDrunkenness / 255.f);
	}
	CPed::ProcessControl();
	SetNearbyPedsToInteractWithPlayer();
	if (bWasPostponed)
		return;

	CPad *padUsed = GetPadFromPlayer(this);
	// set again by the on foot controls this frame; anything else that moves the player must not
	// see last frame's
	bPadAsksToRun = false;
	m_pWanted->Update();
	PruneReferences();

	if (padUsed) {
		ProcessManualReload(padUsed);
		ProcessAimReadyPose(padUsed);
	}
	ProcessAimAssist();

	if (CPad::m_bStickDebug) {
		sprintf(gPlayerDebugLine, "state %d move %d spd %.2f stick %d start %d aim %d posed %d point %d pitch %.2f",
			(int)m_nPedState, (int)m_nMoveState, m_fMoveSpeed, padUsed ? (int)padUsed->GetPedWalkUpDown() : 0,
			RpAnimBlendClumpGetAssociation(GetClump(), ANIM_STD_STARTWALK) != nil, (int)bIsAimingGun, (int)bIsAimPosed,
			(int)bIsPointingGunAt, m_fFPSMoveHeading);
		WritePlayerDebugLog(this, padUsed);
	}

	if (GetWeapon()->m_eWeaponType == WEAPONTYPE_MINIGUN) {
		CWeaponInfo *weaponInfo = CWeaponInfo::GetWeaponInfo(GetWeapon()->m_eWeaponType);
		CAnimBlendAssociation *fireAnim = RpAnimBlendClumpGetAssociation(GetClump(), GetPrimaryFireAnim(weaponInfo));
		if (fireAnim && fireAnim->currentTime - fireAnim->timeStep < weaponInfo->m_fAnimLoopEnd && m_nPedState == PED_ATTACK) {
			if (m_fGunSpinSpeed < 0.45f) {
				m_fGunSpinSpeed = Min(0.45f, m_fGunSpinSpeed + CTimer::GetTimeStep() * 0.013f);
			}

			if (padUsed->GetWeapon() && GetWeapon()->m_nAmmoTotal > 0 && fireAnim->currentTime >= weaponInfo->m_fAnimLoopStart) {
				DMAudio.PlayOneShot(m_audioEntityId, SOUND_WEAPON_MINIGUN_ATTACK, 0.0f);
			} else {
				DMAudio.PlayOneShot(m_audioEntityId, SOUND_WEAPON_MINIGUN_2, m_fGunSpinSpeed * (20.f / 9));
			}
		} else {
			if (m_fGunSpinSpeed > 0.0f) {
				if (m_fGunSpinSpeed >= 0.45f) {
					DMAudio.PlayOneShot(m_audioEntityId, SOUND_WEAPON_MINIGUN_3,  0.0f);
				}
				m_fGunSpinSpeed = Max(0.0f, m_fGunSpinSpeed - CTimer::GetTimeStep() * 0.003f);
			}
		}
	}
	if (GetWeapon()->m_eWeaponType == WEAPONTYPE_CHAINSAW && m_nPedState != PED_ATTACK && !bInVehicle) {
		DMAudio.PlayOneShot(m_audioEntityId, SOUND_WEAPON_CHAINSAW_IDLE, 0.0f);
	}

	if (m_nMoveState != PEDMOVE_RUN && m_nMoveState != PEDMOVE_SPRINT)
		RestoreSprintEnergy(1.0f);
	else if (m_nMoveState == PEDMOVE_RUN)
		RestoreSprintEnergy(0.3f);

	if (m_nPedState == PED_DEAD) {
		ClearWeaponTarget();
		return;
	}
	if (m_nPedState == PED_DIE) {
		ClearWeaponTarget();
		if (CTimer::GetTimeInMilliseconds() > m_bloodyFootprintCountOrDeathTime + 4000)
			SetDead();
		return;
	}
	if (m_nPedState == PED_DRIVING && m_objective != OBJECTIVE_LEAVE_CAR) {
		if (!CReplay::IsPlayingBack() || m_pMyVehicle) {
			if (m_pMyVehicle->IsCar() && ((CAutomobile*)m_pMyVehicle)->Damage.GetDoorStatus(DOOR_FRONT_LEFT) == DOOR_STATUS_SWINGING) {
				CAnimBlendAssociation *rollDoorAssoc = RpAnimBlendClumpGetAssociation(GetClump(), ANIM_STD_CAR_CLOSE_DOOR_ROLLING_LHS);

				if (m_pMyVehicle->m_nGettingOutFlags & CAR_DOOR_FLAG_LF || rollDoorAssoc || (rollDoorAssoc = RpAnimBlendClumpGetAssociation(GetClump(), ANIM_STD_CAR_CLOSE_DOOR_ROLLING_LO_LHS))) {
					if (rollDoorAssoc)
						m_pMyVehicle->ProcessOpenDoor(CAR_DOOR_LF, ANIM_STD_CAR_CLOSE_DOOR_ROLLING_LHS, rollDoorAssoc->currentTime);

				} else {
					// These comparisons are wrong, they return uint16
					if (padUsed && (padUsed->GetAccelerate() != 0.0f || padUsed->GetSteeringLeftRight() != 0.0f || padUsed->GetBrake() != 0.0f)) {
						if (rollDoorAssoc)
							m_pMyVehicle->ProcessOpenDoor(CAR_DOOR_LF, ANIM_STD_CAR_CLOSE_DOOR_ROLLING_LHS, rollDoorAssoc->currentTime);

					} else {
						m_pMyVehicle->m_nGettingOutFlags |= CAR_DOOR_FLAG_LF;
						if (m_pMyVehicle->bLowVehicle)
							rollDoorAssoc = CAnimManager::AddAnimation(GetClump(), ASSOCGRP_STD, ANIM_STD_CAR_CLOSE_DOOR_ROLLING_LO_LHS);
						else
							rollDoorAssoc = CAnimManager::AddAnimation(GetClump(), ASSOCGRP_STD, ANIM_STD_CAR_CLOSE_DOOR_ROLLING_LHS);

						rollDoorAssoc->SetFinishCallback(PedAnimDoorCloseRollingCB, this);
					}
				}
			}
		}
		return;
	}
	if (m_objective == OBJECTIVE_NONE)
		m_nMoveState = PEDMOVE_STILL;
	if (bIsLanding)
		RunningLand(padUsed);

	if (padUsed && padUsed->WeaponJustDown() && !TheCamera.Using1stPersonWeaponMode()) {
		// ...Really?
		eWeaponType playerWeapon = FindPlayerPed()->GetWeapon()->m_eWeaponType;
		if (playerWeapon == WEAPONTYPE_SNIPERRIFLE || playerWeapon == WEAPONTYPE_LASERSCOPE) {
			DMAudio.PlayFrontEndSound(SOUND_WEAPON_SNIPER_SHOT_NO_ZOOM, 0);
		} else if (playerWeapon == WEAPONTYPE_ROCKETLAUNCHER) {
			DMAudio.PlayFrontEndSound(SOUND_WEAPON_ROCKET_SHOT_NO_ZOOM, 0);
		}
	}

	ProcessUpperBodyWeaponAnims();

	switch (m_nPedState) {
		case PED_NONE:
		case PED_IDLE:
		case PED_FLEE_POS:
		case PED_FLEE_ENTITY:
		case PED_ATTACK:
		case PED_FIGHT:
		case PED_AIM_GUN:
		case PED_ANSWER_MOBILE:
			if (!RpAnimBlendClumpGetFirstAssociation(GetClump(), ASSOC_BLOCK) && !m_attachedTo) {
				if (TheCamera.Using1stPersonWeaponMode()) {
					if (padUsed)
						PlayerControlSniper(padUsed);

				} else if (TheCamera.Cams[0].Using3rdPersonMouseCam()
#ifdef FREE_CAM
					&& (!CCamera::bFreeCam || MovesWhileAiming())
#endif
					) {
					if (padUsed)
						PlayerControl1stPersonRunAround(padUsed);

				} else if (m_nPedState == PED_FIGHT) {
					if (padUsed)
						PlayerControlFighter(padUsed);

				} else if (padUsed) {
					PlayerControlZelda(padUsed);
				}
			}
			if (IsPedInControl() && m_nPedState != PED_ANSWER_MOBILE && padUsed)
				ProcessPlayerWeapon(padUsed);
			break;
		case PED_SEEK_ENTITY:
			m_vecSeekPos = m_pSeekTarget->GetPosition();

			// fall through
		case PED_SEEK_POS:
			switch (m_nMoveState) {
				case PEDMOVE_WALK:
					m_fMoveSpeed = 1.0f;
					break;
				case PEDMOVE_RUN:
					m_fMoveSpeed = 1.8f;
					break;
				case PEDMOVE_SPRINT:
					m_fMoveSpeed = 2.5f;
					break;
				default:
					m_fMoveSpeed = 0.0f;
					break;
			}
			SetRealMoveAnim();
			if (Seek()) {
				RestorePreviousState();
				SetMoveState(PEDMOVE_STILL);
			}
			break;
		case PED_SNIPER_MODE:
			if (GetWeapon()->m_eWeaponType == WEAPONTYPE_SNIPERRIFLE || GetWeapon()->m_eWeaponType == WEAPONTYPE_LASERSCOPE) {
				if (padUsed)
					PlayerControlSniper(padUsed);

			} else if (padUsed) {
				PlayerControlM16(padUsed);
			}
			break;
		case PED_SEEK_CAR:
		case PED_SEEK_IN_BOAT:
			if (bVehEnterDoorIsBlocked || bKindaStayInSamePlace) {
				m_fMoveSpeed = 0.0f;
			} else {
				m_fMoveSpeed = Min(2.0f, 2.0f * (m_vecSeekPos - GetPosition()).Magnitude2D());
			}
			if (padUsed && !padUsed->ArePlayerControlsDisabled()) {
				if (padUsed->GetTarget() || padUsed->GetLeftStickXJustDown() || padUsed->GetLeftStickYJustDown() ||
					padUsed->GetDPadUpJustDown() || padUsed->GetDPadDownJustDown() || padUsed->GetDPadLeftJustDown() ||
					padUsed->GetDPadRightJustDown()) {

					RestorePreviousState();
					if (m_objective == OBJECTIVE_ENTER_CAR_AS_PASSENGER || m_objective == OBJECTIVE_ENTER_CAR_AS_DRIVER) {
						RestorePreviousObjective();
					}
				}
			}
			if (padUsed && padUsed->GetSprint())
				m_nMoveState = PEDMOVE_SPRINT;
			SetRealMoveAnim();
			break;
		case PED_JUMP:
			if (padUsed)
				PlayerControlZelda(padUsed);
			if (bIsLanding)
				break;

			// This has been added later it seems
			return;
		case PED_FALL:
		case PED_GETUP:
		case PED_ENTER_TRAIN:
		case PED_EXIT_TRAIN:
		case PED_CARJACK:
		case PED_DRAG_FROM_CAR:
		case PED_ENTER_CAR:
		case PED_STEAL_CAR:
		case PED_EXIT_CAR:
			ClearWeaponTarget();
			break;
		case PED_ARRESTED:
			if (m_nLastPedState == PED_DRAG_FROM_CAR && m_pVehicleAnim)
				BeingDraggedFromCar();
			break;
		default:
			break;
	}
	if (padUsed && IsPedShootable() && m_nPedState != PED_ANSWER_MOBILE && m_nLastPedState != PED_ANSWER_MOBILE) {
		ProcessWeaponSwitch(padUsed);
		GetWeapon()->Update(m_audioEntityId, this);
	}
	ProcessAnimGroups();
	if (padUsed) {
		if (TheCamera.Cams[TheCamera.ActiveCam].Mode == CCam::MODE_FOLLOWPED
			&& TheCamera.Cams[TheCamera.ActiveCam].DirectionWasLooking == LOOKING_BEHIND) {

			m_lookTimer = 0;
			float camAngle = CGeneral::LimitRadianAngle(TheCamera.Cams[TheCamera.ActiveCam].Front.Heading());
			float angleBetweenPlayerAndCam = Abs(camAngle - m_fRotationCur);

			if (m_nPedState != PED_ATTACK && angleBetweenPlayerAndCam > DEGTORAD(30.0f) && angleBetweenPlayerAndCam < DEGTORAD(330.0f)) {
				if (angleBetweenPlayerAndCam > DEGTORAD(150.0f) && angleBetweenPlayerAndCam < DEGTORAD(210.0f)) {
					float rightTurnAngle = CGeneral::LimitRadianAngle(m_fRotationCur - DEGTORAD(150.0f));
					float leftTurnAngle = CGeneral::LimitRadianAngle(DEGTORAD(150.0f) + m_fRotationCur);

					if (m_fLookDirection == 999999.0f || bIsDucking)
						camAngle = rightTurnAngle;
					else if (Abs(rightTurnAngle - m_fLookDirection) < Abs(leftTurnAngle - m_fLookDirection))
						camAngle = rightTurnAngle;
					else
						camAngle = leftTurnAngle;
				}
				SetLookFlag(camAngle, true);
				SetLookTimer(CTimer::GetTimeStepInMilliseconds() * 5.0f);
			} else {
				ClearLookFlag();
			}
		}
	}
	if (m_nMoveState == PEDMOVE_SPRINT && bIsLooking) {
		ClearLookFlag();
		SetLookTimer(250);
	}

	if (m_vecMoveSpeed.Magnitude2D() < 0.1f) {
		if (m_nSpeedTimer) {
			if (CTimer::GetTimeInMilliseconds() > m_nSpeedTimer)
				m_bSpeedTimerFlag = true;
		} else {
			m_nSpeedTimer = CTimer::GetTimeInMilliseconds() + 500;
		}
	} else {
		m_nSpeedTimer = 0;
		m_bSpeedTimerFlag = false;
	}

	if (bDontAllowWeaponChange && FindPlayerPed() == this) {
		if (!CPad::GetPad(0)->GetTarget())
			bDontAllowWeaponChange = false;
	}

	if (m_nPedState != PED_SNIPER_MODE && (GetWeapon()->m_eWeaponState == WEAPONSTATE_FIRING || m_nPedState == PED_ATTACK))
		m_nPadDownPressedInMilliseconds = CTimer::GetTimeInMilliseconds();

	if (!bIsVisible)
		UpdateRpHAnim();
}

bool
CPlayerPed::DoesPlayerWantNewWeapon(eWeaponType weapon, bool onlyIfSlotIsEmpty)
{
	// GetPadFromPlayer(); // unused
	uint32 slot = CWeaponInfo::GetWeaponInfo(weapon)->m_nWeaponSlot;

	if (!HasWeaponSlot(slot) || GetWeapon(slot).m_eWeaponType == weapon)
		return true;

	if (onlyIfSlotIsEmpty)
		return false;

	// Check if he's using that slot right now.
	return m_nPedState != PED_ATTACK && m_nPedState != PED_AIM_GUN || slot != m_currentWeapon;
}

void
CPlayerPed::PlayIdleAnimations(CPad *padUsed)
{
	CAnimBlendAssociation* assoc;

	if (TheCamera.m_WideScreenOn || bIsDucking)
		return;

	struct animAndGroup {
		AnimationId animId;
		AssocGroupId groupId;
	};

	const animAndGroup idleAnims[] = {
		{ANIM_PLAYER_IDLE1, ASSOCGRP_PLAYER_IDLE},
		{ANIM_PLAYER_IDLE2, ASSOCGRP_PLAYER_IDLE},
		{ANIM_PLAYER_IDLE3, ASSOCGRP_PLAYER_IDLE},
		{ANIM_PLAYER_IDLE4, ASSOCGRP_PLAYER_IDLE},
		{ANIM_STD_XPRESS_SCRATCH, ASSOCGRP_STD},
	};

	static int32 lastTime = 0;
	static int32 lastAnim = -1;

	bool hasIdleAnim = false;
	CAnimBlock *idleAnimBlock = CAnimManager::GetAnimationBlock(idleAnimBlockIndex);
	uint32 sinceLastInput = padUsed->InputHowLongAgo();
	if (sinceLastInput <= 30000) {
		if (idleAnimBlock->isLoaded) {
			for (assoc = RpAnimBlendClumpGetFirstAssociation(GetClump()); assoc; assoc = RpAnimBlendGetNextAssociation(assoc)) {
				if (assoc->flags & ASSOC_IDLE) {
					hasIdleAnim = true;
					assoc->blendDelta = -8.0f;
				}
			}
			if (!hasIdleAnim)
				CStreaming::RemoveAnim(idleAnimBlockIndex);
		} else {
			lastTime = 0;
		}
	} else {
		CStreaming::RequestAnim(idleAnimBlockIndex, STREAMFLAGS_DONT_REMOVE);
		if (idleAnimBlock->isLoaded) {
			for(CAnimBlendAssociation *assoc = RpAnimBlendClumpGetFirstAssociation(GetClump()); assoc; assoc = RpAnimBlendGetNextAssociation(assoc)) {
				int firstIdle = idleAnimBlock->firstIndex;
				int index = assoc->hierarchy - CAnimManager::GetAnimation(0);
				if (index >= firstIdle && index < firstIdle + idleAnimBlock->numAnims) {
					hasIdleAnim = true;
					break;
				}
			}

			if (!hasIdleAnim && !bIsLooking && !bIsRestoringLook && sinceLastInput - lastTime > 25000) {
				int anim;
				do
					anim = CGeneral::GetRandomNumberInRange(0, ARRAY_SIZE(idleAnims));
				while (lastAnim == anim);

				assoc = CAnimManager::BlendAnimation(GetClump(), idleAnims[anim].groupId, idleAnims[anim].animId, 8.0f);
				assoc->flags |= ASSOC_IDLE;
				lastAnim = anim;
				lastTime = sinceLastInput;
			}
		}
	}
}

void
CPlayerPed::SetNearbyPedsToInteractWithPlayer(void)
{
	if (CGame::noProstitutes)
		return;

	for (int i = 0; i < m_numNearPeds; ++i) {
		CPed *nearPed = m_nearPeds[i];
		if (nearPed && nearPed->m_objectiveTimer < CTimer::GetTimeInMilliseconds() && !CTheScripts::IsPlayerOnAMission()) {
			int mi = nearPed->GetModelIndex();
			if (CPopulation::CanSolicitPlayerOnFoot(mi)) {
				CVector distToMe = nearPed->GetPosition() - GetPosition();
				CVector dirToMe = GetPosition() - nearPed->GetPosition();
				dirToMe.Normalise();
				if (DotProduct(dirToMe, nearPed->GetForward()) > 0.707 && DotProduct(GetForward(), nearPed->GetForward()) < -0.707 // those are double
					&& distToMe.MagnitudeSqr() < 9.0f && nearPed->m_objective == OBJECTIVE_NONE) {
					nearPed->SetObjective(OBJECTIVE_SOLICIT_FOOT, this);
					nearPed->m_objectiveTimer = CTimer::GetTimeInMilliseconds() + 10000;
					nearPed->Say(SOUND_PED_SOLICIT);
				}
			} else if (CPopulation::CanSolicitPlayerInCar(mi)) {
				if (InVehicle() && m_pMyVehicle->IsVehicleNormal()) {
					if (m_pMyVehicle->IsCar()) {
						CVector distToVeh = nearPed->GetPosition() - m_pMyVehicle->GetPosition();
						if (distToVeh.MagnitudeSqr() < 25.0f && m_pMyVehicle->IsRoomForPedToLeaveCar(CAR_DOOR_LF, nil) && nearPed->m_objective == OBJECTIVE_NONE) {
							nearPed->SetObjective(OBJECTIVE_SOLICIT_VEHICLE, m_pMyVehicle);
						}
					}
				}
			}
		}
	}
}

void
CPlayerPed::UpdateMeleeAttackers(void)
{
	CVector attackCoord;
	if (((CTimer::GetFrameCounter() + m_randomSeed + 7) & 3) == 0) {
		GetMeleeAttackCoords(attackCoord, m_nAttackDirToCheck, 2.0f);

		// Check if there is any vehicle/building inbetween us and m_nAttackDirToCheck. Peds will be able to attack us from those available directions.
		if (CWorld::GetIsLineOfSightClear(GetPosition(), attackCoord, true, true, false, true, false, false, false)
			&& !CWorld::TestSphereAgainstWorld(attackCoord, 0.4f, m_pMeleeList[m_nAttackDirToCheck], true, true, false, true, false, false)) {
			if (m_pMeleeList[m_nAttackDirToCheck] == this)
				m_pMeleeList[m_nAttackDirToCheck] = nil; // mark it as available
		} else {
			m_pMeleeList[m_nAttackDirToCheck] = this; // slot not available. useful for m_bNoPosForMeleeAttack
		}
		if (++m_nAttackDirToCheck >= ARRAY_SIZE(m_pMeleeList))
			m_nAttackDirToCheck = 0;
	}
	// 6 directions
	for (int i = 0; i < ARRAY_SIZE(m_pMeleeList); ++i) {
		CPed *victim = m_pMeleeList[i];
		if (victim && victim != this) {
			if (victim->m_nPedState != PED_DEAD && victim->m_pedInObjective == this)  {
				if (victim->m_objective == OBJECTIVE_KILL_CHAR_ON_FOOT || victim->m_objective == OBJECTIVE_KILL_CHAR_ANY_MEANS || victim->m_objective == OBJECTIVE_KILL_CHAR_ON_BOAT) {
					GetMeleeAttackCoords(attackCoord, i, 2.0f);
					if ((attackCoord - GetPosition()).MagnitudeSqr() > 12.25f)
						m_pMeleeList[i] = nil;
				} else {
					m_pMeleeList[i] = nil;
				}
			} else {
				m_pMeleeList[i] = nil;
			}
		}
	}
	m_bNoPosForMeleeAttack = m_pMeleeList[0] == this && m_pMeleeList[1] == this && m_pMeleeList[2] == this
#ifdef FIX_BUGS
		&& m_pMeleeList[3] == this
#endif
		&& m_pMeleeList[4] == this && m_pMeleeList[5] == this;
}

void
CPlayerPed::RemovePedFromMeleeList(CPed *ped)
{
	for (uint16 i = 0; i < ARRAY_SIZE(m_pMeleeList); i++) {
		if (m_pMeleeList[i] == ped) {
			m_pMeleeList[i] = nil;
			ped->m_attackTimer = 0;
			return;
		}
	}
}

void
CPlayerPed::GetMeleeAttackCoords(CVector& coords, int8 dir, float dist)
{
	coords = GetPosition();
	switch (dir) {
		case 0:
			coords.y += dist;
			break;
		case 1:
			coords.x += Sqrt(3.f / 4.f) * dist;
			coords.y += 0.5f * dist;
			break;
		case 2:
			coords.x += Sqrt(3.f / 4.f) * dist;
			coords.y -= 0.5f * dist;
			break;
		case 3:
			coords.y -= dist;
			break;
		case 4:
			coords.x -= Sqrt(3.f / 4.f) * dist;
			coords.y -= 0.5f * dist;
			break;
		case 5:
			coords.x -= Sqrt(3.f / 4.f) * dist;
			coords.y += 0.5f * dist;
			break;
		default:
			break;
	}
}

int32
CPlayerPed::FindMeleeAttackPoint(CPed *victim, CVector &dist, uint32 &endOfAttackOut)
{
	endOfAttackOut = 0;
	bool thereIsAnEmptySlot = false;
	int dirToAttack = -1;
	for (int i = 0; i < ARRAY_SIZE(m_pMeleeList); i++) {
		CPed* pedAtThisDir = m_pMeleeList[i];
		if (pedAtThisDir) {
			if (pedAtThisDir == victim) {
				dirToAttack = i;
			} else {
				if (pedAtThisDir->m_attackTimer > endOfAttackOut)
					endOfAttackOut = pedAtThisDir->m_attackTimer;
			}
		} else {
			thereIsAnEmptySlot = true;
		}
	}

	// We don't have victim ped in our melee list
	if (dirToAttack == -1 && thereIsAnEmptySlot) {
		float angle = Atan2(-dist.x, -dist.y);
		float adjustedAngle = angle + DEGTORAD(30.0f);
		if (adjustedAngle < 0.f)
			adjustedAngle += TWOPI;

		int wantedDir = Floor(adjustedAngle / DEGTORAD(60.0f));

		// And we have another ped at the direction of victim ped, so store victim to next empty direction to it's real direction. (Bollocks)
		if (m_pMeleeList[wantedDir]) {
			int closestDirToPreferred = -99;
			int preferredDir = wantedDir;

			for (int i = 0; i < ARRAY_SIZE(m_pMeleeList); i++) {
				if (!m_pMeleeList[i]) {
					if (Abs(i - preferredDir) < Abs(closestDirToPreferred - preferredDir))
						closestDirToPreferred = i;
				}
			}
			if (closestDirToPreferred > 0)
				dirToAttack = closestDirToPreferred;
		} else {

			// Luckily the direction of victim ped is already empty, good
			dirToAttack = wantedDir;
		}

		if (dirToAttack != -1) {
			m_pMeleeList[dirToAttack] = victim;
			victim->RegisterReference((CEntity**) &m_pMeleeList[dirToAttack]);
			if (endOfAttackOut > CTimer::GetTimeInMilliseconds())
				victim->m_attackTimer = endOfAttackOut + CGeneral::GetRandomNumberInRange(1000, 2000);
			else
				victim->m_attackTimer = CTimer::GetTimeInMilliseconds() + CGeneral::GetRandomNumberInRange(500, 1000);
		}
	}
	return dirToAttack;
}

#ifdef COMPATIBLE_SAVES
#define CopyFromBuf(buf, data) memcpy(&data, buf, sizeof(data)); SkipSaveBuf(buf, sizeof(data));
#define CopyToBuf(buf, data) memcpy(buf, &data, sizeof(data)); SkipSaveBuf(buf, sizeof(data));
void
CPlayerPed::Save(uint8*& buf)
{
	CPed::Save(buf);
	ZeroSaveBuf(buf, 16);
	CopyToBuf(buf, m_fMaxStamina);
	ZeroSaveBuf(buf, 28);
	CopyToBuf(buf, m_nTargettableObjects[0]);
	CopyToBuf(buf, m_nTargettableObjects[1]);
	CopyToBuf(buf, m_nTargettableObjects[2]);
	CopyToBuf(buf, m_nTargettableObjects[3]);
	ZeroSaveBuf(buf, 164);
}

void
CPlayerPed::Load(uint8*& buf)
{
	CPed::Load(buf);
	SkipSaveBuf(buf, 16);
	CopyFromBuf(buf, m_fMaxStamina);
	SkipSaveBuf(buf, 28);
	CopyFromBuf(buf, m_nTargettableObjects[0]);
	CopyFromBuf(buf, m_nTargettableObjects[1]);
	CopyFromBuf(buf, m_nTargettableObjects[2]);
	CopyFromBuf(buf, m_nTargettableObjects[3]);
	SkipSaveBuf(buf, 164);
}
#undef CopyFromBuf
#undef CopyToBuf
#endif

// The wind up before a shot is the firing animation having to run from nothing to the frame
// weapon.dat calls the fire point, most of a third of a second.  The game already knows how
// to avoid that and just never does it here: PointGunAt() winds the animation forward to its
// loop start and holds it, so the arm is up before the trigger is pulled and only a frame or
// two are left to play.  That only runs while locked on to somebody, and free aiming never
// locks on.  This puts the same pose on while Target/Aim is held.  Firing carries on from
// where the animation was left, because BlendAnimation keeps the time on one that is already
// there, so the first shot comes at once.

// The arm comes down on a fade, so the fade rate is how fast it drops.  The same setting that
// raises the arm scales it, so one slider governs both halves of the movement.
static void
FadeAimPoseOut(CPed *ped, CWeaponInfo *info, float speed)
{
	for (int32 i = 0; i < 2; i++) {
		AnimationId id = i == 0 ? CPed::GetPrimaryFireAnim(info) : CPed::GetCrouchFireAnim(info);
		if (id == (AnimationId)0)
			continue;
		CAnimBlendAssociation *assoc = RpAnimBlendClumpGetAssociation(ped->GetClump(), id);
		if (assoc == nil)
			continue;

		// Stop it running as well as fading it.  Coming out of a shot the animation is past
		// its loop and on its way through a tail that lowers the arm on its own, and letting
		// that play is the whole delay.
		assoc->flags &= ~ASSOC_RUNNING;
		assoc->flags |= ASSOC_DELETEFADEDOUT;
		assoc->blendDelta = -4.0f * speed;
	}
}

void
CPlayerPed::ProcessAimReadyPose(CPad *padUsed)
{
	CWeaponInfo *info = CWeaponInfo::GetWeaponInfo(GetWeapon()->m_eWeaponType);
	AnimationId fireAnim = GetPrimaryFireAnim(info);
	CAnimBlendAssociation *assoc = RpAnimBlendClumpGetAssociation(GetClump(), fireAnim);

	// The raise runs the animation fast, and the shot plays on that very association: the
	// attack fires each time its time crosses the weapon's fire frame and only ever corrects
	// a speed that has fallen below one.  So a shot let off before the arm was up came at the
	// raise speed and kept coming at it.  Hand the speed back the moment the trigger is touched.
	if (assoc != nil &&
		(padUsed->GetWeapon() || m_nPedState == PED_ATTACK || m_nPedState == PED_AIM_GUN ||
		 GetWeapon()->m_eWeaponState == WEAPONSTATE_RELOADING))
		assoc->speed = 1.0f;

	// A trigger pulled halfway through the raise had the rest of it to walk at normal speed
	// before the first shot, which made firing early slower than waiting for the arm.  Put the
	// animation where the pose holds it, so an early press gets the same instant shot.
	if (assoc != nil && bIsAimPosed && padUsed->GetWeapon() &&
		m_nPedState != PED_ATTACK && assoc->currentTime < info->m_fAnimLoopStart)
		assoc->SetCurrentTime(info->m_fAnimLoopStart);

	// Whether the player is asking to aim, and nothing about what the weapon is doing: it
	// stays in its firing state for as long as its rate of fire says, and treating that as
	// not aiming took the pose off and dropped the arm between shots.  Ducking has crouch
	// animations of its own and the scopes raise the weapon themselves.
	bool aiming =
		padUsed->GetTarget() &&
		!bInVehicle &&
		!bIsDucking &&
		info->IsFlagSet(WEAPONFLAG_CANAIM) &&
		info->m_eWeaponFire != WEAPON_FIRE_MELEE &&
		info->m_AnimToPlay != ASSOCGRP_STD &&
		!TheCamera.Using1stPersonWeaponMode();

	if (!aiming) {
		if (bIsAimPosed) {
			// Aim let go in the middle of a shot.  Hold the pose until the trigger is let go as
			// well and the shot that is owed has been taken, then drop it exactly the way letting
			// go in the other order does, so both orders end the same way.
			if (m_nPedState == PED_ATTACK &&
				(padUsed->GetWeapon() || CTimer::GetTimeInMilliseconds() < m_shootTimer))
				return;

			bIsAimPosed = false;

			// The end of a burst leaves the player pointing the gun at nothing, which is what
			// keeps the weapon up between shots, and PointGunAt() puts the pose back on every
			// frame while that lasts.  A real lock on target is left alone.
			if (bIsPointingGunAt && m_pPointGunAt == nil)
				ClearPointGunAt();

			// Cutting the animation short leaves nothing to raise the finish callback that ends
			// the attack, so the player stood in PED_ATTACK long after the arm was down and could
			// not walk away from it.  End it here.
			if (m_nPedState == PED_ATTACK) {
				bIsAttacking = false;
				ClearAttack();
			}

			FadeAimPoseOut(this, info, Max(m_fAimRaiseSpeed, 0.1f));
			// and stop telling the arm where to aim, or it stays up on its own; a lock on target
			// aims the arm itself and is left alone
			if (m_pPointGunAt == nil)
				ClearAimFlag();
		}
		return;
	}

	// While a shot or a reload is actually playing, leave the animation to it.  Nothing is
	// torn down, so the pose is simply picked up again when it is over.
	if (m_nPedState == PED_ATTACK || m_nPedState == PED_AIM_GUN) {
		// The arm is aimed by IK towards the direction and pitch it was last told, and those
		// were only set as a shot or the pointing began, so it stayed at that pitch however the
		// camera moved after.  Told again every frame while nobody is locked on.
		if (m_pPointGunAt == nil) {
			SetAimFlag(CGeneral::LimitRadianAngle(-TheCamera.Orientation));
			m_fFPSMoveHeading = TheCamera.Find3rdPersonQuickAimPitch();
		}
		return;
	}
	if (GetWeapon()->m_eWeaponState == WEAPONSTATE_RELOADING)
		return;

	if (assoc == nil) {
		assoc = CAnimManager::BlendAnimation(GetClump(), info->m_AnimToPlay, fireAnim, 8.0f);
		if (assoc == nil)
			return;

		assoc->SetCurrentTime(0.0f);
		assoc->SetRun();
		assoc->speed = Max(m_fAimRaiseSpeed, 0.1f);
		bIsAimPosed = true;
		return;
	}

	// The shot ends with ClearAttack() fading the animation out, and letting that finish would
	// drop the arm and lift it again on the next frame from a fresh animation.  Aim is still
	// held, so it is caught on the way out and blended straight back in.
	if (assoc->blendDelta < 0.0f) {
		assoc->flags &= ~ASSOC_DELETEFADEDOUT;
		assoc->blendDelta = (1.0f - assoc->blendAmount) * 8.0f;
	}

	bIsAimPosed = true;

	// The direction is the camera's rather than the body's, as in Vice City's own free camera:
	// the body turns towards the camera over a few frames and the arm pointed where it had got
	// to, a little off the crosshair.
	// Vice City only brings the arm part of the way up with the animation - the handguns sit at
	// half height on the ready frame - and lifts it the rest of the way with IK, towards the
	// direction it is told to aim in.  Without the free camera it only ever tells it that once
	// an attack starts, so the held pose stopped at half height.  These are the two lines the
	// game itself uses for the mouse camera when an attack starts, every frame the pose is held
	// so the pitch follows the camera.
	SetAimFlag(CGeneral::LimitRadianAngle(-TheCamera.Orientation));
	m_fFPSMoveHeading = TheCamera.Find3rdPersonQuickAimPitch();

	// The handguns are raised the rest of the way only while the ped is pointing the gun: that
	// state is where PointGunAt holds the animation and the IK lifts the arm.  The free camera
	// gets there for them by pointing the gun at nothing, which is what this does as well.
	if (info->IsFlagSet(WEAPONFLAG_CANAIM_WITHARM) && !bIsPointingGunAt) {
		SetLookFlag(CGeneral::LimitRadianAngle(-TheCamera.Orientation), true, true);
		SetLookTimer(INT32_MAX);
		SetPointGunAt(nil);
		return;
	}

	// held at the ready, exactly where PointGunAt would hold it
	if (assoc->currentTime >= info->m_fAnimLoopStart) {
		assoc->SetCurrentTime(info->m_fAnimLoopStart);
		assoc->flags &= ~ASSOC_RUNNING;
		assoc->speed = 1.0f;

		if (info->IsFlagSet(WEAPONFLAG_CANAIM_WITHARM))
			m_pedIK.m_flags |= CPedIK::AIMS_WITH_ARM;
		else
			m_pedIK.m_flags &= ~CPedIK::AIMS_WITH_ARM;
	}
}

// how far off the sights someone still counts, measured on screen and so the same at any
// distance
#define AIM_ASSIST_CONE (DEGTORAD(7.0f))

// Rotational slow down.  Nothing about where the shot lands changes; the stick simply asks for
// less turn while the sights are near someone, which the player reads as his own hand
// steadying rather than as the game taking over.  Worked out once a frame off last frame's
// camera and handed to the stick through GetLookStickSensitivity, so it lands exactly where
// the aiming sensitivity does.
void
CPlayerPed::ProcessAimAssist(void)
{
	m_fAimAssistFactor = 1.0f;

	if (!bAimAssist || m_fAimAssistStrength <= 0.0f)
		return;
	// only for free aim; the game's own lock on already holds a target, and the two pulling at
	// the same stick would fight
	if (!CPad::GetPad(0)->GetTarget() || m_pPointGunAt != nil || bInVehicle)
		return;

	CWeaponInfo *info = CWeaponInfo::GetWeaponInfo(GetWeapon()->m_eWeaponType);
	if (!info->IsFlagSet(WEAPONFLAG_CANAIM) && !info->IsFlagSet(WEAPONFLAG_CANAIM_WITHARM))
		return;

	CCam &cam = TheCamera.Cams[TheCamera.ActiveCam];
	float tanX, tanY;
	TheCamera.Find3rdPersonCamAimTangents(cam.FOV, tanX, tanY);
	CVector aim = cam.Front + cam.Up * tanY + CrossProduct(cam.Front, cam.Up) * tanX;
	aim.Normalise();

	float bestCos = Cos(AIM_ASSIST_CONE);
	for (int32 i = CPools::GetPedPool()->GetSize() - 1; i >= 0; i--) {
		CPed *ped = CPools::GetPedPool()->GetSlot(i);
		if (ped == nil || ped == this)
			continue;
		if (ped->DyingOrDead() || ped->bInVehicle || ped->m_leader == this)
			continue;

		RwV3d node;
		ped->m_pedIK.GetComponentPosition(node, PED_MID);
		CVector centre(node.x, node.y, node.z);

		CVector toPed = centre - cam.Source;
		float dist = toPed.Magnitude();
		if (dist < 1.0f || dist > info->m_fRange)
			continue;

		// the widest cone found so far, so the nearest one to the sights wins
		float dot = DotProduct(toPed, aim) / dist;
		if (dot <= bestCos)
			continue;
		// peds do not block, or a crowd would shade itself out; glass and fences do not either,
		// the shot goes through them
		if (!CWorld::GetIsLineOfSightClear(cam.Source, centre, true, true, false, true, false, true))
			continue;

		bestCos = dot;
	}

	if (bestCos <= Cos(AIM_ASSIST_CONE))
		return;

	// full at the sights, nothing at the edge of the cone, so it eases in instead of switching on
	float closeness = 1.0f - Acos(Clamp(bestCos, -1.0f, 1.0f)) / AIM_ASSIST_CONE;
	m_fAimAssistFactor = 1.0f - Clamp(m_fAimAssistStrength, 0.0f, 1.0f) * closeness;
}

// The game only reloads once the clip runs dry, in CWeapon::Fire.  Topping it up early is the
// same two lines: put the weapon in its reloading state and set the timer, and
// CWeapon::Update() fills the clip when the timer runs out, exactly as it does for the
// automatic one.  The fast reload cheat is honoured the same way too.
void
CPlayerPed::ProcessManualReload(CPad *padUsed)
{
	if (!padUsed->GetReloadJustDown())
		return;

	CWeapon *weapon = GetWeapon();
	CWeaponInfo *info = CWeaponInfo::GetWeaponInfo(weapon->m_eWeaponType);

	// nothing with a clip to fill
	if (info->m_eWeaponFire == WEAPON_FIRE_MELEE || info->m_eWeaponFire == WEAPON_FIRE_PROJECTILE ||
		info->m_eWeaponFire == WEAPON_FIRE_CAMERA)
		return;
	if (info->m_nAmountofAmmunition <= 1)
		return;
	// Already reloading is the only state that says no.  A weapon reads as firing for as long as
	// its rate of fire lasts after a shot, and waiting that out dropped a reload asked for at the
	// end of a burst.
	if (weapon->m_eWeaponState == WEAPONSTATE_RELOADING)
		return;
	// clip already full, or nothing left to fill it with
	if (weapon->m_nAmmoInClip >= info->m_nAmountofAmmunition || weapon->m_nAmmoTotal <= weapon->m_nAmmoInClip)
		return;

	weapon->m_eWeaponState = WEAPONSTATE_RELOADING;
	weapon->m_nTimer = CTimer::GetTimeInMilliseconds() + info->m_nReload;
	bool fastReload = CWorld::Players[CWorld::PlayerInFocus].m_bFastReload;
	if (fastReload)
		weapon->m_nTimer = CTimer::GetTimeInMilliseconds() + info->m_nReload / 4;

	// Whatever the arm was doing gives way to the reload.  Pointing the gun at nothing is what
	// holds the weapon up between shots and PointGunAt() puts that pose back on every frame, so
	// the state goes first, and the firing animation is faded rather than left to blend against
	// the reload.
	if (bIsPointingGunAt && m_pPointGunAt == nil)
		ClearPointGunAt();
	bIsAimPosed = false;

	CAnimBlendAssociation *fireAssoc = RpAnimBlendClumpGetAssociation(GetClump(), GetPrimaryFireAnim(info));
	if (fireAssoc) {
		fireAssoc->flags |= ASSOC_DELETEFADEDOUT;
		fireAssoc->blendDelta = -8.0f;
	}

	// The animation is started from inside the attack, so a reload asked for outside one starts
	// it the same way CPed::Attack does, crouched when ducking, and not at all under the fast
	// reload cheat.
	AnimationId reloadAnim = bIsDucking && GetCrouchReloadAnim(info) ? GetCrouchReloadAnim(info) : GetReloadAnim(info);
	if (reloadAnim != (AnimationId)0) {
		if (!fastReload && !RpAnimBlendClumpGetAssociation(GetClump(), reloadAnim)) {
			CAnimBlendAssociation *reloadAssoc = CAnimManager::BlendAnimation(GetClump(), info->m_AnimToPlay, reloadAnim, 8.0f);
			if (reloadAssoc)
				reloadAssoc->SetFinishCallback(FinishedReloadCB, this);
		}
		ClearLookFlag();
		ClearAimFlag();
		bIsAttacking = false;
		bIsPointingGunAt = false;
		m_shootTimer = CTimer::GetTimeInMilliseconds();
	}
}
