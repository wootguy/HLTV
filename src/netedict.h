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

#define FL_DELTA_ORIGIN_CHANGED (1 << 0)
#define FL_DELTA_ANGLES_CHANGED (1 << 1)
#define FL_DELTA_FLAGS_CHANGED	(1 << 2)
#define FL_DELTA_ANIM_CHANGED	(1 << 4)

enum DeltaStatCategories {
	FL_DELTA_CAT_EDFLAG,
	FL_DELTA_CAT_ORIGIN,
	FL_DELTA_CAT_ANGLES,
	FL_DELTA_CAT_ANIM,
	FL_DELTA_CAT_RENDER,
	FL_DELTA_CAT_MISC,
	FL_DELTA_CAT_INTERNAL,
	FL_DELTA_CAT_VISIBILITY,
};


#define EDFLAG_VALID 1		// if no other flag is set, then it's a generic model entity (BSP/mdl/spr)
#define EDFLAG_MONSTER 2	// should display health/name
#define EDFLAG_PLAYER 4		// special model loading and rendering
#define EDFLAG_BEAM 8		// lasers and stuff

// edict with only the data needed for rendering, and only the bits needed
struct netedict {
	uint8_t		edflags;		// EDFLAG_*  (0 == invalid/deleted edict)
	int32_t		origin[3];		// 21.3 fixed point (beams), or 19.5 fixed point (everything else)
	uint32_t	angles[3];		// 21.3 fixed point (beams), or 0-360 scaled to uint16_t (everything else)
	uint16_t	modelindex;
	uint8_t		visibility[4];	// players who can see this entity (4-byte bitfield)

	uint8_t		skin;
	uint8_t		body;			// sub-model selection for studiomodels
	uint16_t 	effects;
	uint8_t		colormap;

	uint8_t		sequence;		// animation sequence
	uint8_t		gait;			// gait sequence (player)
	uint8_t		blend;			// animation blend (grunts crouching+shooting)
	uint8_t		frame;			// % playback position in animation sequences (0..255)
	int8_t		framerate;		// animation playback rate (-8x to 8x) (4.4 fixed point)
	uint16_t	controller_lo;	// bone controllers 0-1 settings
	uint16_t	controller_hi;	// bone controllers 2-3 settings
	uint16_t	blending;		

	uint16_t	scale;			// rendering scale (0..255) (8.8 fixed point)

	uint8_t		rendermode; // 3 bits
	uint8_t		renderfx;	// 5 bits
	uint8_t		renderamt;
	uint8_t		rendercolor[3];

	uint16_t	aiment;		// entity pointer when MOVETYPE_FOLLOW, 0 if movetype is not MOVETYPE_FOLLOW, combined ent index and attachment for beams
	uint8_t		classify;	// class_ovverride

	// internal entity state (for debugging)
	uint16_t	classname;		// classname table index (12 bits for 4096 len string pool)
	uint8_t		monsterstate;	// 4 bits
	uint8_t		schedule;		// index in monster schedule table (7 bits) (max hl schedul table is 81)
	uint8_t		task;			// index in the schedule task list (5 bits) (max HL task list len is 19)
	uint8_t		conditions_lo;	// bits_COND_*
	uint8_t		conditions_md;	// bits_COND_*
	uint16_t	conditions_hi;	// bits_COND_*
	uint16_t	memories;		// bits_MEMORY_* (12 bits in HL)
	uint32_t	health;

	// internal vars (not networked/written)
	uint32_t	deltaBitsLast; // last delta bits read/written
	float lastAnimationReset; // last time animation was reset
	bool forceNextFrame; // force sending the next frame value, even if unchanged (for animation resets)
	string_t	classname_stringt; // quickly test if the classname has changed

	netedict();
	void load(const edict_t& ed);
	void apply(edict_t* ed, char* stringpool);
	bool matches(netedict& other);

	// returns false if entity was deleted (no deltas)
	bool readDeltas(mstream& reader);

	// write deltas between this edict and the "old" edict
	// resets writer position on EDELTA_OVERFLOW 
	int writeDeltas(mstream& writer, netedict& old);

	// reset to a default state
	void reset();
};