#pragma once
#include <stdint.h>
#include "mstream.h"

#define PLR_FL_INWATER 1
#define PLR_FL_NOTARGET 2
#define PLR_FL_ONGROUND 4	// or partial ground
#define PLR_FL_WATERJUMP 8
#define PLR_FL_FROZEN 16
#define PLR_FL_DUCKING 32
#define PLR_FL_NOWEAPONS 64

#pragma pack(push, 1)

struct DemoPlayerDelta {
	uint8_t isConnectedChanged : 1;
	uint8_t nameChanged : 1;
	uint8_t modelChanged : 1;
	uint8_t steamIdChanged : 1;
	uint8_t colorsChanged : 1;
	uint8_t pingChanged : 1;
	uint8_t pmMoveChanged : 1;
	uint8_t flagsChanged : 1;

	uint8_t punchAngleXChanged : 1;
	uint8_t punchAngleYChanged : 1;
	uint8_t punchAngleZChanged : 1;
	uint8_t viewmodelChanged : 1;
	uint8_t weaponmodelChanged : 1;
	uint8_t weaponanimChanged : 1;
	uint8_t armorvalueChanged : 1;
	uint8_t buttonChanged : 1;

	uint8_t view_ofsChanged : 1;
	uint8_t fragsChanged : 1;
	uint8_t fovChanged : 1;
	uint8_t clipChanged : 1;
	uint8_t clip2Changed : 1;
	uint8_t ammoChanged : 1;
	uint8_t ammo2Changed : 1;
	uint8_t observerChanged : 1;

	// if isConnectedChanged:
	//     uint8 = is connected
	// if name changed:
	//     uint8 = name length
	//     char[] = name bytes (max 31)
	// if model changed:
	//     uint8 = model length
	//     char[] = model bytes (max 22)
	// if steamIdChanged:
	//     uint64_t = steamid64 bytes
	// if colorsChanged:
	//     byte = top color
	//     byte = bottom color
	// if ping changed:
	//		uint16 = ping
	// if pmMoveDeltaChanged:
	//      uint8 = movement commands since last delta
};

#pragma pack(pop)

// extra info for player entities (combined with netedict data)
// TODO: weapon bits?
struct DemoPlayerEnt {
	bool isConnected;
	char name[32];
	char model[23];
	uint64_t steamid64;
	uint8_t topColor;
	uint8_t bottomColor;
	uint16_t ping;
	uint16_t pmMoveCounter; // detect client FPS and lag spikes
	uint8_t flags; // simplified edict flags (PLR_FL_*)

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
	uint8_t		observer; // observer mode (upper 2bits), observer target (middle 5bits), and deadflag (LSB)

	// weapon info
	uint16_t	clip;
	uint16_t	clip2;
	uint16_t	ammo;
	uint16_t	ammo2;

	int writeDeltas(mstream& writer, const DemoPlayerEnt& old);
	DemoPlayerDelta readDeltas(mstream& reader);
};
