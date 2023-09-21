#pragma once
#include <stdint.h>
#include "mmlib.h"

struct DemoStats {
	uint32_t frameCount;
	uint32_t totalWriteSz;
	uint32_t currentWriteSz;

	uint32_t entDeltaTotalSz;
	uint32_t entDeltaCurrentSz;
	uint32_t entIndexTotalSz; // total index bytes written
	uint32_t entUpdateCount; // number of entity updates written
	uint32_t entDeltaSz[64]; // total size of deltas for each delta type (bit offset = idx)

	uint32_t plrDeltaTotalSz;
	uint32_t plrDeltaCurrentSz;
	uint32_t plrDeltaCount; // number of frames that had any player data
	uint32_t plrDeltaSz[64]; // total size of deltas for each delta type (bit offset = idx)

	uint32_t msgTotalSz;
	uint32_t msgCurrentSz;
	uint32_t msgSz[512]; // indexes at 256+ = temporary entity type - 256
	uint32_t msgCount; // number of network messages sent

	uint32_t cmdTotalSz;
	uint32_t cmdCurrentSz;
	uint32_t cmdCount; // number of commands sent

	void incTotals();

	void showStats(edict_t* ent);
};