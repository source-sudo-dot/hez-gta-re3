#include "common.h"

#include "WeaponWheel.h"

#include "Camera.h"
#include "CutsceneMgr.h"
#include "Draw.h"
#include "Font.h"
#include "Frontend.h"
#include "General.h"
#include "Hud.h"
#include "ModelInfo.h"
#include "Pad.h"
#include "PlayerInfo.h"
#include "PlayerPed.h"
#include "Sprite2d.h"
#include "Text.h"
#include "Timer.h"
#include "TxdStore.h"
#include "Weapon.h"
#include "WeaponInfo.h"
#include "World.h"

bool  CWeaponWheel::bOpen = false;
int32 CWeaponWheel::m_aSlots[TOTAL_WEAPON_SLOTS];
int32 CWeaponWheel::m_nSlots = 0;
int32 CWeaponWheel::m_nSelected = 0;
float CWeaponWheel::m_fOpenAmount = 0.0f;
float CWeaponWheel::m_fPointX = 0.0f;
float CWeaponWheel::m_fPointY = 0.0f;
int32 CWeaponWheel::m_nStowedSlot = WEAPONSLOT_UNARMED;
uint32 CWeaponWheel::m_nPressedAt = 0;
bool  CWeaponWheel::m_bWaitingToOpen = false;

// how far the stick has to be pushed before it counts as pointing somewhere
#define WHEEL_STICK_DEADZONE (0.35f)
// mouse pixels for a full push of the stick
#define WHEEL_MOUSE_RANGE (60.0f)
// the ring, in the 640x448 units the rest of the hud is laid out in
#define WHEEL_RADIUS (100.0f)
#define WHEEL_ICON_SIZE (52.0f)
// The button is given this long before the ring opens.  Let go inside it and nothing opens at
// all - the weapon simply goes away or comes back.
#define WHEEL_HOLD_MS (200)

void
CWeaponWheel::Init(void)
{
	bOpen = false;
	m_nSlots = 0;
	m_nSelected = 0;
	m_fOpenAmount = 0.0f;
	m_fPointX = 0.0f;
	m_fPointY = 0.0f;
	m_nStowedSlot = WEAPONSLOT_UNARMED;
	m_bWaitingToOpen = false;
}

bool
CWeaponWheel::CanOpen(void)
{
	CPlayerPed *player = FindPlayerPed();
	if(player == nil)
		return false;
	// on foot only
	if(FindPlayerVehicle() != nil)
		return false;
	if(player->Dead() || player->m_nPedState == PED_DIE || player->m_nPedState == PED_ARRESTED)
		return false;
	if(CPad::GetPad(0)->ArePlayerControlsDisabled())
		return false;
	// the scripts lock the weapon in places, and the game's own weapon switch honours that
	if(CPlayerPed::bDontAllowWeaponChange)
		return false;
	if(FrontEndMenuManager.m_bMenuActive || CCutsceneMgr::IsRunning())
		return false;
	// the first person sights take the whole screen and read the same stick
	if(TheCamera.Using1stPersonWeaponMode())
		return false;
	return true;
}

// The slots the player has something in that can still be used, in slot order.  The unarmed
// slot is always on the ring so there is a way back to the fists.
void
CWeaponWheel::CollectSlots(void)
{
	CPlayerPed *player = FindPlayerPed();
	m_nSlots = 0;
	if(player == nil)
		return;

	for(int32 i = WEAPONSLOT_UNARMED; i < TOTAL_WEAPON_SLOTS; i++){
		if(i != WEAPONSLOT_UNARMED &&
		   !(player->HasWeaponSlot(i) && player->GetWeapon(i).HasWeaponAmmoToBeUsed()))
			continue;
		m_aSlots[m_nSlots++] = i;
	}
}

void
CWeaponWheel::Open(void)
{
	CPlayerPed *player = FindPlayerPed();

	CollectSlots();
	if(m_nSlots < 2)	// nothing worth picking from
		return;

	m_nSelected = 0;
	for(int32 i = 0; i < m_nSlots; i++)
		if(m_aSlots[i] == player->m_currentWeapon)
			m_nSelected = i;

	m_fPointX = 0.0f;
	m_fPointY = 0.0f;
	bOpen = true;
	CTimer::SetCodePause(true);
}

void
CWeaponWheel::Close(bool takeSelection)
{
	CPlayerPed *player = FindPlayerPed();

	if(takeSelection && player != nil && m_nSelected >= 0 && m_nSelected < m_nSlots){
		int32 slot = m_aSlots[m_nSelected];
		if(slot != player->m_currentWeapon)
			player->MakeChangesForNewWeapon(slot);
	}

	bOpen = false;
	CTimer::SetCodePause(false);
}

// A tap of the button rather than a hold: the weapon goes away and the player is on his fists,
// and the next tap takes back the one that was put away.
void
CWeaponWheel::ToggleStow(void)
{
	CPlayerPed *player = FindPlayerPed();
	if(player == nil)
		return;

	if(player->m_currentWeapon != WEAPONSLOT_UNARMED){
		m_nStowedSlot = player->m_currentWeapon;
		player->MakeChangesForNewWeapon((int32)WEAPONSLOT_UNARMED);
		return;
	}

	// only back to something still carried with something left in it
	if(m_nStowedSlot == WEAPONSLOT_UNARMED)
		return;
	if(!player->HasWeaponSlot(m_nStowedSlot) || !player->GetWeapon(m_nStowedSlot).HasWeaponAmmoToBeUsed())
		return;

	player->MakeChangesForNewWeapon(m_nStowedSlot);
}

void
CWeaponWheel::Process(void)
{
	CPad *pad = CPad::GetPad(0);

	bool held = pad->GetWeaponWheel();

	if(!bOpen){
		if(!held){
			// let go before the ring had its chance: a tap, and the ring stays shut
			if(m_bWaitingToOpen){
				m_bWaitingToOpen = false;
				if(CanOpen())
					ToggleStow();
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

	// something took the player away while the ring was up
	if(!CanOpen()){
		Close(false);
		return;
	}

	if(!held){
		Close(true);
		return;
	}

	// Point with the right stick, or push the pick around with the mouse.  The stick says where
	// to point outright, the mouse moves the pointer, so both feel the way they do elsewhere.
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

	// The first slot sits at the top and they go round clockwise, so the angle the player is
	// pointing at maps straight onto one of them.
	if(Sqrt(SQR(m_fPointX) + SQR(m_fPointY)) > WHEEL_STICK_DEADZONE){
		float angle = HALFPI - CGeneral::GetATanOfXY(m_fPointX, m_fPointY);
		while(angle < 0.0f) angle += TWOPI;
		while(angle >= TWOPI) angle -= TWOPI;
		int32 slot = (int32)(angle / TWOPI * m_nSlots + 0.5f) % m_nSlots;
		m_nSelected = slot;
	}
}

// Vice City draws the fists from the hud sprites and every other weapon from a texture named
// after its model, kept in the model's own txd.  The hud draws its weapon icon the same way.
static void
DrawWeaponIcon(eWeaponType type, const CRect &rect, const CRGBA &colour)
{
	CWeaponInfo *info = CWeaponInfo::GetWeaponInfo(type);
	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);

	if(info->m_nModelId <= 0){
		CHud::Sprites[type].Draw(rect, colour);
		return;
	}

	CBaseModelInfo *model = CModelInfo::GetModelInfo(info->m_nModelId);
	if(model == nil || model->GetTxdSlot() < 0)
		return;
	TxdDef *txd = CTxdStore::GetSlot(model->GetTxdSlot());
	if(txd == nil || txd->texDict == nil)
		return;
	RwTexture *icon = RwTexDictionaryFindNamedTexture(txd->texDict, model->GetModelName());
	if(icon == nil)
		return;

	static CSprite2d sprite;
	sprite.m_pTexture = icon;
	sprite.Draw(rect, colour);
	// the texture belongs to the txd; the sprite must not take it with it
	sprite.m_pTexture = nil;
}

void
CWeaponWheel::Draw(void)
{
	// grow on the way in and shrink on the way out.  this runs per rendered frame, so it takes
	// the same time however fast the game draws
	float target = bOpen ? 1.0f : 0.0f;
	float blend = Clamp(0.4f * CTimer::GetRenderFrameLength(), 0.0f, 1.0f);
	m_fOpenAmount += (target - m_fOpenAmount) * blend;
	if(m_fOpenAmount < 0.004f){
		m_fOpenAmount = 0.0f;
		return;
	}
	if(m_nSlots < 2)
		return;

	CPlayerPed *player = FindPlayerPed();
	if(player == nil)
		return;

	float open = m_fOpenAmount;
	float centreX = SCREEN_WIDTH / 2.0f;
	float centreY = SCREEN_HEIGHT / 2.0f;
	// one scale for both axes, or the ring comes out an ellipse
	float radius = SCREEN_SCALE_Y(WHEEL_RADIUS) * (0.7f + 0.3f * open);
	float icon = SCREEN_SCALE_Y(WHEEL_ICON_SIZE);

	CSprite2d::DrawRect(CRect(0.0f, 0.0f, SCREEN_WIDTH, SCREEN_HEIGHT),
		CRGBA(0, 0, 0, (int32)(150.0f * open)));

	for(int32 i = 0; i < m_nSlots; i++){
		float angle = TWOPI * i / m_nSlots;
		float x = centreX + Sin(angle) * radius;
		float y = centreY - Cos(angle) * radius;

		bool selected = i == m_nSelected;
		float size = icon * (selected ? 0.62f : 0.45f);
		int32 alpha = (int32)((selected ? 255.0f : 150.0f) * open);

		if(selected)
			CSprite2d::DrawRect(CRect(x - size*1.25f, y - size*1.25f, x + size*1.25f, y + size*1.25f),
				CRGBA(255, 255, 255, (int32)(45.0f * open)));

		eWeaponType type = player->GetWeapon(m_aSlots[i]).m_eWeaponType;
		DrawWeaponIcon(type, CRect(x - size, y - size, x + size, y + size), CRGBA(255, 255, 255, alpha));
	}

	// what is picked, and how much of it there is, in the middle of the ring
	if(m_nSelected >= 0 && m_nSelected < m_nSlots){
		CWeapon &weapon = player->GetWeapon(m_aSlots[m_nSelected]);
		CWeaponInfo *info = CWeaponInfo::GetWeaponInfo(weapon.m_eWeaponType);
		if(weapon.m_eWeaponType != WEAPONTYPE_UNARMED && info->m_eWeaponFire != WEAPON_FIRE_MELEE){
			char buf[16];
			wchar wbuf[16];
			sprintf(buf, "%d", Min(weapon.m_nAmmoTotal, 9999));
			AsciiToUnicode(buf, wbuf);
			CFont::SetBackgroundOff();
			CFont::SetJustifyOff();
			CFont::SetCentreOn();
			CFont::SetCentreSize(SCREEN_WIDTH);
			CFont::SetPropOn();
			CFont::SetFontStyle(FONT_BANK);
			CFont::SetScale(SCREEN_SCALE_X(0.5f), SCREEN_SCALE_Y(0.8f));
			// The ring's dimmed backdrop and its other icons start from 150, the number from full,
			// and white on a dark screen stays legible long after the rest has gone.  Taken down
			// with the cube of the opening, it is still full with the ring open and gone with it.
			int32 textAlpha = (int32)(255.0f * open * open * open);
			CFont::SetColor(CRGBA(225, 225, 225, textAlpha));
			// The drop shadow was left however the HUD had it, black at full strength, so the number
			// faded and its black shadow stayed behind until the ring was gone.  It fades with it.
			CFont::SetDropShadowPosition(1);
			CFont::SetDropColor(CRGBA(0, 0, 0, textAlpha));
			CFont::PrintString(centreX, centreY - SCREEN_SCALE_Y(8.0f), wbuf);
			CFont::SetDropShadowPosition(0);
		}
	}
}
