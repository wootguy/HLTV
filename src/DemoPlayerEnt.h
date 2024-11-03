#pragma once
#include <stdint.h>
#include "mstream.h"

#define PLR_FL_CONNECTED 1
#define PLR_FL_INWATER 2
#define PLR_FL_ONGROUND 4	// or partial ground
#define PLR_FL_WATERJUMP 8
#define PLR_FL_FROZEN 16
#define PLR_FL_DUCKING 32
#define PLR_FL_NOWEAPONS 64

#define FL_BIGPLRDELTA			(1 << 0) // if set, deltaBits = 4bytes, else 1 byte
#define FL_DELTA_FLAGS			(1 << 1)
#define FL_DELTA_BUTTON			(1 << 2)
#define FL_DELTA_PING			(1 << 3)
#define FL_DELTA_PUNCHANGLE_X	(1 << 4)
#define FL_DELTA_PUNCHANGLE_Y	(1 << 5)
#define FL_DELTA_PUNCHANGLE_Z	(1 << 6)
#define FL_DELTA_WEAPONDELTA	(1 << 7)

#define FL_DELTA_NAME			(1 << 8)
#define FL_DELTA_MODEL			(1 << 9)
#define FL_DELTA_STEAMID		(1 << 10)
#define FL_DELTA_COLORS			(1 << 11)
#define FL_DELTA_VIEWMODEL		(1 << 12)
#define FL_DELTA_WEAPONMODEL	(1 << 13)
#define FL_DELTA_WEAPONANIM		(1 << 14)
#define FL_DELTA_ARMORVALUE		(1 << 15)
#define FL_DELTA_VIEWOFS		(1 << 16)
#define FL_DELTA_FRAGS			(1 << 17)
#define FL_DELTA_FOV			(1 << 18)
#define FL_DELTA_OBSERVER		(1 << 19)

#define PLR_DELTA_BYTES 3 // size of a "big" player delta

#define PLR_WEP_ID			(1 << 0)
#define PLR_WEP_CLIP		(1 << 1)
#define PLR_WEP_CLIP2		(1 << 2)
#define PLR_WEP_AMMO		(1 << 3)
#define PLR_WEP_AMMO2		(1 << 4)
#define PLR_WEP_STATE		(1 << 5)

// extra info for player entities (combined with netedict data)
// TODO: weapon bits?
struct DemoPlayerEnt {
	uint8_t flags; // simplified edict flags (PLR_FL_*) (0 = invalid/deleted player)
	char name[32];
	char model[23];
	uint64_t steamid64;
	uint8_t topColor;
	uint8_t bottomColor;
	uint16_t ping;

	// player-specific entvars for 1st/3rd person views and scoreboard
	int16_t		punchangle[3];	// 13.3 fixed point
	uint16_t	viewmodel;		// 1st-person weapon model
	uint16_t	weaponmodel;	// 3rd-person weapon model
	uint16_t	armorvalue;
	uint16_t	button;
	uint16_t	frags;
	int16_t		view_ofs;	// eye position (Z) (12.4 fixed point)
	uint8_t		fov;
	uint8_t		weaponanim;
	uint8_t		observer; // observer target (upper 5bits, 7 = not spectating + dead), observer mode (lower 3bits)

	// bits indicating which weapon data was changed (PLR_WEP_*)
	uint8_t weaponDeltaFlags;

	uint16_t	clip;
	uint16_t	clip2;
	uint16_t	ammo;
	uint16_t	ammo2;
	uint8_t		weaponId;

	// misc weapon state
	uint8_t chargeReady : 2;
	uint8_t inAttack : 2;
	uint8_t inReload : 1;
	uint8_t inReloadSpecial : 1;
	uint8_t fireState : 1;

	int writeDeltas(mstream& writer, const DemoPlayerEnt& old);
	uint32_t readDeltas(mstream& reader);
};

/*
item->m_flTimeWeaponIdle = -0.001f;
item->m_flNextPrimaryAttack = -0.001f;
item->m_flNextSecondaryAttack = -0.001f;
item->fuser1 = -0.001f;
item->fuser2 = gun->m_flStartThrow;
item->fuser3 = gun->m_flReleaseThrow;
*/