#pragma once
#include "meta_utils.h"

#define MAX_EDICTS 8192 // sven co-op

void MapInit(edict_t* pEdictList, int edictCount, int maxClients);
void StartFrame();
void ClientLeave(edict_t* plr);
