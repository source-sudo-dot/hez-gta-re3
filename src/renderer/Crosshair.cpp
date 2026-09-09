#include "common.h"

#include "Crosshair.h"

#include "Sprite2d.h"
#include "Timer.h"

bool  CCrosshair::bModern = true;
float CCrosshair::m_fMarkerTime = 0.0f;
int32 CCrosshair::m_nMarkerKind = 0;

enum {
	MARKER_HIT = 1,
	MARKER_HEADSHOT,
	MARKER_KILL,
};

// how long a marker stays up
#define MARKER_SECONDS (0.35f)

void
CCrosshair::RegisterHit(bool headShot)
{
	// a kill already showing is the more interesting news, leave it alone
	if(m_nMarkerKind == MARKER_KILL && m_fMarkerTime > 0.0f)
		return;

	m_nMarkerKind = headShot ? MARKER_HEADSHOT : MARKER_HIT;
	m_fMarkerTime = MARKER_SECONDS;
}

void
CCrosshair::RegisterKill(bool headShot)
{
	m_nMarkerKind = MARKER_KILL;
	m_fMarkerTime = MARKER_SECONDS;
}

// a filled block, given its middle and its half size
static void
DrawBlock(float x, float y, float halfW, float halfH, const CRGBA &col)
{
	CSprite2d::DrawRect(CRect(x - halfW, y - halfH, x + halfW, y + halfH), col);
}

void
CCrosshair::Draw(float x, float y, float size)
{
	// Draw() runs once per rendered frame and the marker should fade in real time, so it
	// is counted down on the rendered frame length, which the time scale does not touch.
	if(m_fMarkerTime > 0.0f){
		m_fMarkerTime -= CTimer::GetRenderFrameLength() / (float)LOGICAL_FRAME_RATE;
		if(m_fMarkerTime < 0.0f)
			m_fMarkerTime = 0.0f;
	}

	float unit = SCREEN_SCALE_Y(size);
	float thickness = Max(SCREEN_SCALE_Y(1.2f), 1.0f);

	CRGBA shadow(0, 0, 0, 140);
	CRGBA white(255, 255, 255, 230);

	// centre dot
	DrawBlock(x, y, thickness * 1.6f, thickness * 1.6f, shadow);
	DrawBlock(x, y, thickness, thickness, white);

	// four ticks with a gap, so the middle of the picture stays clear
	float gap = unit * 0.42f;
	float len = unit * 0.38f;
	DrawBlock(x, y - gap - len * 0.5f, thickness, len * 0.5f, white);
	DrawBlock(x, y + gap + len * 0.5f, thickness, len * 0.5f, white);
	DrawBlock(x - gap - len * 0.5f, y, len * 0.5f, thickness, white);
	DrawBlock(x + gap + len * 0.5f, y, len * 0.5f, thickness, white);

	if(m_fMarkerTime <= 0.0f)
		return;

	// The marker starts tight on the reticle and opens outwards as it fades, so a burst
	// of hits reads as separate flashes rather than one steady glow.
	float life = m_fMarkerTime / MARKER_SECONDS;
	int32 alpha = (int32)(255.0f * life);
	float spread = unit * (0.55f + 0.45f * (1.0f - life));
	float markHalf = Max(unit * 0.10f, thickness);

	CRGBA col;
	switch(m_nMarkerKind){
	case MARKER_KILL:     col = CRGBA(230, 60, 50, alpha); break;
	case MARKER_HEADSHOT: col = CRGBA(255, 200, 60, alpha); break;
	default:              col = CRGBA(255, 255, 255, alpha); break;
	}

	// four blocks set out on the diagonals
	float d = spread * 0.7071f;
	DrawBlock(x - d, y - d, markHalf, markHalf, CRGBA(0, 0, 0, alpha / 2));
	DrawBlock(x + d, y - d, markHalf, markHalf, CRGBA(0, 0, 0, alpha / 2));
	DrawBlock(x - d, y + d, markHalf, markHalf, CRGBA(0, 0, 0, alpha / 2));
	DrawBlock(x + d, y + d, markHalf, markHalf, CRGBA(0, 0, 0, alpha / 2));

	float inner = markHalf * 0.6f;
	DrawBlock(x - d, y - d, inner, inner, col);
	DrawBlock(x + d, y - d, inner, inner, col);
	DrawBlock(x - d, y + d, inner, inner, col);
	DrawBlock(x + d, y + d, inner, inner, col);
}
