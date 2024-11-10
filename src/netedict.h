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
#define FL_DELTA_ETYPE_CHANGED	(1 << 2)
#define FL_DELTA_ANIM_CHANGED	(1 << 4)

class CBasePlayer;

enum DeltaStatCategories {
	FL_DELTA_CAT_EDFLAG,
	FL_DELTA_CAT_ORIGIN,
	FL_DELTA_CAT_ANGLES,
	FL_DELTA_CAT_MISC,
	FL_DELTA_CAT_INTERNAL,
	
	FL_DELTA_CAT_DISPLAY,
	FL_DELTA_CAT_ANIM,
	FL_DELTA_CAT_RENDER,
	FL_DELTA_CAT_VISIBILITY,

	FL_DELTA_CAT_PLAYER,
	FL_DELTA_CAT_PLAYER_INFO,
	FL_DELTA_CAT_PLAYER_STATE_FAST,
	FL_DELTA_CAT_PLAYER_STATE_SLOW,
	FL_DELTA_CAT_PLAYER_WEAPON,
};

#define PACK_WEAPON_STATE(fireState, inReloadSpecial, inReload, inAttack, chargeReady) \
	(((fireState & 1) << 6) | ((inReloadSpecial & 1) << 5) | ((inReload & 1) << 4) | ((inAttack & 3) << 2) | (chargeReady & 3))

#define UNPACK_WEAPON_STATE(weaponState, fireState, inReloadSpecial, inReload, inAttack, chargeReady) \
	chargeReady = weaponState & 0x3; \
	inAttack = (weaponState >> 2) & 0x3; \
	inReload = (weaponState >> 4) & 0x1; \
	inReloadSpecial = (weaponState >> 5) & 0x1; \
	fireState = (weaponState >> 6) & 0x1;

enum ENTITY_TYPES {
	ETYPE_INVALID, // deleted entity
	ETYPE_GENERIC,
	ETYPE_MONSTER,
	ETYPE_PLAYER,
	ETYPE_BEAM,
};

#define PLR_FL_CONNECTED 1
#define PLR_FL_INWATER 2
#define PLR_FL_ONGROUND 4	// or partial ground
#define PLR_FL_WATERJUMP 8
#define PLR_FL_FROZEN 16
#define PLR_FL_DUCKING 32
#define PLR_FL_NOWEAPONS 64

#define PLR_NO_WEAPON_MODEL ((1 << MODEL_BITS) - 1) // special value indicating the player weapon model is null

// edict with only the data needed for rendering, and only the bits needed
struct netedict {
	uint8_t		etype;			// ENTITY_TYPES
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

	// player info
	uint64_t	steamid64;
	char		name[32];
	char		model[23];
	uint8_t		topColor;
	uint8_t		bottomColor;

	// player state fast (frequently updated)
	int16_t		frags;
	uint16_t	button;
	uint16_t	armorvalue;
	uint16_t	ping;
	int16_t		punchangle[3];	// 13.3 fixed point
	uint8_t		playerFlags;	// PLR_FL_*

	// player state slow (infrequently updated)
	uint16_t	viewEnt;		// entity this players view is set to. 0 = self (e.g. cameras)
	uint16_t	viewmodel;		// 1st-person weapon model
	uint16_t	weaponmodel;	// 3rd-person weapon model
	int16_t		view_ofs;		// eye position (Z) (12.4 fixed point)
	uint8_t		fov;
	uint8_t		specTarget;		// 5 bits
	uint8_t		specMode;		// 3 bits
	uint8_t		weaponId;		// for client prediction
	uint8_t		deadFlag;		// 4 bits (discard_body ignored)

	// player weapon state (frequently updated together)
	uint16_t	clip;
	uint16_t	clip2;
	uint16_t	ammo;
	uint16_t	ammo2;
	uint8_t		weaponanim;		// 1st-person animation
	uint8_t		weaponState;	// combination of the weapon bits below
	
	// weaponState bits for prediction (not networked)
	uint8_t		chargeReady : 2;
	uint8_t		inAttack : 2;
	uint8_t		inReload : 1;
	uint8_t		inReloadSpecial : 1;
	uint8_t		fireState : 1;

	// internal vars (not networked/written)
	uint32_t	deltaBitsLast;		// last delta bits read/written
	float		lastAnimationReset; // last time animation was reset
	bool		forceNextFrame;		// force sending the next frame value, even if unchanged (for animation resets)
	string_t	classname_stringt;	// quickly test if the classname has changed
	float		lastPingTime;		// last time ping was updated

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

private:
	void loadPlayer(CBasePlayer* plr);
	void applyPlayer(CBasePlayer* plr);
};