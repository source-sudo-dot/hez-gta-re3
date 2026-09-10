#include "common.h"

#include "Crosshair.h"

#include "General.h"
#include "Sprite2d.h"
#include "Timer.h"

bool  CCrosshair::bModern = true;
bool  CCrosshair::bHitMarkers = true;
float CCrosshair::m_fSize = 5.0f;
float CCrosshair::m_fMarkerTime = 0.0f;
bool  CCrosshair::m_bHeadShot = false;
bool  CCrosshair::m_bKill = false;

// how long a marker stays up
#define MARKER_SECONDS (0.30f)
// how round the ring looks
#define RING_SEGMENTS (44)
// how far the softened edge reaches, in pixels either side
#define EDGE_FEATHER (0.75f)
// the hairline of black that carries the shape over a bright road
#define OUTLINE_WIDTH (1.0f)

void
CCrosshair::RegisterHit(bool headShot)
{
	// a kill already showing is the more interesting news, leave it alone
	if(m_bKill && m_fMarkerTime > 0.0f)
		return;

	m_bHeadShot = headShot;
	m_bKill = false;
	m_fMarkerTime = MARKER_SECONDS;
}

// The shot that kills is registered as a hit first and comes through here after, so
// this only has the colour to add.  It must not touch the shape: the flag CDarkel is
// handed is only set when the game decides to take the head off, which for most
// weapons is a one in sixteen roll, so most head shots reach here saying they were
// not one.  The hit knows better, it read the part off the collision itself.
void
CCrosshair::RegisterKill(bool headShot)
{
	if(m_fMarkerTime <= 0.0f)
		m_bHeadShot = headShot;
	else if(headShot)
		m_bHeadShot = true;

	m_bKill = true;
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

// Flat polygons come out with hard edges, and at the size this is set to the ring is
// only a couple of pixels across, so every stair step shows.  Laying a slightly wider
// pass at part alpha underneath leaves a soft pixel along both edges, which is what the
// hardware would have put there had it been antialiasing the scene.
static void
DrawRingSoft(float x, float y, float radius, float half, const CRGBA &col)
{
	DrawRing(x, y, radius, half + EDGE_FEATHER, CRGBA(col.r, col.g, col.b, col.a / 2));
	DrawRing(x, y, radius, half, col);
}

static void
DrawLineSoft(float x1, float y1, float x2, float y2, float half, const CRGBA &col)
{
	DrawLine(x1, y1, x2, y2, half + EDGE_FEATHER, CRGBA(col.r, col.g, col.b, col.a / 2));
	DrawLine(x1, y1, x2, y2, half, col);
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
	// the marker keeps the thinner stroke, the ring reads better a quarter heavier
	float half = Max(radius * 0.055f, 1.0f);
	float ringHalf = Max(radius * 0.06875f, 1.25f);

	CRGBA outline(0, 0, 0, 205);
	CRGBA white(255, 255, 255, 235);

	// a hairline of black either side of the ring, in place of the soft halo that used
	// to sit under it, so the shape holds against a pale road as well as a dark one
	DrawRingSoft(x, y, radius, ringHalf + OUTLINE_WIDTH, outline);
	DrawRingSoft(x, y, radius, ringHalf, white);

	if(!bHitMarkers || m_fMarkerTime <= 0.0f)
		return;

	float life = m_fMarkerTime / MARKER_SECONDS;
	int32 alpha = (int32)(235.0f * life);

	// red only when the shot killed, whichever shape it is
	CRGBA col = m_bKill ? CRGBA(235, 55, 45, alpha) : CRGBA(255, 255, 255, alpha);

	// how far the marker reaches, the same either way so they sit in the same place
	float reach = radius * 1.10f;

	CRGBA markerOutline(0, 0, 0, alpha * 205 / 235);

	if(!m_bHeadShot){
		// a cross through the ring
		float d = reach * 0.7071f;
		DrawLineSoft(x - d, y - d, x + d, y + d, half + OUTLINE_WIDTH, markerOutline);
		DrawLineSoft(x - d, y + d, x + d, y - d, half + OUTLINE_WIDTH, markerOutline);
		DrawLineSoft(x - d, y - d, x + d, y + d, half, col);
		DrawLineSoft(x - d, y + d, x + d, y - d, half, col);
		return;
	}

	// A head shot puts a burst of spokes in the same place instead, so the two read
	// apart by their shape and not only by colour.
	float inner = radius * 0.32f;
	for(int32 i = 0; i < 8; i++){
		float a = i * (TWOPI / 8) + PI / 8;
		float s = Sin(a);
		float c = -Cos(a);
		DrawLineSoft(x + s * inner, y + c * inner, x + s * reach, y + c * reach,
			half + OUTLINE_WIDTH, markerOutline);
	}
	for(int32 i = 0; i < 8; i++){
		float a = i * (TWOPI / 8) + PI / 8;
		float s = Sin(a);
		float c = -Cos(a);
		DrawLineSoft(x + s * inner, y + c * inner, x + s * reach, y + c * reach, half, col);
	}
}
