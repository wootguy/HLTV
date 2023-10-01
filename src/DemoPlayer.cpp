#include "DemoPlayer.h"
#include "mmlib.h"
#include "SvenTV.h"

const char* model_entity = "env_sprite";

void sendScoreInfo(int idx, float score, long deaths, float health, float armor, int classification, short ping, short icon) {
	MESSAGE_BEGIN(MSG_BROADCAST, MSG_ScoreInfo);
	WRITE_BYTE(idx);
	WRITE_BYTE(((byte*)&score)[0]);
	WRITE_BYTE(((byte*)&score)[1]);
	WRITE_BYTE(((byte*)&score)[2]);
	WRITE_BYTE(((byte*)&score)[3]);
	WRITE_LONG(deaths);
	WRITE_BYTE(((byte*)&health)[0]);
	WRITE_BYTE(((byte*)&health)[1]);
	WRITE_BYTE(((byte*)&health)[2]);
	WRITE_BYTE(((byte*)&health)[3]);
	WRITE_BYTE(((byte*)&armor)[0]);
	WRITE_BYTE(((byte*)&armor)[0]);
	WRITE_BYTE(((byte*)&armor)[0]);
	WRITE_BYTE(((byte*)&armor)[0]);
	WRITE_BYTE(classification);
	WRITE_SHORT(ping); // (is this really ping? I always see 0)
	WRITE_SHORT(icon);
	MESSAGE_END();
}

void sendScoreInfo(edict_t* ent) {
	CBasePlayer* plr = (CBasePlayer*)GET_PRIVATE(ent);

	if (!ent || !plr) {
		return;
	}

	float score = ent->v.frags;
	float health = ent->v.health;
	float armor = ent->v.armorvalue;
	int idx = ENTINDEX(ent);
	int classification = plr->m_fOverrideClass ? plr->m_iClassSelection : CLASS_PLAYER;
	int deaths = plr->m_iDeaths;
	int ping = 1337;
	int icon = 0;

	sendScoreInfo(idx, score, deaths, health, armor, classification, ping, icon);
}

void delayRenamePlayer(EHandle h_plr, string name) {
	edict_t* ent = h_plr;
	if (!ent) {
		return;
	}
	char* infoBuffer = g_engfuncs.pfnGetInfoKeyBuffer(ent);
	g_engfuncs.pfnSetClientKeyValue(ENTINDEX(ent), infoBuffer, "name", (char*)name.c_str());
}

DemoPlayer::DemoPlayer() {
	fileedicts = new netedict[MAX_EDICTS];
	fileplayerinfos = new DemoPlayerEnt[32];

	memset(fileplayerinfos, 0, MAX_PLAYERS * sizeof(DemoPlayerEnt));
}

DemoPlayer::~DemoPlayer() {
	delete[] fileplayerinfos;
	delete[] fileedicts;

	closeReplayFile();
}

bool DemoPlayer::openDemo(edict_t* plr, string path, float offsetSeconds, bool skipPrecache) {
	this->offsetSeconds = offsetSeconds;
	stopReplay();
	replayFile = fopen(path.c_str(), "rb");

	memset(&g_stats, 0, sizeof(DemoStats));

	if (!replayFile) {
		ClientPrint(plr, HUD_PRINTTALK, UTIL_VarArgs("Failed to open demo file: %s\n", path.c_str()));
		return false;
	}

	if (!fread(&demoHeader, sizeof(DemoHeader), 1, replayFile)) {
		ClientPrint(plr, HUD_PRINTTALK, "Invalid demo file: EOF before header read\n");
		closeReplayFile();
		return false;
	}

	if (demoHeader.version != DEMO_VERSION) {
		ClientPrint(plr, HUD_PRINTTALK, UTIL_VarArgs("Invalid demo version: %d (expected %d)\n", demoHeader.version, DEMO_VERSION));
		closeReplayFile();
		return false;
	}

	string mapname = string(demoHeader.mapname, 64);
	if (!g_engfuncs.pfnIsMapValid((char*)mapname.c_str())) {
		ClientPrint(plr, HUD_PRINTTALK, UTIL_VarArgs("Invalid demo map: %s\n", mapname.c_str()));
		closeReplayFile();
		return false;
	}

	if (demoHeader.modelLen > 1024 * 1024 * 32 || demoHeader.soundLen > 1024 * 1024 * 32) {
		ClientPrint(plr, HUD_PRINTTALK, UTIL_VarArgs("Invalid demo file: %u + %u byte model/sound data (too big)\n", demoHeader.modelLen, demoHeader.soundLen));
		closeReplayFile();
		return false;
	}

	if (demoHeader.modelLen) {
		char* modelData = new char[demoHeader.modelLen];

		if (!fread(modelData, demoHeader.modelLen, 1, replayFile)) {
			ClientPrint(plr, HUD_PRINTTALK, "Invalid demo file: incomplete model data\n");
			closeReplayFile();
			return false;
		}

		precacheModels = splitString(string(modelData, demoHeader.modelLen), "\n");

		for (int i = 0; i < precacheModels.size(); i++) {
			int replayIdx = demoHeader.modelIdxStart + i;
			replayModelPath[replayIdx] = precacheModels[i];
			//println("Replay model %d: %s", replayIdx, precacheModels[i].c_str());
		}

		delete[] modelData;
	}
	if (demoHeader.soundLen) {
		char* soundData = new char[demoHeader.soundLen];

		if (!fread(soundData, demoHeader.soundLen, 1, replayFile)) {
			ClientPrint(plr, HUD_PRINTTALK, "Invalid demo file: incomplete sound data\n");
			closeReplayFile();
			return false;
		}

		precacheSounds = splitString(string(soundData, demoHeader.soundLen), "\n");

		delete[] soundData;
	}
	if (demoHeader.modelLen == 0) {
		println("WARNING: Demo has no model list. The plugin may have been reloaded before the demo started.");
	}

	ClientPrint(plr, HUD_PRINTCONSOLE, UTIL_VarArgs("\nfile       : %s\n", path.c_str()));
	{
		time_t rawtime;
		struct tm* timeinfo;
		char buffer[80];

		time(&rawtime);
		timeinfo = localtime(&rawtime);

		strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
		ClientPrint(plr, HUD_PRINTCONSOLE, UTIL_VarArgs("date       : %s\n", buffer));
	}
	{
		int duration = demoHeader.endTime ? TimeDifference(demoHeader.startTime, demoHeader.endTime) : 0;
		int hours = duration / (60 * 60);
		int minutes = (duration - (hours * 60 * 60)) / 60;
		int seconds = duration % 60;

		if (duration) {
			ClientPrint(plr, HUD_PRINTCONSOLE, UTIL_VarArgs("duration   : %02d:%02d:%02d\n", hours, minutes, seconds));
		}
		else {
			ClientPrint(plr, HUD_PRINTCONSOLE, "duration   : unknown\n");
		}
	}
	ClientPrint(plr, HUD_PRINTCONSOLE, UTIL_VarArgs("map        : %s\n", mapname.c_str()));
	ClientPrint(plr, HUD_PRINTCONSOLE, UTIL_VarArgs("maxplayers : %d\n", demoHeader.maxPlayers));
	ClientPrint(plr, HUD_PRINTCONSOLE, UTIL_VarArgs("models     : %d (offset %d)\n", precacheModels.size(), demoHeader.modelIdxStart));
	ClientPrint(plr, HUD_PRINTCONSOLE, UTIL_VarArgs("sounds     : %d\n", precacheSounds.size()));

	ClientPrintAll(HUD_PRINTTALK, UTIL_VarArgs("[SvenTV] Opened: %s\n", path.c_str()));

	g_stats.totalWriteSz = sizeof(DemoHeader) + demoHeader.modelLen + demoHeader.soundLen;

	if (skipPrecache) {
		prepareDemo();
		return true;
	}

	if (toLowerCase(STRING(gpGlobals->mapname)) != toLowerCase(mapname)) {
		g_engfuncs.pfnServerCommand(UTIL_VarArgs("changelevel %s\n", mapname.c_str()));
		g_engfuncs.pfnServerExecute();
	}
}

void DemoPlayer::prepareDemo() {
	if (clearMapForPlayback) {
		for (int i = 1; i <= gpGlobals->maxClients; i++) {
			edict_t* ent = INDEXENT(i);
			CBasePlayer* plr = (CBasePlayer*)GET_PRIVATE(ent);
			if (isValidPlayer(ent) && plr) {
				// hide weapons
				//ent->v.viewmodel = 0;
				//ent->v.weaponmodel = 0;
				//plr->m_iHideHUD = 1;
			}
		}
		for (int i = gpGlobals->maxClients; i < gpGlobals->maxEntities; i++) {
			edict_t* ent = INDEXENT(i);

			// kill everything that isn't attached to a player
			if (ent && strlen(STRING(ent->v.classname)) > 0 && (!ent->v.aiment || (ent->v.aiment->v.flags & FL_CLIENT) == 0)) {
				REMOVE_ENTITY(ent);
			}
		}
	}
	else {
		// just remove the monsters
		for (int i = gpGlobals->maxClients; i < gpGlobals->maxEntities; i++) {
			edict_t* ent = INDEXENT(i);
			if (!ent) {
				continue;
			}
			int flags = ent->v.flags;
			if ((flags & FL_CLIENT) == 0 && (flags & FL_MONSTER) != 0) {
				REMOVE_ENTITY(ent);
			}
		}
	}

	for (int i = 0; i < MAX_PLAYERS; i++) {
		oldPlayerNames[i] = "";
	}

	replayStartTime = getEpochMillis() - (uint64_t)(offsetSeconds * 1000);
	replayFrame = 0;
	nextFrameOffset = ftell(replayFile);
	nextFrameTime = 0;
}

void DemoPlayer::precacheDemo() {
	if (!replayFile) {
		return;
	}

	for (int i = 0; i < precacheModels.size(); i++) {
		PrecacheModel(precacheModels[i]);
	}
	for (int i = 0; i < precacheSounds.size(); i++) {
		PrecacheSound(precacheSounds[i]);
	}
}

void DemoPlayer::stopReplay() {
	closeReplayFile();
	precacheModels.clear();
	precacheSounds.clear();
	replayModelPath.clear();
	replayEnts.clear();
}

void DemoPlayer::closeReplayFile() {
	if (replayFile) {
		fclose(replayFile);
		replayFile = NULL;
	}

	for (int i = 0; i < replayEnts.size(); i++) {
		edict_t* ent = replayEnts[i].h_ent;
		if (!ent) {
			continue;
		}

		if (ent->v.flags & FL_FAKECLIENT) {
			kickPlayer(ent);
		}
		else {
			REMOVE_ENTITY(replayEnts[i].h_ent);
		}			
	}
	replayEnts.clear();

	for (int i = 1; i < gpGlobals->maxClients; i++) {
		edict_t* ent = INDEXENT(i);
		if (!isValidPlayer(ent) || (ent->v.flags & FL_FAKECLIENT)) {
			continue;
		}

		
		if (oldPlayerNames->size()) {
			// remove (1) prefix if it was added during the demo
			char* infoBuffer = g_engfuncs.pfnGetInfoKeyBuffer(ent);
			g_engfuncs.pfnSetClientKeyValue(i, infoBuffer, "name", (char*)oldPlayerNames[i - 1].c_str());
		}
	}
}

bool DemoPlayer::readEntDeltas(mstream& reader) {
	uint32_t startOffset = reader.tell();

	uint16_t numEntDeltas = 0;
	reader.read(&numEntDeltas, 2);

	//println("Reading %d deltas", (int)numEntDeltas);

	int loop = -1;
	uint16_t fullIndex = 0;
	uint32_t indexSz = 0;
	for (int i = 0; i < numEntDeltas; i++) {
		loop++;
		uint32_t readOffset = reader.tell();

		uint16_t offset = 0;
		reader.read(&offset, 1);

		if (offset & FL_ENTIDX_LONG) {
			reader.seek(readOffset);
			reader.read(&offset, 2);
			fullIndex = offset >> 1;
			indexSz += 2;
		}
		else {
			indexSz += 1;
			fullIndex += offset >> 1;
		}

		if (fullIndex >= MAX_EDICTS) {
			println("ERROR: Invalid delta wants to update edict %d at %d\n", (int)fullIndex, loop);
			closeReplayFile();
			return false;
		}

		uint64_t startPos = reader.tell();

		netedict* ed = &fileedicts[fullIndex];
		if (!ed->readDeltas(reader)) {
			//println("Skip free %d", fullIndex);
			continue;
		}

		//println("Read index %d (%d bytes)", (int)fullIndex, (int)(reader.tell() - startPos));

		if (reader.eom()) {
			println("ERROR: Invalid delta hit unexpected eom at %d\n", loop);
			closeReplayFile();
			return false;
		}
	}

	g_stats.entIndexTotalSz += indexSz;
	g_stats.entDeltaCurrentSz = reader.tell() - startOffset;

	return true;
}

bool DemoPlayer::validateEdicts() {
	for (int i = 0; i < MAX_EDICTS; i++) {
		if (fileedicts[i].edflags && !(fileedicts[i].edflags & EDFLAG_BEAM) && fileedicts[i].aiment > 8192) {
			println("Invalid edict %d has %d", i, (int)fileedicts[i].aiment);
			return false;
		}
	}
	return true;
}

edict_t* DemoPlayer::convertEdictType(edict_t* ent, int i) {
	bool playerSlotFree = true; // todo
	bool isPlayer = (fileedicts[i].edflags & EDFLAG_PLAYER);
	bool entIsPlayer = ent->v.flags & FL_CLIENT;
	bool playerInfoLoaded = (i-1) < MAX_PLAYERS && fileplayerinfos[i - 1].flags;

	if (useBots && playerSlotFree && isPlayer && !entIsPlayer && playerInfoLoaded) {
		DemoPlayerEnt& info = fileplayerinfos[i - 1];

		// rename real players so that chat colors work for the bots
		for (int i = 1; i < gpGlobals->maxClients; i++) {
			edict_t* ent = INDEXENT(i);
			if (!isValidPlayer(ent) || (ent->v.flags & FL_FAKECLIENT)) {
				continue;
			}

			char* infoBuffer = g_engfuncs.pfnGetInfoKeyBuffer(ent);
			char* name = g_engfuncs.pfnInfoKeyValue(infoBuffer, "name");

			if (strcasecmp(name, info.name) == 0) {
				oldPlayerNames[i - 1] = string(info.name);
				g_engfuncs.pfnSetClientKeyValue(i, infoBuffer, "name", "");
				g_Scheduler.SetTimeout(delayRenamePlayer, 0.1f, ent, string(name));
			}
		}
		
		edict_t* bot = g_engfuncs.pfnCreateFakeClient(info.name);

		if (bot) {
			int eidx = ENTINDEX(bot);
			REMOVE_ENTITY(replayEnts[i].h_ent);
			gpGamedllFuncs->dllapi_table->pfnClientPutInServer(bot); // for scoreboard and HUD info only
			char* infoBuffer = g_engfuncs.pfnGetInfoKeyBuffer(bot);
			g_engfuncs.pfnSetClientKeyValue(eidx, infoBuffer, "name", info.name);
			g_engfuncs.pfnSetClientKeyValue(eidx, infoBuffer, "model", info.model);
			g_engfuncs.pfnSetClientKeyValue(eidx, infoBuffer, "topcolor", UTIL_VarArgs("%d", info.topColor));
			g_engfuncs.pfnSetClientKeyValue(eidx, infoBuffer, "bottomcolor", UTIL_VarArgs("%d", info.bottomColor));
			bot->v.weaponmodel = ALLOC_STRING(getReplayModel(info.weaponmodel).c_str());
			bot->v.viewmodel = ALLOC_STRING(getReplayModel(info.viewmodel).c_str());
			bot->v.frags = info.frags;
			bot->v.weaponanim = info.weaponanim;
			bot->v.armorvalue = info.armorvalue;
			bot->v.view_ofs.z = info.view_ofs / 16.0f;
			bot->v.deadflag = info.observer & 1 ? DEAD_DEAD : DEAD_NO;
			bot->v.takedamage = DAMAGE_NO;
			bot->v.movetype = MOVETYPE_NOCLIP;
			bot->v.solid = SOLID_SLIDEBOX;

			replayEnts[i].h_ent = ent = bot;
		}
		else {
			println("Failed to create bot");
		}
	}
	else if ((fileedicts[i].edflags & EDFLAG_BEAM) && (ent->v.flags & (FL_MONSTER | FL_CLIENT))) {
		map<string, string> keys;
		CBaseEntity* newEnt = CreateEntity("beam", keys, true);

		if (ent->v.flags & FL_CLIENT) {
			kickPlayer(ent);
		}
		else {
			REMOVE_ENTITY(replayEnts[i].h_ent);
		}

		replayEnts[i].h_ent = ent = newEnt->edict();
		ent->v.flags |= FL_CUSTOMENTITY;
		ent->v.effects |= EF_NODRAW;
		SET_MODEL(ent, "sprites/error.spr");
	}
	else if (!(fileedicts[i].edflags & EDFLAG_BEAM) && (ent->v.flags & (FL_MONSTER | FL_CLIENT)) == 0) {
		map<string, string> keys;
		keys["model"] = "sprites/error.spr";

		CBaseEntity* newEnt = CreateEntity(model_entity, keys, true);

		if (ent->v.flags & FL_CLIENT) {
			kickPlayer(ent);
		}
		else {
			REMOVE_ENTITY(replayEnts[i].h_ent);
		}

		replayEnts[i].h_ent = ent = newEnt->edict();
		ent->v.solid = SOLID_NOT;
		ent->v.effects |= EF_NODRAW;
		ent->v.movetype = MOVETYPE_NONE;
		ent->v.flags |= FL_MONSTER;
	}

	return ent;
}

bool DemoPlayer::createReplayEntities(int count) {
	while (count >= replayEnts.size()) {
		map<string, string> keys;
		keys["model"] = "sprites/error.spr";

		CBaseEntity* ent = CreateEntity(model_entity, keys, true);
		ent->pev->solid = SOLID_NOT;
		ent->pev->effects |= EF_NODRAW;
		ent->pev->movetype = MOVETYPE_NONE;
		ent->pev->flags |= FL_MONSTER;

		if (!ent) {
			println("Entity overflow at %d\n", (int)replayEnts.size());
			for (int i = 0; i < replayEnts.size(); i++) {
				REMOVE_ENTITY(replayEnts[i].h_ent);
			}
			closeReplayFile();
			return false;
		}

		ReplayEntity rent;
		memset(&rent, 0, sizeof(ReplayEntity));
		rent.h_ent = ent;

		replayEnts.push_back(rent);
	}

	return true;
}

void DemoPlayer::setupInterpolation(edict_t* ent, int i) {
	InterpInfo& interp = replayEnts[i].interp;

	if (fileedicts[i].deltaBitsLast & FL_DELTA_EDFLAGS) {
		// entity type changed. Don't interpolate first frame
		interp.originStart = interp.originEnd = ent->v.origin;
		interp.anglesStart = interp.anglesEnd = ent->v.angles;
		interp.frameStart = interp.frameEnd = ent->v.frame;
	}

	// interpolation setup
	if (!(fileedicts[i].edflags & EDFLAG_MONSTER)) {
		interp.originStart = interp.originEnd;
		interp.originEnd = ent->v.origin;

		interp.anglesStart = interp.anglesEnd;
		interp.anglesEnd = ent->v.angles;
	}
	else if (fileedicts[i].edflags & EDFLAG_MONSTER) {
		// monster data is updated at a framerate independent of the server
		uint32_t originMask = FL_DELTA_ORIGIN_X | FL_DELTA_ORIGIN_Y | FL_DELTA_ORIGIN_Z;
		uint32_t anglesMask = FL_DELTA_ANGLES_X | FL_DELTA_ANGLES_Y | FL_DELTA_ANGLES_Z;
		if (fileedicts[i].deltaBitsLast & (originMask|anglesMask)) {
			interp.originStart = interp.originEnd;
			interp.originEnd = ent->v.origin;

			interp.anglesStart = interp.anglesEnd;
			interp.anglesEnd = ent->v.angles;

			interp.estimatedUpdateDelay = clampf(gpGlobals->time - interp.lastMovementTime, 0, 0.1f); // expected time between frames
			interp.lastMovementTime = gpGlobals->time;
		}
	}

	// frame prediction
	if (fileedicts[i].edflags & (EDFLAG_MONSTER | EDFLAG_PLAYER)) {
		uint32_t animMask = FL_DELTA_FRAME | FL_DELTA_SEQUENCE | FL_DELTA_FRAMERATE;
		if (fileedicts[i].deltaBitsLast & animMask) {
			bool sequenceChanged = interp.sequenceEnd != ent->v.sequence;

			interp.frameEnd = ent->v.frame; // interpolate from here
			interp.animTime = gpGlobals->time; // time frame was set

			if (sequenceChanged || interp.framerateSmd == 0) {
				CBaseAnimating* anim = (CBaseAnimating*)GET_PRIVATE(ent);

				if (anim) {
					void* pmodel = GET_MODEL_PTR(ent);

					studiohdr_t* pstudiohdr;
					pstudiohdr = (studiohdr_t*)pmodel;
					if (pstudiohdr && pstudiohdr->id == 1414743113 && pstudiohdr->version == 10) {
						GetSequenceInfo(pmodel, &ent->v, &interp.framerateSmd, &interp.groundspeed);
						interp.sequenceLoops = ((GetSequenceFlags(pmodel, anim->pev) & STUDIO_LOOPING) != 0);
					}
					else {
						println("Invalid studio model: %s", STRING(ent->v.model));
					}
				}
				else {
					println("Not normal anim");
				}
			}

			interp.sequenceStart = interp.sequenceEnd;
			interp.sequenceEnd = ent->v.sequence;
		}
	}

	interp.framerateEnt = ent->v.framerate;
	ent->v.framerate = 0.000001f; // prevent the game trying to interpolate
}

edict_t* DemoPlayer::getReplayEntity(int idx) {
	if (idx < replayEnts.size()) {
		return replayEnts[idx].h_ent;
	}
	return NULL;
}

bool DemoPlayer::simulate(DemoFrame& header) {
	int errorSprIdx = g_engfuncs.pfnModelIndex("sprites/error.spr");

	for (int i = 1; i < MAX_EDICTS; i++) {
		if (!fileedicts[i].edflags) {
			if (i < replayEnts.size()) {
				replayEnts[i].h_ent.GetEdict()->v.effects |= EF_NODRAW;
			}
			continue;
		}

		// create ents until the client has enough to handle this demo frame
		if (!createReplayEntities(i)) {
			return false;
		}

		edict_t* ent = replayEnts[i].h_ent;
		ent = convertEdictType(ent, i);
		
		int oldModelIdx = ent->v.modelindex;

		fileedicts[i].apply(ent);

		setupInterpolation(ent, i);

		if (oldModelIdx != ent->v.modelindex) {
			string newModel = getReplayModel(ent->v.modelindex);
			bool isSprite = newModel.find(".spr") != string::npos;

			if ((fileedicts[i].edflags & EDFLAG_BEAM) && !isSprite) {
				println("Invalid model set on beam: %s", newModel.c_str());
			}
			else if (!(fileedicts[i].edflags & EDFLAG_PLAYER)) {
				SET_MODEL(ent, newModel.c_str());
			}
		}

		if (ent->v.modelindex == errorSprIdx) {
			// prevent console flooding with invalid frame errors
			ent->v.frame = 0;
		}

		/*
		if (g_engfuncs.pfnModelIndex("sprites/lgtning.spr") == ent->v.modelindex || g_engfuncs.pfnModelIndex("sprites/error.spr") == ent->v.modelindex) {
			SET_MODEL(ent, "sprites/error.spr");
			println("OK LIGHTNING: %f %d", ent->v.scale, ent->v.movetype);
		}
		*/
	}

	interpolateEdicts();

	return true;
}

string DemoPlayer::getReplayModel(uint16_t modelIdx) {
	static set<uint16_t> unknownSpam;

	if (replayModelPath.count(modelIdx)) {
		// Demo file model path
		return replayModelPath[modelIdx];
	}
	else if (g_indexToModel.count(modelIdx)) {
		// BSP model or whatever the game has loaded for this idx
		return g_indexToModel[modelIdx];
	}
	else {
		if (!unknownSpam.count(modelIdx)) {
			println("Unknown model idx %d", modelIdx);
			unknownSpam.insert(modelIdx);
		}

		return "sprites/error.spr";
	}
}

void DemoPlayer::convReplayEntIdx(uint16_t& eidx) {
	if (eidx >= replayEnts.size()) {
		println("Invalid replay ent idx %d", (int)eidx);
		eidx = 0;
		return;
	}
	eidx = ENTINDEX(replayEnts[eidx].h_ent);
}

void DemoPlayer::convReplayModelIdx(uint16_t& modelIdx) {
	modelIdx = g_engfuncs.pfnModelIndex(getReplayModel(modelIdx).c_str());
}

void DemoPlayer::convReplaySoundIdx(uint16_t& soundIdx) {
	if (soundIdx >= precacheSounds.size()) {
		println("Invalid replay sound idx %d", (int)soundIdx);
		soundIdx = 0;
		return;
	}

	string path = precacheSounds[soundIdx];
	if (g_SoundCache.find(path) == g_SoundCache.end()) {
		println("Invalid replay sound: %s", path.c_str());
		soundIdx = 0;
		return;
	}

	soundIdx = g_SoundCache[path];
}

bool DemoPlayer::readPlayerDeltas(mstream& reader) {
	uint32_t startOffset = reader.tell();

	uint32_t includedPlayers = 0;
	reader.read(&includedPlayers, 4);

	int numPlayerDeltas = 0;
	for (int i = 0; i < gpGlobals->maxClients; i++) {
		if ((includedPlayers & (1 << i)) == 0) {
			continue;
		}

		uint32_t deltas = fileplayerinfos[i].readDeltas(reader);
		numPlayerDeltas++;

		if (i+1 < replayEnts.size()) {
			edict_t* bot = replayEnts[i+1].h_ent;
			if (!bot || (bot->v.flags & FL_FAKECLIENT) == 0) {
				continue;
			}
			int eidx = ENTINDEX(bot);
			char* infoBuffer = g_engfuncs.pfnGetInfoKeyBuffer(bot);
			DemoPlayerEnt& info = fileplayerinfos[i];

			if (deltas & FL_DELTA_NAME) {
				g_engfuncs.pfnSetClientKeyValue(eidx, infoBuffer, "name", info.name);
			}
			if (deltas & FL_DELTA_MODEL) {
				g_engfuncs.pfnSetClientKeyValue(eidx, infoBuffer, "model", info.model);
			}
			if (deltas & FL_DELTA_COLORS) {
				g_engfuncs.pfnSetClientKeyValue(eidx, infoBuffer, "topcolor", UTIL_VarArgs("%d", info.topColor));
				g_engfuncs.pfnSetClientKeyValue(eidx, infoBuffer, "bottomcolor", UTIL_VarArgs("%d", info.bottomColor));
			}
			if (deltas & FL_DELTA_WEAPONMODEL) {
				bot->v.weaponmodel = ALLOC_STRING(getReplayModel(info.weaponmodel).c_str());
			}
			if (deltas & FL_DELTA_VIEWMODEL) {
				bot->v.viewmodel = ALLOC_STRING(getReplayModel(info.viewmodel).c_str());
			}
			bot->v.frags = info.frags;
			bot->v.weaponanim = info.weaponanim;
			bot->v.armorvalue = info.armorvalue;
			bot->v.view_ofs.z = info.view_ofs / 16.0f;
			bot->v.deadflag = info.observer & 1 ? DEAD_DEAD : DEAD_NO;
		}
	}

	//println("Got %d player deltas", numPlayerDeltas);

	g_stats.plrDeltaCurrentSz = reader.tell() - startOffset;

	return !reader.eom();
}

bool DemoPlayer::processTempEntityMessage(NetMessageData& msg) {
	uint8_t type = msg.data[0];

	byte* args = msg.data + 1;
	uint16_t* args16 = (uint16_t*)args;
	float* fargs = (float*)args;

	if (type > TE_USERTRACER) {
		println("Invalid temp ent type %d", (int)type);
		return false;
	}

	static uint8_t expectedSzLookup[TE_USERTRACER+1] = {
		37, // TE_BEAMPOINTS
		27, // TE_BEAMENTPOINT
		13, // TE_GUNSHOT
		18, // TE_EXPLOSION
		13, // TE_TAREXPLOSION
		17, // TE_SMOKE
		25, // TE_TRACER
		30, // TE_LIGHTNING
		17, // TE_BEAMENTS
		13, // TE_SPARKS
		13, // TE_LAVASPLASH
		13, // TE_TELEPORT
		15, // TE_EXPLOSION2
		0, // TE_BSPDECAL (19 or 17!!!)
		16, // TE_IMPLOSION
		32, // TE_SPRITETRAIL
		0, // unused index
		17, // TE_SPRITE
		29, // TE_BEAMSPRITE
		37, // TE_BEAMTORUS
		37, // TE_BEAMDISK
		37, // TE_BEAMCYLINDER
		11, // TE_BEAMFOLLOW
		18, // TE_GLOWSPRITE
		17, // TE_BEAMRING
		32, // TE_STREAK_SPLASH
		0, // unused index
		19, // TE_DLIGHT
		27, // TE_ELIGHT
		0, // TE_TEXTMESSAGE (depends on text/effect)
		30, // TE_LINE
		30, // TE_BOX

		// unused indexes
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,

		3, // TE_KILLBEAM
		17, // TE_LARGEFUNNEL
		27, // TE_BLOODSTREAM
		25, // TE_SHOWLINE
		27, // TE_BLOOD
		16, // TE_DECAL
		6, // TE_FIZZ
		30, // TE_MODEL
		22, // TE_EXPLODEMODEL
		43, // TE_BREAKMODEL
		16, // TE_GUNSHOTDECAL
		30, // TE_SPRITE_SPRAY
		14, // TE_ARMOR_RICOCHET
		17, // TE_PLAYERDECAL
		36, // TE_BUBBLES
		36, // TE_BUBBLETRAIL
		19, // TE_BLOODSPRITE
		14, // TE_WORLDDECAL
		14, // TE_WORLDDECALHIGH
		16, // TE_DECALHIGH
		29, // TE_PROJECTILE
		31, // TE_SPRAY
		7, // TE_PLAYERSPRITES
		17, // TE_PARTICLEBURST
		20, // TE_FIREFIELD
		10, // TE_PLAYERATTACHMENT
		2, // TE_KILLPLAYERATTACHMENTS
		35, // TE_MULTIGUNSHOT
		31 // TE_USERTRACER		
	};

	int expectedSz = expectedSzLookup[type];

	if (type == TE_BSPDECAL) {
		expectedSz = args16[7] ? 19 : 17;
	}
	if (type != TE_TEXTMESSAGE && msg.header.sz != expectedSz) {
		println("Bad size for %s (%d): %d != %d", te_names[type], (int)type, (int)msg.header.sz, expectedSz);
		return false;
	}


	switch (type) {
	case TE_BEAMPOINTS:
	case TE_BEAMDISK:
	case TE_BEAMCYLINDER:
	case TE_BEAMTORUS:
	case TE_SPRITETRAIL:
	case TE_SPRITE_SPRAY:
	case TE_SPRAY:
	case TE_PROJECTILE:
		convReplayModelIdx(args16[12]);
		//args16[12] = g_engfuncs.pfnModelIndex("sprites/laserbeam.spr");
		break;
	case TE_BEAMENTPOINT:
		convReplayEntIdx(args16[0]);
		convReplayModelIdx(args16[7]);
		break;
	case TE_KILLBEAM:
		convReplayEntIdx(args16[0]);
		break;
	case TE_BEAMENTS:
	case TE_BEAMRING:
		convReplayEntIdx(args16[0]);
		convReplayEntIdx(args16[1]);
		convReplayModelIdx(args16[2]);
		break;
	case TE_LIGHTNING: {
		uint16_t& arg = *(uint16_t*)(args + 27);
		convReplayModelIdx(arg);
		break;
	}
	case TE_BEAMSPRITE:
		convReplayModelIdx(args16[12]);
		convReplayModelIdx(args16[13]);
		break;
	case TE_BEAMFOLLOW:
	case TE_FIZZ:
		convReplayEntIdx(args16[0]);
		convReplayModelIdx(args16[1]);
		break;
	case TE_PLAYERSPRITES: {
		convReplayEntIdx(args16[0]);
		edict_t* plr = INDEXENT(args16[0]);
		if (!plr || !(plr->v.flags & FL_CLIENT)) {
			args16[0] = 1; // prevent fatal error
		}
		convReplayModelIdx(args16[1]);
		break;
	}
	case TE_EXPLOSION:
	case TE_SMOKE:
	case TE_SPRITE:
	case TE_GLOWSPRITE:
	case TE_LARGEFUNNEL:
		convReplayModelIdx(args16[6]);
		break;
	case TE_PLAYERATTACHMENT: {
		uint16_t entIdx = args[0];
		uint16_t& modelIdx = *(uint16_t*)(args + 5);
		convReplayEntIdx(entIdx);
		convReplayModelIdx(modelIdx);
		edict_t* plr = INDEXENT(entIdx);
		if (!plr || !(plr->v.flags & FL_CLIENT)) {
			entIdx = 1; // prevent fatal error
		}
		args[0] = (uint8_t)entIdx;
		break;
	}
	case TE_KILLPLAYERATTACHMENTS:
	case TE_PLAYERDECAL: {
		uint16_t entIdx = args[0];
		convReplayEntIdx(entIdx);
		edict_t* plr = INDEXENT(entIdx);
		if (!plr || !(plr->v.flags & FL_CLIENT)) {
			entIdx = 1; // prevent fatal error
		}
		args[0] = (uint8_t)entIdx;
		break;
	}
	case TE_BUBBLETRAIL:
	case TE_BUBBLES:
		convReplayModelIdx(args16[14]);
		break;
	case TE_BLOODSPRITE:
		convReplayModelIdx(args16[6]);
		convReplayModelIdx(args16[7]);
		break;
	case TE_FIREFIELD:
		convReplayModelIdx(args16[7]);
		break;
	case TE_EXPLODEMODEL:
		convReplayModelIdx(args16[8]);
		break;
	case TE_MODEL: {
		uint16_t& modelIdx = *(uint16_t*)(args + 25);
		convReplayModelIdx(modelIdx);
		break;
	}
	case TE_BREAKMODEL: {
		uint16_t& modelIdx = *(uint16_t*)(args + 37);
		convReplayModelIdx(modelIdx);
		break;
	}
	case TE_DECAL:
	case TE_GUNSHOTDECAL:
	case TE_DECALHIGH:
	{
		uint16_t& modelIdx = *(uint16_t*)(args + 13);
		convReplayModelIdx(modelIdx);
		break;
	}
	case TE_BSPDECAL:
		if (args16[7]) { // not world entity?
			convReplayEntIdx(args16[7]);
			//convReplayModelIdx(args16[5]); // (BSP model idx in demo should always match the game)
		}
		break;
	default:
		break;
	}

	//println("Play temp ent %d", (int)type);

	return true;
}

bool DemoPlayer::processDemoNetMessage(NetMessageData& msg) {
	byte* args = msg.data;
	uint16_t* args16 = (uint16_t*)args;

	g_stats.msgSz[msg.header.type] += msg.header.sz;

	switch (msg.header.type) {
	case SVC_TEMPENTITY:
		g_stats.msgSz[256 + msg.data[0]] += msg.header.sz;
		return processTempEntityMessage(msg);
	case MSG_StartSound: {
		uint16_t& flags = *(uint16_t*)(args);
		if ((flags & SND_SENTENCE) == 0) {
			uint16_t& soundIdx = *(uint16_t*)(args + (msg.header.sz - 2));
			convReplaySoundIdx(soundIdx);
		} // plugins can't change sentences(?), so client should match server
		return true;
	}
	case MSG_SayText: {
		// name coloring doesn't work for bots, so copy the name color to p1 and use them as the source
		uint16_t idx = args[0];
		convReplayEntIdx(idx);
		args[0] = idx;
		return true;
	}
	case MSG_ScoreInfo: {
		if (useBots) {
			uint16_t idx = args[0];
			convReplayEntIdx(idx);
			args[0] = idx;

			return true;
		}
		return false;
	}
	default:
		println("Unhandled netmsg %d", (int)msg.header.type);
		return false;
	}
}

bool DemoPlayer::readNetworkMessages(mstream& reader) {
	uint32_t startOffset = reader.tell();

	uint16_t numMessages;
	reader.read(&numMessages, 2);

	if (numMessages > MAX_NETMSG_FRAME) {
		println("Invalid net msg count %d", (int)numMessages);
		return false;
	}

	static NetMessageData msg;

	for (int i = 0; i < numMessages; i++) {
		reader.read(&msg.header, sizeof(DemoNetMessage));

		if (msg.header.hasOrigin) {
			reader.read(&msg.origin[0], 3);
			reader.read(&msg.origin[1], 3);
			reader.read(&msg.origin[2], 3);
		}
		if (msg.header.hasEdict) {
			reader.read(&msg.eidx, 2);
		}
		else {
			msg.eidx = 0;
		}
		if (msg.header.sz > MAX_NETMSG_DATA) {
			println("Invalid net msg size %d", (int)msg.header.sz);
			return false; // data corrupted, abort before SVC_BAD
		}
		reader.read(msg.data, msg.header.sz);

		if (msg.eidx >= replayEnts.size()) {
			println("Invalid msg ent %d", (int)msg.eidx);
			continue;
		}

		if (msg.header.hasEdict && replayEnts[msg.eidx].h_ent.GetEdict() && !(replayEnts[msg.eidx].h_ent.GetEdict()->v.flags & FL_CLIENT)) {
			continue; // target is not a player (bots disabled probably)
		}

		edict_t* ent = msg.header.hasEdict ? replayEnts[msg.eidx].h_ent.GetEdict() : NULL;
		if (msg.eidx == netmsgPlrIdx) {
			// redirect to game player
			msg.eidx = 1;
			ent = INDEXENT(1);
		}

		if (!processDemoNetMessage(msg)) {
			continue;
		}

		msg.send(msg.header.dest, ent);
	}

	g_stats.msgCurrentSz = reader.tell() - startOffset;
	g_stats.msgCount += numMessages;

	return true;
}

bool DemoPlayer::readEvents(mstream& reader) {
	uint32_t startOffset = reader.tell();

	uint8_t numEvents;
	reader.read(&numEvents, 1);

	if (numEvents > MAX_EVENT_FRAME) {
		println("Invalid net msg count %d", (int)numEvents);
		return false;
	}

	for (int i = 0; i < numEvents; i++) {
		DemoEventData ev;
		memset(&ev, 0, sizeof(DemoEventData));
		reader.read(&ev.header, sizeof(DemoEvent));
		float origin[3];
		float angles[3];
		float fparam1 = 0;
		float fparam2 = 0;
		memset(origin, 0, sizeof(float) * 3);
		memset(angles, 0, sizeof(float) * 3);

		if (ev.header.hasOrigin) {
			reader.read(&ev.origin[0], 3);
			reader.read(&ev.origin[1], 3);
			reader.read(&ev.origin[2], 3);
			for (int k = 0; k < 3; k++)
				origin[k] = FIXED_TO_FLOAT(ev.origin[k], 21, 3);
		}
		if (ev.header.hasAngles) {
			reader.read(&ev.angles[0], 2);
			reader.read(&ev.angles[1], 2);
			reader.read(&ev.angles[2], 2);
			for (int k = 0; k < 3; k++)
				angles[k] = ev.angles[k] / 8.0f;
		}
		if (ev.header.hasFparam1) {
			reader.read(&ev.fparam1, 3);
			fparam1 = ev.fparam1 / 128.0f;
		}
		if (ev.header.hasFparam2) {
			reader.read(&ev.fparam2, 3);
			fparam2 = ev.fparam2 / 128.0f;
		}
		if (ev.header.hasIparam1) {
			reader.read(&ev.iparam1, 2);
		}
		if (ev.header.hasIparam2) {
			reader.read(&ev.iparam2, 2);
		}

		uint16_t eidx = ev.header.entindex;
		convReplayEntIdx(eidx);
		if (eidx >= replayEnts.size()) {
			println("Invalid event edict %d", (int)ev.header.entindex);
			continue;
		}
		edict_t* ent = INDEXENT(eidx);

		/*
		println("PLAY EVT: %d %d %d %f (%.1f %.1f %.1f) (%.1f %.1f %.1f) %f %f %d %d %d %d",
			(int)ev.header.flags, eidx, (int)ev.header.eventindex, 0.0f,
			origin[0], origin[1], origin[2], angles[0], angles[1], angles[2],
			fparam1, fparam2, (int)ev.iparam1, (int)ev.iparam2,
			(int)ev.header.bparam1, (int)ev.header.bparam2);
		*/

		g_engfuncs.pfnPlaybackEvent(ev.header.flags, ent, ev.header.eventindex, 0.0f, origin, angles,
			fparam1, fparam2, ev.iparam1, ev.iparam2, ev.header.bparam1, ev.header.bparam2);
	}

	g_stats.eventCurrentSz = reader.tell() - startOffset;
	g_stats.eventCount += numEvents;

	return true;
}

bool DemoPlayer::readClientCommands(mstream& reader) {
	uint32_t startOffset = reader.tell();

	uint8_t numCommands;
	reader.read(&numCommands, 1);

	static char commandChars[MAX_CMD_LENGTH + 1];

	for (int i = 0; i < numCommands; i++) {
		DemoCommand cmd;
		reader.read(&cmd, sizeof(DemoCommand));

		if (cmd.len > MAX_CMD_LENGTH) {
			println("Invalid cmd len %d", (int)cmd.len);
			return false;
		}

		reader.read(commandChars, cmd.len);
		commandChars[cmd.len] = '\0';

		if (cmd.idx > demoHeader.maxPlayers) {
			println("Invalid command player %d / %d", (int)cmd.idx, (int)demoHeader.maxPlayers);
			continue;
		}
		if (cmd.idx == 0) {
			println("[SvenTV][Cmd][Server] %s", commandChars);
			continue;
		}

		DemoPlayerEnt& plr = fileplayerinfos[cmd.idx - 1];

		const char* legacySteamId = "STEAM_ID_INVALID";
		if (plr.steamid64 == 1ULL) {
			legacySteamId = "STEAM_ID_LAN";
		}
		else if (plr.steamid64 == 2ULL) {
			legacySteamId = "BOT";
		}
		else if (plr.steamid64 > 0ULL) {
			uint32_t accountIdLowBit = plr.steamid64 & 1;
			uint32_t accountIdHighBits = (plr.steamid64 >> 1) & 0x7FFFFFF;
			legacySteamId = UTIL_VarArgs("STEAM_0:%u:%u", accountIdLowBit, accountIdHighBits);
		}

		println("[SvenTV][Cmd][%s][%s] %s", legacySteamId, plr.name, commandChars);
	}

	g_stats.cmdCurrentSz = reader.tell() - startOffset;
	g_stats.cmdCount += numCommands;

	return true;
}

bool DemoPlayer::readDemoFrame() {
	uint32_t t = (getEpochMillis() - replayStartTime) * replaySpeed;

	fseek(replayFile, nextFrameOffset, SEEK_SET);

	DemoFrame header;
	if (!fread(&header, sizeof(DemoFrame), 1, replayFile)) {
		ClientPrintAll(HUD_PRINTTALK, "[SvenTV] Unexpected EOF\n");
		closeReplayFile();
		return false;
	}
	uint32_t demoTime = 0;
	uint32_t frameSize = 0;
	uint32_t headerSz = sizeof(DemoFrame);
	if (header.isGiantFrame) {
		fread(&demoTime, sizeof(uint32_t), 1, replayFile);
		fread(&frameSize, sizeof(uint32_t), 1, replayFile);
		headerSz += 8;
	}
	else if (header.isBigFrame) {
		fread(&demoTime, sizeof(uint8_t), 1, replayFile);
		fread(&frameSize, sizeof(uint16_t), 1, replayFile);
		demoTime = lastFrameDemoTime + demoTime;
		headerSz += 3;
	}
	else {
		fread(&demoTime, sizeof(uint8_t), 1, replayFile);
		fread(&frameSize, sizeof(uint8_t), 1, replayFile);
		demoTime = lastFrameDemoTime + demoTime;
		headerSz += 2;
	}

	frameProgress = 1.0f;
	if (demoTime > lastFrameDemoTime) {
		frameProgress = 1.0f - ((demoTime - t) / (float)(demoTime - lastFrameDemoTime));
	}

	if (demoTime > t) {
		//println("Wait %u > %u", header.demoTime, t);
		interpolateEdicts();
		return false;
	}

	g_stats.bigFrameCount += header.isBigFrame;
	g_stats.giantFrameCount += header.isGiantFrame;
	lastFrameDemoTime = demoTime;

	if (frameSize > 1024 * 1024 * 32 || frameSize == 0) {
		ClientPrintAll(HUD_PRINTTALK, "[SvenTV] Invalid frame size\n");
		closeReplayFile();
		return false;
	}
	nextFrameOffset += frameSize;
	frameSize -= headerSz;

	if (frameSize == 0) {
		g_stats.incTotals();
		return true; // nothing changed
	}

	//println("Frame %d (%.1f kb), Time: %.1f", replayFrame, header.frameSize / 1024.0f, (float)TimeDifference(0, header.demoTime));

	/*
	if (replayFrame == 242) {
		println("debug");
	}
	*/

	char* frameData = new char[frameSize];
	if (!fread(frameData, frameSize, 1, replayFile)) {
		delete[] frameData;
		ClientPrintAll(HUD_PRINTTALK, "[SvenTV] Unexpected EOF\n");
		closeReplayFile();
		return false;
	}

	mstream reader = mstream(frameData, frameSize);

	if (header.isKeyFrame) {
		memset(fileedicts, 0, MAX_EDICTS * sizeof(netedict));
		memset(fileplayerinfos, 0, 32 * sizeof(DemoPlayerEnt));
	}

	g_stats.entDeltaCurrentSz = g_stats.plrDeltaCurrentSz 
		= g_stats.msgCurrentSz = g_stats.cmdCurrentSz = g_stats.eventCurrentSz = 0;

	for (int i = 0; i < MAX_EDICTS; i++) {
		fileedicts[i].deltaBitsLast = 0;
	}

	if (header.hasEntityDeltas && (!readEntDeltas(reader))) {
		delete[] frameData;
		return false;
	}

	if (header.hasPlayerDeltas && !readPlayerDeltas(reader)) {
		delete[] frameData;
		return false;
	}
	if (header.hasNetworkMessages && !readNetworkMessages(reader)) {
		delete[] frameData;
		return false;
	}
	if (header.hasEvents && !readEvents(reader)) {
		delete[] frameData;
		return false;
	}
	if (header.hasCommands && !readClientCommands(reader)) {
		delete[] frameData;
		return false;
	}
	if (!simulate(header)) {
		delete[] frameData;
		return false;
	}

	g_stats.incTotals();

	replayFrame++;

	memcpy(&lastReplayFrame, &header, sizeof(DemoFrame));
	delete[] frameData;

	return true;
}

inline Vector lerp(Vector start, Vector end, float t) {
	Vector out;
	out[0] = start[0] + (end[0] - start[0]) * t;
	out[1] = start[1] + (end[1] - start[1]) * t;
	out[2] = start[2] + (end[2] - start[2]) * t;
	return out;
}

inline float lerp(float start, float end, float t) {
	return start + (end - start) * t;
}

inline int anglelerp16(int start, int end, float t) {
	// 65536 = 360 deg
	int shortest_angle = ((((end - start) % 65536) + 98304) % 65536) - 32768;

	return start + shortest_angle * t;
}

inline int anglelerp(int start, int end, float t) {
	int shortest_angle = ((((end - start) % 360) + 540) % 360) - 180;

	return start + shortest_angle * t;
}

void DemoPlayer::interpolateEdicts() {
	static uint64_t lastTime = 0;
	uint64_t now = getEpochMillis();
	float dt = TimeDifference(lastTime, now);
	lastTime = now;

	for (int i = 0; i < replayEnts.size(); i++) {
		if (!fileedicts[i].edflags) {
			continue;
		}

		if (!replayEnts[i].h_ent.IsValid() || replayEnts[i].h_ent.GetEdict()->v.effects & EF_NODRAW) {
			continue;
		}

		edict_t* ent = replayEnts[i].h_ent;
		InterpInfo& interp = replayEnts[i].interp;

		if (fileedicts[i].edflags & (EDFLAG_MONSTER | EDFLAG_PLAYER)) {
			float animTime = (gpGlobals->time - interp.animTime) * replaySpeed;
			float inc = animTime * interp.framerateEnt * interp.framerateSmd;

			ent->v.frame = interp.frameEnd + inc;
			//println("ANIM TIME %.2f %.2f %.2f", (gpGlobals->time - interp.lastMovementTime) * replaySpeed, t, interp.estimatedUpdateDelay);

			if (interp.sequenceLoops)
				ent->v.frame = normalizeRangef(ent->v.frame, 0, 255);
			else
				ent->v.frame = clampf(ent->v.frame, 0, 255);

			interp.interpFrame = ent->v.frame;
		}

		if ((fileedicts[i].edflags & EDFLAG_MONSTER)) {
			float t = 1;
			if (interp.sequenceEnd == ent->v.sequence && interp.estimatedUpdateDelay > 0) {
				float deltaTime = (gpGlobals->time - interp.lastMovementTime) * replaySpeed;
				t = clampf(deltaTime / interp.estimatedUpdateDelay, 0, 1);
			}

			ent->v.origin = lerp(interp.originStart, interp.originEnd, t);

			ent->v.angles[0] = anglelerp(interp.anglesStart[0], interp.anglesEnd[0], t);
			ent->v.angles[1] = anglelerp(interp.anglesStart[1], interp.anglesEnd[1], t);
			ent->v.angles[2] = anglelerp(interp.anglesStart[2], interp.anglesEnd[2], t);
		}
		else {
			ent->v.origin = lerp(interp.originStart, interp.originEnd, frameProgress);

			ent->v.angles[0] = anglelerp(interp.anglesStart[0], interp.anglesEnd[0], frameProgress);
			ent->v.angles[1] = anglelerp(interp.anglesStart[1], interp.anglesEnd[1], frameProgress);
			//ent->v.angles[2] = anglelerp(interp.anglesStart[2], interp.anglesEnd[1], frameProgress);

			// fixes hud info
			g_engfuncs.pfnSetOrigin(ent, ent->v.origin);

			if (fileedicts[i].edflags & EDFLAG_PLAYER) {
				if ((ent->v.flags & FL_CLIENT) == 0) {
					updatePlayerModelGait(ent, dt); // manual gait calculations for non-player entity
					updatePlayerModelPitchBlend(ent);
				}
				else {
					// bot code sets gait/blends automatically
					ent->v.angles.x = normalizeRangef(ent->v.angles.x, -180.0f, 180.0f) * 0.5f;
				}
			}
		}
	}
}

void DemoPlayer::updatePlayerModelPitchBlend(edict_t* ent) {
	float pitch = normalizeRangef(-ent->v.angles.x, -180.0f, 180.0f);

	if (pitch > 35) {
		ent->v.angles.x = (pitch - 35) * 0.5f;
	}
	else if (pitch < -45) {
		ent->v.angles.x = (pitch - -45) * 0.5f;
	}
	else {
		ent->v.angles.x = 0;
	}

	uint8_t blend = 127 + (pitch * 1.4f);
	ent->v.blending[0] = blend;
}

void DemoPlayer::updatePlayerModelGait(edict_t* ent, float dt) {
	// TODO: calculate demoFileFps
	Vector gaitspeed = Vector(ent->v.velocity[0], ent->v.velocity[1], 0) * demoFileFps;
	float& gaityaw = ent->v.fuser4;
	float& yaw = ent->v.angles.y;

	float dtScale = 1.0f / dt;
	const float PI = 3.1415f;

	// gait calculations from the HLSDK
	if (gaitspeed.Length() < 5)
	{
		// standing still. Rotate legs back to forward position
		float flYawDiff = yaw - gaityaw;
		flYawDiff = flYawDiff - (int)(flYawDiff / 360) * 360;
		if (flYawDiff > 180)
			flYawDiff -= 360;
		if (flYawDiff < -180)
			flYawDiff += 360;

		if (dt < 0.25)
			flYawDiff *= dt * 4;
		else
			flYawDiff *= dt;

		gaityaw += flYawDiff;
		gaityaw -= (int)(gaityaw / 360) * 360;
	}
	else
	{
		// moving. rotate legs towards movement direction
		Vector doot = gaitspeed.Normalize();
		gaityaw = (atan2(gaitspeed.y, gaitspeed.x) * 180 / PI);
		if (gaityaw > 180)
			gaityaw = 180;
		if (gaityaw < -180)
			gaityaw = -180;
	}

	float flYaw = yaw - gaityaw;
	flYaw = flYaw - (int)(flYaw / 360) * 360;
	if (flYaw < -180)
		flYaw = flYaw + 360;
	if (flYaw > 180)
		flYaw = flYaw - 360;

	if (flYaw > 120)
	{
		gaityaw = gaityaw - 180;
		flYaw = flYaw - 180;
	}
	else if (flYaw < -120)
	{
		gaityaw = gaityaw + 180;
		flYaw = flYaw + 180;
	}

	// adjust torso
	uint8_t torso = ((flYaw / 4.0) + 30) / (60.0 / 255.0);
	memset(ent->v.controller, torso, 4);
	ent->v.angles.y = gaityaw;
	
	if (ent->v.gaitsequence == 0) {
		ent->v.gaitsequence = ent->v.sequence;
	}

	static uint8_t crouching_anims[256] = {
		0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, // 0-15
		0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 1, 1, 1, 0, // 16-31
		0, 0, 0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 1, 1, 1, 0, // 32-47
		0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, // 48-63
		0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 1, 1, 1, 0, 0, 0, // 64-79
		0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, // 80-95
		1, 1, 1, 0, 0, 0, 1, 1, 1, 0, 0, 0, 1, 1, 1, 0, // 96-111
		0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 1, // 112-127
		1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, // 128-143
		1, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, // 144-159
		0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, // 160-175
		1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 176-191
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 192-207
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 208-223
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 224-239
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 240-255
	};

	if (crouching_anims[clamp(ent->v.gaitsequence, 0, 255)]) {
		ent->v.origin.z += 18;
	}
}

void DemoPlayer::playDemo() {
	if (!replayFile) {
		return;
	}

	while (readDemoFrame());
}