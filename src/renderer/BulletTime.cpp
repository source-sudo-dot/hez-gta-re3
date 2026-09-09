#include "common.h"

#include "BulletTime.h"

#include "Camera.h"
#include "CutsceneMgr.h"
#include "DMAudio.h"
#include "Frontend.h"
#include "Pad.h"
#include "PlayerInfo.h"
#include "PlayerPed.h"
#include "Sprite2d.h"
#include "Timer.h"
#include "World.h"

bool  CBulletTime::bEnabled = true;
float CBulletTime::m_fDuration = 3.0f;
float CBulletTime::m_fRecharge = 3.0f;
float CBulletTime::m_fCharge = 1.0f;
bool  CBulletTime::m_bActive = false;

// far enough ahead that the pill's own timer never ends this, the meter does
#define BULLETTIME_PILL_MS (60 * 60 * 1000)

void
CBulletTime::Init(void)
{
	m_bActive = false;
	m_fCharge = 1.0f;
}

void
CBulletTime::Process(void)
{
	CPlayerPed *player = FindPlayerPed();

	// Logical frames are counted off real time, not the game clock, so a second here is a
	// second in the room whatever the time scale is doing.
	float dt = 1.0f / LOGICAL_FRAME_RATE;

	if(!bEnabled || player == nil || CTimer::GetIsPaused()){
		if(m_bActive){
			m_bActive = false;
			if(player != nil)
				player->ClearAdrenaline();
		}
		return;
	}

	if(m_bActive){
		// a pill picked up off the street, or anything else that stops the adrenaline,
		// takes the meter with it
		if(!player->m_bAdrenalineActive){
			m_bActive = false;
			m_fCharge = 0.0f;
			return;
		}

		m_fCharge -= dt / Max(m_fDuration, 0.1f);
		if(m_fCharge <= 0.0f){
			m_fCharge = 0.0f;
			m_bActive = false;
			player->ClearAdrenaline();
		}
		return;
	}

	if(m_fCharge < 1.0f){
		m_fCharge = Min(1.0f, m_fCharge + dt / Max(m_fRecharge, 0.1f));
		return;
	}

	// full, and nothing else is already slowing things down
	if(CPad::GetPad(0)->GetBulletTime() &&
	   !player->m_bAdrenalineActive &&
	   FindPlayerVehicle() == nil &&
	   !player->Dead() && player->m_nPedState != PED_DIE && player->m_nPedState != PED_ARRESTED &&
	   !FrontEndMenuManager.m_bMenuActive && !CCutsceneMgr::IsRunning()){
		player->m_bAdrenalineActive = true;
		player->m_nAdrenalineTime = CTimer::GetTimeInMilliseconds() + BULLETTIME_PILL_MS;
		DMAudio.PlayFrontEndSound(SOUND_PICKUP_ADRENALINE, 0);
		m_bActive = true;
	}
}

// A bar the width of the crosshair, a little under it.  Green while there is a full
// charge to spend, amber while it is running down, dim while it fills back up.
void
CBulletTime::DrawMeter(float x, float y)
{
	if(!bEnabled)
		return;

	float halfWidth = SCREEN_SCALE_X(26.0f);
	float height = SCREEN_SCALE_Y(3.0f);
	float top = y + SCREEN_SCALE_Y(22.0f);

	CSprite2d::DrawRect(CRect(x - halfWidth - SCREEN_SCALE_X(1.0f), top - SCREEN_SCALE_Y(1.0f),
		x + halfWidth + SCREEN_SCALE_X(1.0f), top + height + SCREEN_SCALE_Y(1.0f)),
		CRGBA(0, 0, 0, 120));

	if(m_fCharge <= 0.0f)
		return;

	CRGBA colour;
	if(m_bActive)
		colour = CRGBA(255, 170, 40, 220);
	else if(m_fCharge >= 1.0f)
		colour = CRGBA(90, 230, 90, 200);
	else
		colour = CRGBA(150, 150, 150, 140);

	CSprite2d::DrawRect(CRect(x - halfWidth, top,
		x - halfWidth + 2.0f * halfWidth * m_fCharge, top + height), colour);
}
