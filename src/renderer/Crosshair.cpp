#include "common.h"

#include "Crosshair.h"

#include "General.h"
#include "Sprite2d.h"
#include "Timer.h"

bool  CCrosshair::bModern = true;
float CCrosshair::m_fSize = 5.0f;
float CCrosshair::m_fMarkerTime = 0.0f;
int32 CCrosshair::m_nMarkerKind = 0;

enum {
	MARKER_HIT = 1,
	MARKER_HEADSHOT,
	MARKER_KILL,
};

// how long a marker stays up
#define MARKER_SECONDS (0.30f)
// how round the ring looks
#define RING_SEGMENTS (28)

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

// Draw2DPolygon walks its corners in the order 3, 4, 2, 1, so they are handed over in
// the order that makes that walk go round the shape.
static void
DrawQuad(float ax, float ay, float bx, float by, float cx, float cy, float dx, float dy, const CRGBA &col)
{
	CSprite2d::Draw2DPolygon(dx, dy, cx, cy, ax, ay, bx, by, col);
}

// a straight line of a given thickness between two points, at any angle
static void
DrawLine(float x1, float y1, float x2, float y2, float half, const CRGBA &col)
{
	float dx = x2 - x1;
	float dy = y2 - y1;
	float len = Sqrt(SQR(dx) + SQR(dy));
	if(len < 0.0001f)
		return;

	float nx = -dy / len * half;
	float ny = dx / len * half;
	DrawQuad(x1 + nx, y1 + ny, x2 + nx, y2 + ny, x2 - nx, y2 - ny, x1 - nx, y1 - ny, col);
}

static void
DrawRing(float x, float y, float radius, float half, const CRGBA &col)
{
	float step = TWOPI / RING_SEGMENTS;
	for(int32 i = 0; i < RING_SEGMENTS; i++){
		float a0 = i * step;
		float a1 = (i + 1) * step;
		DrawLine(x + Sin(a0) * radius, y - Cos(a0) * radius,
			x + Sin(a1) * radius, y - Cos(a1) * radius, half, col);
	}
}

void
CCrosshair::Draw(float x, float y, float scale)
{
	// Draw() runs once per rendered frame and the marker should fade in real time, so it
	// is counted down on the rendered frame length, which the time scale does not touch.
	if(m_fMarkerTime > 0.0f){
		m_fMarkerTime -= CTimer::GetRenderFrameLength() / (float)LOGICAL_FRAME_RATE;
		if(m_fMarkerTime < 0.0f)
			m_fMarkerTime = 0.0f;
	}

	// one scale on both axes, or the ring comes out an ellipse
	float radius = SCREEN_SCALE_Y(Max(m_fSize, 1.0f)) * scale;
	float half = Max(radius * 0.055f, 1.0f);

	CRGBA shadow(0, 0, 0, 110);
	CRGBA white(255, 255, 255, 235);

	DrawRing(x, y, radius, half * 1.9f, shadow);
	DrawRing(x, y, radius, half, white);

	// the dot in the middle
	CSprite2d::DrawRect(CRect(x - half * 1.4f, y - half * 1.4f, x + half * 1.4f, y + half * 1.4f), white);

	if(m_fMarkerTime <= 0.0f)
		return;

	float life = m_fMarkerTime / MARKER_SECONDS;
	int32 alpha = (int32)(235.0f * life);

	CRGBA col;
	if(m_nMarkerKind == MARKER_KILL)
		col = CRGBA(235, 55, 45, alpha);
	else
		col = CRGBA(255, 255, 255, alpha);

	if(m_nMarkerKind == MARKER_HIT){
		// a cross through the ring
		float d = radius * 0.78f;
		DrawLine(x - d, y - d, x + d, y + d, half, col);
		DrawLine(x - d, y + d, x + d, y - d, half, col);
		return;
	}

	// A head shot or a kill puts a burst of spokes outside the ring instead, so the two
	// read apart at a glance rather than by colour alone.
	float inner = radius * 1.25f;
	float outer = radius * (1.75f + 0.55f * (1.0f - life));
	for(int32 i = 0; i < 8; i++){
		float a = i * (TWOPI / 8) + PI / 8;
		float s = Sin(a);
		float c = -Cos(a);
		DrawLine(x + s * inner, y + c * inner, x + s * outer, y + c * outer, half, col);
	}
}
