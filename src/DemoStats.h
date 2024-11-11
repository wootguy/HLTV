#pragma once
#include <stdint.h>

#ifdef HLCOOP_BUILD
#include "extdll.h"
#else
#include "mmlib.h"
#endif

enum DeltaCats {
	DELTA_CAT_USERCMD_LERP,
	DELTA_CAT_USERCMD_MSEC,
	DELTA_CAT_USERCMD_ANGLES,
	DELTA_CAT_USERCMD_MOVE,
	DELTA_CAT_USERCMD_LIGHTLEVEL,
	DELTA_CAT_USERCMD_BUTTONS,
	DELTA_CAT_USERCMD_IMPULSE,
	DELTA_CAT_USERCMD_WEAPON,
};

struct DemoStats {
	uint32_t frameCount;
	uint32_t bigFrameCount;
	uint32_t giantFrameCount;
	uint32_t totalWriteSz;
	uint32_t currentWriteSz;

	uint32_t entDeltaTotalSz;
	uint32_t entDeltaCurrentSz;
	uint32_t entIndexTotalSz; // total index bytes written
	uint32_t entUpdateCount; // number of entity updates written
	uint32_t entDeltaCatSz[64]; // total size of deltas for each delta category

	uint32_t msgTotalSz;
	uint32_t msgCurrentSz;
	uint32_t msgSz[512]; // indexes at 256+ = temporary entity type - 256
	uint32_t msgCount; // number of network messages sent

	uint32_t eventTotalSz;
	uint32_t eventCurrentSz;
	uint32_t eventCount; // number of events sent
	uint32_t evtSize[256];

	uint32_t usercmdTotalSz;
	uint32_t usercmdCurrentSz;
	uint32_t usercmdCount; // number of usercmds sent
	uint32_t usercmdSz[16];

	uint32_t cmdTotalSz;
	uint32_t cmdCurrentSz;
	uint32_t cmdCount; // number of commands sent

	uint32_t sprayCount;
	uint32_t sprayCurrentSz;
	uint32_t sprayTotalSz;

	// calculate current frame size and increment stats
	// accounts for variable size time/framesize
	void incTotals();

	void calcFrameSize();

	void showStats(edict_t* ent);
};