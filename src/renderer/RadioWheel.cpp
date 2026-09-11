#include "common.h"

#include "RadioWheel.h"

#include "Camera.h"
#include "CutsceneMgr.h"
#include "DMAudio.h"
#include "Draw.h"
#include "Font.h"
#include "Frontend.h"
#include "General.h"
#include "MusicManager.h"
#include "Pad.h"
#include "PlayerPed.h"
#include "Sprite2d.h"
#include "Text.h"
#include "Timer.h"
#include "Vehicle.h"
#include "World.h"
#include "audio_enums.h"
#include "sampman.h"

bool   CRadioWheel::bEnabled = true;
bool   CRadioWheel::bOpen = false;
int32  CRadioWheel::m_aSlots[16];
int32  CRadioWheel::m_nSlots = 0;
int32  CRadioWheel::m_nSelected = 0;
float  CRadioWheel::m_fOpenAmount = 0.0f;
float  CRadioWheel::m_fPointX = 0.0f;
float  CRadioWheel::m_fPointY = 0.0f;
int32  CRadioWheel::m_nLastStation = HEAD_RADIO;
uint32 CRadioWheel::m_nPressedAt = 0;
bool   CRadioWheel::m_bWaitingToOpen = false;

#define WHEEL_STICK_DEADZONE (0.35f)
#define WHEEL_MOUSE_RANGE (60.0f)
#define WHEEL_RADIUS (110.0f)
// The button is given this long before the ring opens.  Let go inside it and nothing
// opens at all - the radio simply goes quiet or comes back.
#define WHEEL_HOLD_MS (50)

void
CRadioWheel::Init(void)
{
	bOpen = false;
	m_nSlots = 0;
	m_nSelected = 0;
	m_fOpenAmount = 0.0f;
	m_fPointX = 0.0f;
	m_fPointY = 0.0f;
	m_nLastStation = HEAD_RADIO;
	m_bWaitingToOpen = false;
}

bool
CRadioWheel::CanOpen(void)
{
	CPlayerPed *player = FindPlayerPed();
	if(player == nil)
		return false;

	// behind the wheel of something, and not one of the ones carrying a police radio,
	// which cannot be tuned at all
	CVehicle *veh = FindPlayerVehicle();
	if(veh == nil || player->m_nPedState != PED_DRIVING)
		return false;
	if(MusicManager.UsesPoliceRadio(veh))
		return false;

	if(CPad::GetPad(0)->ArePlayerControlsDisabled())
		return false;
	if(FrontEndMenuManager.m_bMenuActive || CCutsceneMgr::IsRunning())
		return false;
	return true;
}

// Every station, with the player's own music only where there is some, and off at the
// top of the ring so there is always a way to quiet.
void
CRadioWheel::CollectSlots(void)
{
	m_nSlots = 0;
	m_aSlots[m_nSlots++] = RADIO_OFF;
	for(int32 i = HEAD_RADIO; i < USERTRACK; i++)
		m_aSlots[m_nSlots++] = i;
	if(SampleManager.IsMP3RadioChannelAvailable())
		m_aSlots[m_nSlots++] = USERTRACK;
}

wchar *
CRadioWheel::NameOf(int32 station)
{
	switch(station){
		case HEAD_RADIO: return TheText.Get("FEA_FM0");
		case DOUBLE_CLEF: return TheText.Get("FEA_FM1");
		case JAH_RADIO: return TheText.Get("FEA_FM2");
		case RISE_FM: return TheText.Get("FEA_FM3");
		case LIPS_106: return TheText.Get("FEA_FM4");
		case GAME_FM: return TheText.Get("FEA_FM5");
		case MSX_FM: return TheText.Get("FEA_FM6");
		case FLASHBACK: return TheText.Get("FEA_FM7");
		case CHATTERBOX: return TheText.Get("FEA_FM8");
		case USERTRACK: return TheText.Get("FEA_FM9");
		default: return TheText.Get("FEZ_ROF");
	}
}

void
CRadioWheel::Tune(int32 station)
{
	if(station != RADIO_OFF)
		m_nLastStation = station;
	MusicManager.TuneToStation((uint8)station);
}

// A tap rather than a hold: quiet, and the next tap back to what was playing.
void
CRadioWheel::ToggleOff(void)
{
	if(DMAudio.GetRadioInCar() == RADIO_OFF)
		Tune(m_nLastStation);
	else
		Tune(RADIO_OFF);
}

void
CRadioWheel::Open(void)
{
	CollectSlots();
	if(m_nSlots < 2)
		return;

	int32 current = DMAudio.GetRadioInCar();
	m_nSelected = 0;
	for(int32 i = 0; i < m_nSlots; i++)
		if(m_aSlots[i] == current)
			m_nSelected = i;

	m_fPointX = 0.0f;
	m_fPointY = 0.0f;
	bOpen = true;
	CTimer::SetCodePause(true);
}

void
CRadioWheel::Close(bool takeSelection)
{
	if(takeSelection && m_nSelected >= 0 && m_nSelected < m_nSlots){
		int32 station = m_aSlots[m_nSelected];
		if(station != DMAudio.GetRadioInCar())
			Tune(station);
	}

	bOpen = false;
	CTimer::SetCodePause(false);
}

void
CRadioWheel::Process(void)
{
	CPad *pad = CPad::GetPad(0);

	if(!bEnabled){
		if(bOpen)
			Close(false);
		return;
	}

	bool held = pad->GetRadioWheel();

	if(!bOpen){
		if(!held){
			// let go before the ring had its chance: a tap, and the ring stays shut
			if(m_bWaitingToOpen){
				m_bWaitingToOpen = false;
				ToggleOff();
			}
			return;
		}

		if(!CanOpen()){
			m_bWaitingToOpen = false;
			return;
		}

		if(!m_bWaitingToOpen){
			m_bWaitingToOpen = true;
			m_nPressedAt = CTimer::GetTimeInMillisecondsPauseMode();
			return;
		}

		if(CTimer::GetTimeInMillisecondsPauseMode() - m_nPressedAt >= WHEEL_HOLD_MS){
			m_bWaitingToOpen = false;
			Open();
		}
		return;
	}

	// something took the player out of the car while the ring was up
	if(!CanOpen()){
		Close(false);
		return;
	}

	if(!held){
		Close(true);
		return;
	}

	// Point with the right stick, or push the pick around with the mouse, the way the
	// weapon ring is pointed at.
	float stickX = pad->NewState.RightStickX / 128.0f;
	float stickY = -pad->NewState.RightStickY / 128.0f;
	if(Sqrt(SQR(stickX) + SQR(stickY)) > WHEEL_STICK_DEADZONE){
		m_fPointX = stickX;
		m_fPointY = stickY;
	}else{
		m_fPointX += pad->GetMouseX() / WHEEL_MOUSE_RANGE;
		m_fPointY -= pad->GetMouseY() / WHEEL_MOUSE_RANGE;
	}
	float len = Sqrt(SQR(m_fPointX) + SQR(m_fPointY));
	if(len > 1.0f){
		m_fPointX /= len;
		m_fPointY /= len;
	}

	// Slot 0 sits at the top and they go round clockwise, so the angle the player is
	// pointing at maps straight onto one of them.
	if(Sqrt(SQR(m_fPointX) + SQR(m_fPointY)) > WHEEL_STICK_DEADZONE){
		float angle = HALFPI - CGeneral::GetATanOfXY(m_fPointX, m_fPointY);
		while(angle < 0.0f) angle += TWOPI;
		while(angle >= TWOPI) angle -= TWOPI;
		m_nSelected = (int32)(angle / TWOPI * m_nSlots + 0.5f) % m_nSlots;
	}
}

void
CRadioWheel::Draw(void)
{
	// grow on the way in and shrink on the way out, per rendered frame, so it takes the
	// same time however fast the game draws
	float target = bOpen ? 1.0f : 0.0f;
	float blend = Clamp(0.4f * CTimer::GetRenderFrameLength(), 0.0f, 1.0f);
	m_fOpenAmount += (target - m_fOpenAmount) * blend;
	if(m_fOpenAmount < 0.004f){
		m_fOpenAmount = 0.0f;
		return;
	}
	if(m_nSlots < 2)
		return;

	float open = m_fOpenAmount;
	float centreX = SCREEN_WIDTH / 2.0f;
	float centreY = SCREEN_HEIGHT / 2.0f;
	// one scale on both axes, or the ring comes out an ellipse
	float radius = SCREEN_SCALE_Y(WHEEL_RADIUS) * (0.7f + 0.3f * open);

	CSprite2d::DrawRect(CRect(0.0f, 0.0f, SCREEN_WIDTH, SCREEN_HEIGHT),
		CRGBA(0, 0, 0, (int32)(150.0f * open)));

	CFont::SetBackgroundOff();
	CFont::SetJustifyOff();
	CFont::SetCentreOn();
	CFont::SetCentreSize(SCREEN_WIDTH);
	CFont::SetPropOn();
	CFont::SetFontStyle(FONT_BANK);
	CFont::SetDropShadowPosition(1);
	CFont::SetDropColor(CRGBA(0, 0, 0, (int32)(255.0f * open)));

	for(int32 i = 0; i < m_nSlots; i++){
		float angle = TWOPI * i / m_nSlots;
		float x = centreX + Sin(angle) * radius;
		float y = centreY - Cos(angle) * radius;

		bool selected = i == m_nSelected;
		if(selected)
			CFont::SetScale(SCREEN_SCALE_X(0.6f), SCREEN_SCALE_Y(1.0f));
		else
			CFont::SetScale(SCREEN_SCALE_X(0.42f), SCREEN_SCALE_Y(0.72f));

		CFont::SetColor(CRGBA(255, 255, 255, (int32)((selected ? 255.0f : 140.0f) * open)));
		CFont::PrintString(x, y - SCREEN_SCALE_Y(8.0f), NameOf(m_aSlots[i]));
	}

	CFont::SetDropShadowPosition(0);
}
