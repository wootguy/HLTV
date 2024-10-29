#pragma once
#ifdef HLCOOP_BUILD
#include "extdll.h"
#else
#include "mmlib.h"
#endif

#include <stdint.h>
#include "mstream.h"
#include <vector>

// flags for indicating which edict fields were updated

// if set, deltaBits = 2+ bytes, and origin values are 19.5 fixed point and replace the previous origin
// otherwise, deltaBits = 1byte, and origin values are 11.5 fixed point and added to the previous origin
#define FL_BIGENTDELTA			(1 << 0)

// FL_DELTA_BIGORIGIN  + FL_BIGGERENTDELTA = 4 byte origin values.
// FL_DELTA_BIGORIGIN or FL_BIGGERENTDELTA = 2 byte origin deltas.
// 0 = 1 byte origin deltas
#define FL_DELTA_BIGORIGIN		(1 << 1)

#define FL_DELTA_ORIGIN_X		(1 << 2)
#define FL_DELTA_ORIGIN_Y		(1 << 3)
#define FL_DELTA_ORIGIN_Z		(1 << 4)
#define FL_DELTA_ANGLES_X		(1 << 5)
#define FL_DELTA_ANGLES_Y		(1 << 6)
#define FL_DELTA_ANGLES_Z		(1 << 7)	// high priority for apache

#define FL_BIGGERENTDELTA		(1 << 8)	// if set, deltaBits = 4 bytes
#define FL_DELTA_FRAME			(1 << 9)
#define FL_DELTA_CONTROLLER_LO	(1 << 10)	// high priority for turrets + apache
#define FL_DELTA_SEQUENCE		(1 << 11)
#define FL_DELTA_GAITBLEND		(1 << 12)	// gait sequence + lower blending byte
#define FL_DELTA_RENDERAMT		(1 << 13)
#define FL_DELTA_HEALTH			(1 << 14)
#define FL_DELTA_FRAMERATE		(1 << 15)

#define FL_DELTA_EFFECTS		(1 << 16)
#define FL_DELTA_RENDERCOLOR_0	(1 << 17)
#define FL_DELTA_RENDERCOLOR_1	(1 << 18)
#define FL_DELTA_RENDERCOLOR_2	(1 << 19)
#define FL_DELTA_RENDERMODEFX	(1 << 20)
#define FL_DELTA_SKIN			(1 << 21)
#define FL_DELTA_BODY			(1 << 22)
#define FL_DELTA_SCALE			(1 << 23)
#define FL_DELTA_COLORMAP		(1 << 24)
#define FL_DELTA_MODELINDEX		(1 << 25)
#define FL_DELTA_CONTROLLER_HI	(1 << 26)	// rare for something to have more than 2 controllers
#define FL_DELTA_AIMENT			(1 << 27)
#define FL_DELTA_CLASSIFY		(1 << 28)
#define FL_DELTA_EDFLAGS		(1 << 29)

#define ENT_DELTA_BYTES 4 // size of a "big" ent delta

#define EDFLAG_VALID 1		// if no other flag is set, then it's a generic model entity (BSP/mdl/spr)
#define EDFLAG_MONSTER 2	// should display health/name
#define EDFLAG_PLAYER 4		// special model loading and rendering
#define EDFLAG_BEAM 8		// lasers and stuff
#define EDFLAG_GOD 16		// GODMODE/DAMAGE_NO
#define EDFLAG_NOTARGET 32	// FL_NOTARGET

// edict with only the data needed for rendering, and only the bits needed
struct netedict {
	uint8_t		edflags;		// EDFLAG_*  (0 == invalid/deleted edict)
	int32_t		origin[3];		// 21.3 fixed point (beams), or 19.5 fixed point (everything else)
	uint32_t	health;
	uint32_t	angles[3];		// 21.3 fixed point (beams), or 0-360 scaled to uint16_t (everything else)
	uint16_t	modelindex;

	uint8_t		skin;
	uint8_t		body;			// sub-model selection for studiomodels
	uint16_t 	effects;
	uint8_t		colormap;

	uint8_t		sequence;		// animation sequence
	uint16_t	gaitblend;		// upper byte = gait sequence (player), lower byte = animation blend (grunts crouching+shooting)
	uint8_t		frame;			// % playback position in animation sequences (0..255)
	int8_t		framerate;		// animation playback rate (-8x to 8x) (4.4 fixed point)
	uint16_t	controller_lo;	// bone controllers 0-1 settings
	uint16_t	controller_hi;	// bone controllers 2-3 settings
	uint16_t	blending;		

	uint16_t	scale;			// rendering scale (0..255) (8.8 fixed point)

	uint8_t		rendermodefx; // upper 3 bits = rendermode, lower 5 bits = renderfx
	uint8_t		renderamt;
	uint8_t		rendercolor[3];

	uint16_t	aiment;		// entity pointer when MOVETYPE_FOLLOW, 0 if movetype is not MOVETYPE_FOLLOW
	uint8_t		classify;	// class_ovverride

	// internal vars (not networked/written)
	uint32_t	deltaBitsLast; // last delta bits read/written
	float lastAnimationReset; // last time animation was reset
	bool forceNextFrame; // force sending the next frame value, even if unchanged (for animation resets)

	netedict();
	void load(const edict_t& ed);
	void apply(edict_t* ed);
	bool matches(netedict& other);

	// returns false if entity was deleted (no deltas)
	bool readDeltas(mstream& reader);

	// write deltas between this edict and the "old" edict
	// resets writer position on EDELTA_OVERFLOW 
	int writeDeltas(mstream& writer, netedict& old);

	// reset to a default state
	void reset();
};