#include "main.h"
#include "DemoPlayer.h"
#include "SvenTV.h"
#include "pm_shared.h"

//const char* model_entity = "env_sprite";
const char* model_entity = "cycler";

set<uint16_t> unknownSpam; // don't show repeat errors

void DemoDataStream::seek(uint64_t to) {
	if (fileStream) {
		fseek(fileStream, to, SEEK_SET);
	}
	else {
		memoryStream.seek(to);
	}
}

bool DemoDataStream::valid() {
	return fileStream || memoryStream.getBuffer();
}

size_t DemoDataStream::read(void* dst, size_t sz) {
	if (fileStream) {
		return fread(dst, sz, 1, fileStream);
	}
	else {
		return memoryStream.read(dst, sz);
	}
}

size_t DemoDataStream::tell() {
	if (fileStream) {
		return ftell(fileStream);
	}
	else {
		return memoryStream.tell();
	}
}

void DemoDataStream::close() {
	if (fileStream) {
		fclose(fileStream);
		fileStream = NULL;
	}
	else  if (memoryStream.getBuffer()) {
		memoryStream.freeBuf();
	}
}

#ifdef HLCOOP_BUILD
#include "animation.h"

void sendScoreInfo(int idx, float score, long deaths, float health, float armor, int classification, short ping, short icon) {
	// TODO
}

void sendScoreInfo(edict_t* ent) {
	// TODO
}

#else
void sendScoreInfo(int idx, float score, long deaths, float health, float armor, int classification, short ping, short icon) {
	// TODO
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
#endif

void delayRenamePlayer(EHANDLE h_plr, string name) {
	edict_t* ent = h_plr.GetEdict();
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

void show_message(edict_t* plr, int mode, const char* msg) {
	if (plr)
		UTIL_ClientPrint(plr, mode, msg);
	else
		g_engfuncs.pfnServerPrint(msg);
}

bool DemoPlayer::openDemo(edict_t* plr, string path, float offsetSeconds, bool skipPrecache) {
	this->offsetSeconds = offsetSeconds;
	stopReplay();
	unknownSpam.clear();

	replayData = new DemoDataStream(fopen(path.c_str(), "rb"));

	memset(&g_stats, 0, sizeof(DemoStats));

	if (!replayData->valid()) {
		show_message(plr, HUD_PRINTTALK, UTIL_VarArgs("Failed to open demo file: %s\n", path.c_str()));
		return false;
	}

	if (!replayData->read(&demoHeader, sizeof(DemoHeader))) {
		show_message(plr, HUD_PRINTTALK, "Invalid demo file: EOF before header read\n");
		closeReplayFile();
		return false;
	}

	if (demoHeader.version != DEMO_VERSION) {
		show_message(plr, HUD_PRINTTALK, UTIL_VarArgs("Invalid demo version: %d (expected %d)\n", demoHeader.version, DEMO_VERSION));
		closeReplayFile();
		return false;
	}

	string mapname = string(demoHeader.mapname, 64);
	if (!g_engfuncs.pfnIsMapValid((char*)mapname.c_str())) {
		show_message(plr, HUD_PRINTTALK, UTIL_VarArgs("Invalid demo map: %s\n", mapname.c_str()));
		closeReplayFile();
		return false;
	}

	if (demoHeader.modelLen > 1024 * 1024 * 32 || demoHeader.soundLen > 1024 * 1024 * 32) {
		show_message(plr, HUD_PRINTTALK, UTIL_VarArgs("Invalid demo file: %u + %u byte model/sound data (too big)\n", demoHeader.modelLen, demoHeader.soundLen));
		closeReplayFile();
		return false;
	}

	if (demoHeader.modelLen) {
		uint16_t* modelIndexes = new uint16_t[demoHeader.modelCount];
		char* modelData = new char[demoHeader.modelLen];

		if (!replayData->read(modelIndexes, demoHeader.modelCount*sizeof(uint16_t))) {
			show_message(plr, HUD_PRINTTALK, "Invalid demo file: incomplete model data\n");
			closeReplayFile();
			return false;
		}
		if (!replayData->read(modelData, demoHeader.modelLen)) {
			show_message(plr, HUD_PRINTTALK, "Invalid demo file: incomplete model data\n");
			closeReplayFile();
			return false;
		}

		precacheModels = splitString(string(modelData, demoHeader.modelLen), "\n");

		for (int i = 0; i < (int)precacheModels.size(); i++) {
			replayModelPath[modelIndexes[i]] = precacheModels[i];
			//println("Replay model %d: %s", replayIdx, precacheModels[i].c_str());
		}

		delete[] modelData;
		delete[] modelIndexes;
	}
	if (demoHeader.soundLen) {
		uint16_t* soundIndexes = new uint16_t[demoHeader.soundCount];
		char* soundData = new char[demoHeader.soundLen];

		if (!replayData->read(soundIndexes, demoHeader.soundCount*sizeof(uint16_t))) {
			show_message(plr, HUD_PRINTTALK, "Invalid demo file: incomplete sound data\n");
			closeReplayFile();
			return false;
		}
		if (!replayData->read(soundData, demoHeader.soundLen)) {
			show_message(plr, HUD_PRINTTALK, "Invalid demo file: incomplete sound data\n");
			closeReplayFile();
			return false;
		}

		precacheSounds = splitString(string(soundData, demoHeader.soundLen), "\n");

		for (int i = 0; i < (int)precacheSounds.size(); i++) {
			replaySoundPath[soundIndexes[i]] = precacheSounds[i];
		}

		delete[] soundData;
		delete[] soundIndexes;
	}
	if (demoHeader.modelLen == 0) {
		println("WARNING: Demo has no model list. The plugin may have been reloaded before the demo started.");
	}

	show_message(plr, HUD_PRINTCONSOLE, UTIL_VarArgs("\nfile       : %s\n", path.c_str()));
	{
		time_t rawtime;
		struct tm* timeinfo;
		char buffer[80];

		time(&rawtime);
		timeinfo = localtime(&rawtime);

		strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
		show_message(plr, HUD_PRINTCONSOLE, UTIL_VarArgs("date       : %s\n", buffer));
	}
	{
		int duration = demoHeader.endTime ? TimeDifference(demoHeader.startTime, demoHeader.endTime) : 0;
		int hours = duration / (60 * 60);
		int minutes = (duration - (hours * 60 * 60)) / 60;
		int seconds = duration % 60;

		if (duration) {
			show_message(plr, HUD_PRINTCONSOLE, UTIL_VarArgs("duration   : %02d:%02d:%02d\n", hours, minutes, seconds));
		}
		else {
			show_message(plr, HUD_PRINTCONSOLE, "duration   : unknown\n");
		}
	}
	show_message(plr, HUD_PRINTCONSOLE, UTIL_VarArgs("map        : %s\n", mapname.c_str()));
	show_message(plr, HUD_PRINTCONSOLE, UTIL_VarArgs("maxplayers : %d\n", demoHeader.maxPlayers));
	show_message(plr, HUD_PRINTCONSOLE, UTIL_VarArgs("models     : %d\n", precacheModels.size()));
	show_message(plr, HUD_PRINTCONSOLE, UTIL_VarArgs("sounds     : %d\n", precacheSounds.size()));

	UTIL_ClientPrintAll(HUD_PRINTTALK, UTIL_VarArgs("[HLTV] Opened: %s\n", path.c_str()));

	g_stats.totalWriteSz = sizeof(DemoHeader) + demoHeader.modelLen + demoHeader.soundLen;

	if (skipPrecache) {
		prepareDemo();
		return true;
	}

	if (toLowerCase(STRING(gpGlobals->mapname)) != toLowerCase(mapname)) {
		g_engfuncs.pfnServerCommand(UTIL_VarArgs("changelevel %s\n", mapname.c_str()));
		g_engfuncs.pfnServerExecute();
	}

	return true;
}

void DemoPlayer::prepareDemo() {
	if (clearMapForPlayback) {
		for (int i = 1; i <= gpGlobals->maxClients; i++) {
			edict_t* ent = INDEXENT(i);
			CBasePlayer* plr = (CBasePlayer*)GET_PRIVATE(ent);
			if (IsValidPlayer(ent) && plr) {
				// hide weapons
				//ent->v.viewmodel = 0;
				//ent->v.weaponmodel = 0;
				//plr->m_iHideHUD = 1;
				plr->RemoveAllItems(FALSE);
			}
		}
		for (int i = gpGlobals->maxClients; i < gpGlobals->maxEntities; i++) {
			edict_t* ent = INDEXENT(i);

			// kill everything that isn't attached to a player
			//if (ent && strlen(STRING(ent->v.classname)) > 0 && (!ent->v.aiment || (ent->v.aiment->v.flags & FL_CLIENT) == 0)) {
			if (ent && strlen(STRING(ent->v.classname)) > 0) {
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

	MESSAGE_BEGIN(MSG_ALL, SVC_STUFFTEXT);
	WRITE_STRING("stopsound\n");
	MESSAGE_END();
	UTIL_StopGlobalMp3();

	// don't show leave/join messages for bots in the hlcoop plugin
	CVAR_SET_FLOAT("mp_leavejoin_msg", 1);

	replayStartTime = getEpochMillis() - (uint64_t)(offsetSeconds * 1000);
	replayFrame = 0;
	nextFrameOffset = replayData ? replayData->tell() : 0;
	nextFrameTime = 0;
}

void DemoPlayer::precacheDemo() {
	if (!replayData) {
		return;
	}

	for (int i = 0; i < (int)precacheModels.size(); i++) {
		PRECACHE_MODEL(precacheModels[i].c_str());
	}
	for (int i = 0; i < (int)precacheSounds.size(); i++) {
		PRECACHE_SOUND_NULLENT(precacheSounds[i].c_str());
	}
}

void DemoPlayer::stopReplay() {
	closeReplayFile();
	precacheModels.clear();
	precacheSounds.clear();
	replayModelPath.clear();
	replayEnts.clear();
}

bool DemoPlayer::isPlaying() {
	return replayData;
}

void DemoPlayer::closeReplayFile() {
	if (isValidating) {
		return;
	}
	if (replayData) {
		replayData->close();
		delete replayData;
		replayData = NULL;

		MESSAGE_BEGIN(MSG_ALL, SVC_STUFFTEXT);
		WRITE_STRING("stopsound\n");
		MESSAGE_END();
		UTIL_StopGlobalMp3();

		// allow leave/join messages for bots in the hlcoop plugin
		CVAR_SET_FLOAT("mp_leavejoin_msg", 2);
	}

	for (int i = 0; i < (int)replayEnts.size(); i++) {
		edict_t* ent = replayEnts[i].h_ent.GetEdict();
		if (!ent) {
			continue;
		}

		if (ent->v.flags & FL_FAKECLIENT) {
			KickPlayer(ent);
		}
		else {
			REMOVE_ENTITY(replayEnts[i].h_ent.GetEdict());
		}			
	}
	replayEnts.clear();

	for (int i = 1; i < gpGlobals->maxClients; i++) {
		edict_t* ent = INDEXENT(i);
		if (!IsValidPlayer(ent) || (ent->v.flags & FL_FAKECLIENT)) {
			continue;
		}

		
		if (oldPlayerNames->size()) {
			// remove (1) prefix if it was added during the demo
			char* infoBuffer = g_engfuncs.pfnGetInfoKeyBuffer(ent);
			g_engfuncs.pfnSetClientKeyValue(i, infoBuffer, "name", (char*)oldPlayerNames[i - 1].c_str());
		}
	}
}

bool DemoPlayer::readEntDeltas(mstream& reader, DemoDataTest* validate) {
	uint32_t startOffset = reader.tell();

	uint16_t numEntDeltas = 0;
	reader.read(&numEntDeltas, 2);

	if (validate) validate->numEntDeltas = numEntDeltas;

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

		netedict temp;
		memcpy(&temp, &fileedicts[fullIndex], sizeof(netedict));

		netedict* ed = &fileedicts[fullIndex];
		if (!ed->readDeltas(reader)) {
			//println("Skip free %d", fullIndex);
			continue;
		}

		if (validate) {
			int szRead = reader.tell() - readOffset;
			netedict& expectedEd = validate->newEntState[fullIndex];

			if (!expectedEd.matches(*ed)) {
				ALERT(at_console, "Read unexpected ent state for %d!\n", (int)fullIndex);
				// now debug it
				expectedEd.matches(*ed);

				memcpy(ed, &temp, sizeof(netedict));
				reader.seek(startPos);
				ed->readDeltas(reader);

				mstream testStream(1024 * 512); // no delta should be this big, being safe
				validate->newEntState[fullIndex].writeDeltas(testStream, validate->oldEntState[fullIndex]);
				testStream.freeBuf();
				
				return false;
			}
			if (validate->entDeltaSz[fullIndex] != szRead) {
				ALERT(at_console, "Read unexpected ent delta for %d! %d != %d\n",
					(int)fullIndex, validate->entDeltaSz[fullIndex], szRead);

				// now debug it
				memcpy(ed, &temp, sizeof(netedict));
				reader.seek(startPos);
				ed->readDeltas(reader);
				return false;
			}
			
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
			if (!IsValidPlayer(ent) || (ent->v.flags & FL_FAKECLIENT)) {
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
			REMOVE_ENTITY(replayEnts[i].h_ent.GetEdict());
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
			bot->v.flags |= FL_FAKECLIENT;

			CBasePlayer* plr = (CBasePlayer*)CBaseEntity::Instance(bot);
			plr->m_isObserver = false;
			plr->UpdateTeamInfo();

			replayEnts[i].h_ent = ent = bot;
		}
		else {
			println("Failed to create bot");
		}
	}
	else if ((fileedicts[i].edflags & EDFLAG_BEAM) && (ent->v.flags & (FL_MONSTER | FL_CLIENT))) {
		unordered_map<string, string> keys;
		CBaseEntity* newEnt = CBaseEntity::Create("beam", g_vecZero, g_vecZero, NULL, keys);

		if (ent->v.flags & FL_CLIENT) {
			KickPlayer(ent);
		}
		else {
			REMOVE_ENTITY(replayEnts[i].h_ent.GetEdict());
		}

		replayEnts[i].h_ent = ent = newEnt->edict();
		ent->v.flags |= FL_CUSTOMENTITY;
		ent->v.effects |= EF_NODRAW;
		SET_MODEL(ent, NOT_PRECACHED_MODEL);
	}
	else if (!(fileedicts[i].edflags & EDFLAG_BEAM) && (ent->v.flags & (FL_MONSTER | FL_CLIENT)) == 0) {
		unordered_map<string, string> keys;
		keys["model"] = NOT_PRECACHED_MODEL;

		CBaseEntity* newEnt = CBaseEntity::Create(model_entity, g_vecZero, g_vecZero, NULL, keys);

		if (ent->v.flags & FL_CLIENT) {
			KickPlayer(ent);
		}
		else {
			REMOVE_ENTITY(replayEnts[i].h_ent.GetEdict());
		}

		replayEnts[i].h_ent = ent = newEnt->edict();
		ent->v.solid = SOLID_NOT;
		ent->v.effects |= EF_NODRAW;
		ent->v.movetype = MOVETYPE_NONE;
		ent->v.flags |= FL_MONSTER;
	}

	ent->v.iuser4 = i; // hack to store original ent index
	return ent;
}

bool DemoPlayer::createReplayEntities(int count) {
	while (count >= (int)replayEnts.size()) {
		unordered_map<string, string> keys;
		keys["model"] = NOT_PRECACHED_MODEL;

		CBaseEntity* ent = CBaseEntity::Create(model_entity, g_vecZero, g_vecZero, NULL, keys);
		ent->pev->solid = SOLID_NOT;
		ent->pev->effects |= EF_NODRAW;
		ent->pev->movetype = MOVETYPE_NONE;
		ent->pev->flags |= FL_MONSTER;

		if (!ent) {
			println("Entity overflow at %d\n", (int)replayEnts.size());
			for (int i = 0; i < (int)replayEnts.size(); i++) {
				REMOVE_ENTITY(replayEnts[i].h_ent.GetEdict());
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
				CBaseAnimating* anim = CBaseEntity::Instance(ent)->MyAnimatingPointer();

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
			}

			interp.sequenceStart = interp.sequenceEnd;
			interp.sequenceEnd = ent->v.sequence;
		}
	}

	interp.framerateEnt = ent->v.framerate;
	ent->v.framerate = 0.000001f; // prevent the game trying to interpolate
}

edict_t* DemoPlayer::getReplayEntity(int idx) {
	if (idx < (int)replayEnts.size()) {
		return replayEnts[idx].h_ent.GetEdict();
	}
	return NULL;
}

bool DemoPlayer::simulate(DemoFrame& header) {
	int errorSprIdx = g_engfuncs.pfnModelIndex(NOT_PRECACHED_MODEL);

	for (int i = 1; i < MAX_EDICTS; i++) {
		if (!fileedicts[i].edflags) {
			if (i < (int)replayEnts.size()) {
				replayEnts[i].h_ent.GetEdict()->v.effects |= EF_NODRAW;
			}
			continue;
		}

		// create ents until the client has enough to handle this demo frame
		if (!createReplayEntities(i)) {
			return false;
		}

		edict_t* ent = replayEnts[i].h_ent.GetEdict();
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
	if (replayModelPath.count(modelIdx)) {
		// Demo file model path
		return replayModelPath[modelIdx];
	}
	else {
		if (!unknownSpam.count(modelIdx)) {
			println("Unknown model idx %d", modelIdx);
			unknownSpam.insert(modelIdx);
		}

		return NOT_PRECACHED_MODEL;
	}
}

void DemoPlayer::convReplayEntIdx(byte* dat, int offset, int dataSz) {
	if (offset >= dataSz) {
		ALERT(at_console, "Tried to write past end of message\n", 0);
		return;
	}

	uint16_t* eidx = (uint16_t*)(dat + offset);
	if (*eidx >= replayEnts.size()) {
		//println("Invalid replay ent idx %d", (int)eidx);
		*eidx = 0;
		return;
	}
	*eidx = ENTINDEX(replayEnts[*eidx].h_ent.GetEdict());

	if (*eidx >= sv_max_client_edicts->value) {
		ALERT(at_console, "Invalid client msg edict idx %d (max %d)\n", (int)*eidx, (int)sv_max_client_edicts->value);
	}
}

void DemoPlayer::convReplayModelIdx(byte* dat, int offset, int dataSz) {
	if (offset >= dataSz) {
		ALERT(at_console, "Tried to write past end of message\n", 0);
		return;
	}

	uint16_t* modelIdx = (uint16_t*)(dat + offset);
	*modelIdx = g_engfuncs.pfnModelIndex(getReplayModel(*modelIdx).c_str());
}

bool DemoPlayer::convReplaySoundIdx(uint16_t& soundIdx) {
	if (replaySoundPath.find(soundIdx) == replaySoundPath.end()) {
		println("Invalid replay sound index %d", soundIdx);
		soundIdx = 0;
		return false;
	}

	soundIdx = SOUND_INDEX(replaySoundPath[soundIdx].c_str());
	return true;
}

bool DemoPlayer::readPlayerDeltas(mstream& reader, DemoDataTest* validate) {
	uint32_t startOffset = reader.tell();

	uint32_t includedPlayers = 0;
	reader.read(&includedPlayers, 4);

	if (validate) validate->playerDeltaBits = includedPlayers;

	int numPlayerDeltas = 0;
	for (int i = 0; i < gpGlobals->maxClients; i++) {
		if ((includedPlayers & (1 << i)) == 0) {
			continue;
		}

		uint32_t deltas = fileplayerinfos[i].readDeltas(reader);
		numPlayerDeltas++;

		if (!validate && i+1 < (int)replayEnts.size()) {
			edict_t* bot = replayEnts[i+1].h_ent.GetEdict();
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

bool DemoPlayer::processTempEntityMessage(NetMessageData& msg, DemoDataTest* validate) {
	uint8_t type = msg.data[0];

	byte* args = msg.data + 1;
	uint16_t* args16 = (uint16_t*)args;
	//float* fargs = (float*)args;
	int dataSz = msg.sz - 1;

	if (type > TE_USERTRACER) {
		println("Invalid temp ent type %d", (int)type);
		return false;
	}

	const int BYTEsz = 1;
	//const int CHARsz = 1;
	const int ANGLsz = 1;
	const int EIDXsz = 2;
	const int SHORTsz = 2;
	//const int LONGsz = 4;

#ifdef HLCOOP_BUILD
	const int COORDsz = 2;
#else
	const int COORDsz = 4; // sven coords are 4 bytes for larger maps
#endif

	// TODO: use sz consts and get rid of this ifdef

#ifdef HLCOOP_BUILD
	static uint8_t expectedSzLookup[TE_USERTRACER+1] = {
		25, // TE_BEAMPOINTS
		9, // TE_BEAMENTPOINT
		10, // TE_GUNSHOT
		12, // TE_EXPLOSION
		7, // TE_TAREXPLOSION
		11, // TE_SMOKE
		13, // TE_TRACER
		18, // TE_LIGHTNING
		17, // TE_BEAMENTS
		7, // TE_SPARKS
		7, // TE_LAVASPLASH
		7, // TE_TELEPORT
		9, // TE_EXPLOSION2
		0, // TE_BSPDECAL (13 or 11!!!)
		10, // TE_IMPLOSION
		20, // TE_SPRITETRAIL
		0, // unused index
		11, // TE_SPRITE
		17, // TE_BEAMSPRITE
		25, // TE_BEAMTORUS
		25, // TE_BEAMDISK
		25, // TE_BEAMCYLINDER
		11, // TE_BEAMFOLLOW
		12, // TE_GLOWSPRITE
		17, // TE_BEAMRING
		20, // TE_STREAK_SPLASH
		0, // unused index
		13, // TE_DLIGHT
		17, // TE_ELIGHT
		0, // TE_TEXTMESSAGE (depends on text/effect)
		18, // TE_LINE
		18, // TE_BOX

		// unused indexes
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,

		3, // TE_KILLBEAM
		11, // TE_LARGEFUNNEL
		15, // TE_BLOODSTREAM
		13, // TE_SHOWLINE
		15, // TE_BLOOD
		10, // TE_DECAL
		6, // TE_FIZZ
		18, // TE_MODEL
		14, // TE_EXPLODEMODEL
		25, // TE_BREAKMODEL
		10, // TE_GUNSHOTDECAL
		18, // TE_SPRITE_SPRAY
		8, // TE_ARMOR_RICOCHET
		11, // TE_PLAYERDECAL
		20, // TE_BUBBLES
		20, // TE_BUBBLETRAIL
		13, // TE_BLOODSPRITE
		8, // TE_WORLDDECAL
		8, // TE_WORLDDECALHIGH
		10, // TE_DECALHIGH
		17, // TE_PROJECTILE
		19, // TE_SPRAY
		7, // TE_PLAYERSPRITES
		11, // TE_PARTICLEBURST
		14, // TE_FIREFIELD
		8, // TE_PLAYERATTACHMENT
		2, // TE_KILLPLAYERATTACHMENTS
		19, // TE_MULTIGUNSHOT
		16 // TE_USERTRACER		
	};
#else
	static uint8_t expectedSzLookup[TE_USERTRACER + 1] = {
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
#endif

	int expectedSz = expectedSzLookup[type];

	if (type == TE_BSPDECAL) {
#ifdef HLCOOP_BUILD
		expectedSz = args16[7] ? 13 : 11;
#else
		expectedSz = args16[7] ? 19 : 17;
#endif
	}
	if (type != TE_TEXTMESSAGE && msg.sz != expectedSz) {
		println("Bad size for %s (%d): %d != %d", te_names[type], (int)type, (int)msg.sz, expectedSz);
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
		convReplayModelIdx(args, COORDsz*6, dataSz);
		//args16[12] = g_engfuncs.pfnModelIndex("sprites/laserbeam.spr");
		break;
	case TE_BEAMENTPOINT:
		convReplayEntIdx(args, 0, dataSz);
		convReplayModelIdx(args, EIDXsz + COORDsz*3, dataSz);
		break;
	case TE_KILLBEAM:
		convReplayEntIdx(args, 0, dataSz);
		break;
	case TE_BEAMENTS:
	case TE_BEAMRING:
		convReplayEntIdx(args, 0, dataSz);
		convReplayEntIdx(args, EIDXsz, dataSz);
		convReplayModelIdx(args, EIDXsz*2, dataSz);
		break;
	case TE_LIGHTNING: {
		convReplayModelIdx(args, COORDsz*6 + BYTEsz*3, dataSz);
		break;
	}
	case TE_BEAMSPRITE:
		convReplayModelIdx(args, COORDsz*6, dataSz);
		convReplayModelIdx(args, COORDsz*6 + SHORTsz, dataSz);
		break;
	case TE_BEAMFOLLOW:
	case TE_FIZZ:
		convReplayEntIdx(args, 0, dataSz);
		convReplayModelIdx(args, EIDXsz, dataSz);
		break;
	case TE_PLAYERSPRITES: {
		convReplayEntIdx(args, 0, dataSz);
		edict_t* plr = INDEXENT(args16[0]);
		if (!plr || !(plr->v.flags & FL_CLIENT)) {
			args16[0] = 1; // prevent fatal error
		}
		convReplayModelIdx(args, EIDXsz, dataSz);
		break;
	}
	case TE_EXPLOSION:
	case TE_SMOKE:
	case TE_SPRITE:
	case TE_GLOWSPRITE:
	case TE_LARGEFUNNEL:
		convReplayModelIdx(args, COORDsz*3, dataSz);
		break;
	case TE_PLAYERATTACHMENT: {
		uint16_t entIdx = args[0];
		convReplayEntIdx((byte*)&entIdx, 0, dataSz);
		convReplayModelIdx(args, BYTEsz + COORDsz, dataSz);
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
		convReplayEntIdx((byte*)&entIdx, 0, dataSz);
		edict_t* plr = INDEXENT(entIdx);
		if (!plr || !(plr->v.flags & FL_CLIENT)) {
			entIdx = 1; // prevent fatal error
		}
		args[0] = (uint8_t)entIdx;
		break;
	}
	case TE_BUBBLETRAIL:
	case TE_BUBBLES:
		convReplayModelIdx(args, COORDsz*7, dataSz);
		break;
	case TE_BLOODSPRITE:
		convReplayModelIdx(args, COORDsz*3, dataSz);
		convReplayModelIdx(args, COORDsz*3 + SHORTsz, dataSz);
		break;
	case TE_FIREFIELD:
		convReplayModelIdx(args, COORDsz*3 + SHORTsz, dataSz);
		break;
	case TE_EXPLODEMODEL:
		convReplayModelIdx(args, COORDsz*4, dataSz);
		break;
	case TE_MODEL:
		convReplayModelIdx(args, COORDsz*6 + ANGLsz, dataSz);
		break;
	case TE_BREAKMODEL:
		convReplayModelIdx(args, COORDsz*9 + BYTEsz, dataSz);
		break;
	case TE_DECAL:
	case TE_DECALHIGH:
		convReplayModelIdx(args, COORDsz*3 + BYTEsz, dataSz);
		break;
	case TE_GUNSHOTDECAL:
		convReplayModelIdx(args, COORDsz*3, dataSz);
		break;
	case TE_BSPDECAL: {
		uint16_t* entIdx = (uint16_t*)(args + COORDsz * 3 + SHORTsz);
		if (*entIdx) { // not world entity?
			convReplayEntIdx(args, COORDsz*3 + SHORTsz, dataSz);
			//convReplayModelIdx(args16[5]); // (BSP model idx in demo should always match the game)
		}
		break;
	}
	default:
		break;
	}

	//println("Play temp ent %d", (int)type);

	return true;
}

int DemoPlayer::processDemoNetMessage(NetMessageData& msg, DemoDataTest* validate) {
	byte* args = msg.data;
	uint16_t* args16 = (uint16_t*)args;

	g_stats.msgSz[msg.header.type] += msg.sz;

	switch (msg.header.type) {
	case SVC_TEMPENTITY:
		g_stats.msgSz[256 + msg.data[0]] += msg.sz;
		return processTempEntityMessage(msg, validate) ? 1 : -1;
	case SVC_WEAPONANIM:
		return 1;
	case SVC_SOUND: {
		mstream bitbuffer((char*)msg.data, msg.sz);
		uint16_t field_mask = bitbuffer.readBits(9);
		uint8_t volume = 0;
		uint8_t attenuation = 0;
		uint8_t pitch = 100;
		if (field_mask & SND_FL_VOLUME) {
			volume = bitbuffer.readBits(8);
		}
		if (field_mask & SND_FL_ATTENUATION) {
			attenuation = bitbuffer.readBits(8);
		}
		uint8_t channel = bitbuffer.readBits(3);
		uint16_t ient = bitbuffer.readBits(11); // ent index (11 = rehlds value)
		convReplayEntIdx((uint8_t*)&ient, 0, 2);
		uint16_t old_sound_num = bitbuffer.readBits((field_mask & SND_FL_LARGE_INDEX) ? 16 : 8); // sound index
		uint16_t sound_num = old_sound_num;
		if (!validate && !(field_mask & SND_SENTENCE) && !convReplaySoundIdx(sound_num)) {
			return -1;
		}
		if (sound_num > 255) {
			field_mask |= SND_FL_LARGE_INDEX;
		}
		Vector origin = bitbuffer.readBitVec3Coord();
		if (field_mask & SND_FL_PITCH)
			pitch = bitbuffer.readBits(8);

		// rewrite with the new index
		bitbuffer.seek(0);
		bitbuffer.writeBits(field_mask, 9);
		if (field_mask & SND_FL_VOLUME)
			bitbuffer.writeBits(volume, 8);
		if (field_mask & SND_FL_ATTENUATION)
			bitbuffer.writeBits(attenuation, 8);
		bitbuffer.writeBits(channel, 3);
		bitbuffer.writeBits(ient, 11);
		bitbuffer.writeBits(sound_num, (field_mask & SND_FL_LARGE_INDEX) ? 16 : 8);
		bitbuffer.writeBitVec3Coord(origin);
		if (field_mask & SND_FL_PITCH)
			bitbuffer.writeBits(pitch, 8);
		msg.sz = bitbuffer.tell() + 1;

		return 1;
	}
	case SVC_SPAWNSTATICSOUND: {
		uint16_t* sound_num = args16 + 3;
		if (!validate && !convReplaySoundIdx(*sound_num)) {
			return -1;
		}
		return 1;
	}
	case SVC_STUFFTEXT:
		return 1;
#ifndef HLCOOP_BUILD
	case MSG_StartSound: {
		uint16_t& flags = *(uint16_t*)(args);
		if (flags & SND_ENT) {
			uint16_t& entidx = *(uint16_t*)(args + 2);
			convReplayEntIdx(entidx);
		}
		if ((flags & SND_SENTENCE) == 0) {
			uint16_t& soundIdx = *(uint16_t*)(args + (msg.sz - 2));
			convReplaySoundIdx(soundIdx);
		} // plugins can't change sentences(?), so client should match server
		return 1;
	}
	case MSG_TracerDecal:
		// Vector start
		// Vector end
		// byte showTracer (0/1)
		// byte ???
		return 1;
	case MSG_SayText: {
		// name coloring doesn't work for bots, so copy the name color to p1 and use them as the source
		uint16_t idx = args[0];
		convReplayEntIdx(idx);
		args[0] = idx;
		return 1;
	}
	case MSG_ScoreInfo: {
		if (useBots) {
			uint16_t idx = args[0];
			convReplayEntIdx(idx);
			args[0] = idx;

			return 1;
		}
		return 0;
	}
#endif
	case SVC_UPDATEUSERINFO: {
		uint16_t entIdx = *args + 1;
		convReplayEntIdx((byte*)&entIdx, 0, 2);
		*args = entIdx - 1;
		return 1;
	}
	default:
		if (msg.header.type == gmsgSayText) {
			uint16_t entIdx = *args;
			convReplayEntIdx((byte*)&entIdx, 0, 2);
			*args = entIdx;
			return 1;
		}
		if (msg.header.type == gmsgScoreInfo) {
			uint16_t entIdx = *args;
			convReplayEntIdx((byte*)&entIdx, 0, 2);
			*args = entIdx;
			return 1;
		}
		if (msg.header.type == gmsgTeamInfo) {
			uint16_t entIdx = *args;
			convReplayEntIdx((byte*)&entIdx, 0, 2);
			*args = entIdx;
			return 1;
		}
		if (msg.header.type == gmsgDeathMsg) {
			uint16_t entIdx = *args;
			convReplayEntIdx((byte*)&entIdx, 0, 2);
			*args = entIdx;

			uint16_t entIdx2 = args[1];
			convReplayEntIdx((byte*)&entIdx2, 0, 2);
			args[1] = entIdx2;
			return 1;
		}
		if (msg.header.type == gmsgStatusText) {
			return 0; // TODO: this is showing ***** name on screen at all times
		}
		if (msg.header.type == gmsgStatusValue) {
			uint8_t idx = *args;
			if (idx == 1) {
				// 1 idx is always the player index
				convReplayEntIdx(args, 1, msg.sz);
			}
			return 0; // same as above
		}
		//println("Unhandled netmsg %d", (int)msg.header.type);
		return 0;
	}
}

void DemoPlayer::decompressNetMessage(NetMessageData& msg) {
	if (!g_compressMessages) {
		return;
	}

	switch (msg.header.type) {
	case SVC_TEMPENTITY: {
		uint8_t type = msg.data[0];

		switch (type) {
		case TE_BEAMPOINTS:
		case TE_BEAMDISK:
		case TE_BEAMCYLINDER:
		case TE_BEAMTORUS:
		case TE_LIGHTNING:
		case TE_BEAMSPRITE:
		case TE_SPRITETRAIL:
		case TE_BUBBLETRAIL:
		case TE_SPRITE_SPRAY:
		case TE_SPRAY:
		case TE_LINE:
		case TE_SHOWLINE:
		case TE_BOX:
		case TE_BLOODSTREAM:
		case TE_BLOOD:
		case TE_MODEL:
		case TE_PROJECTILE:
		case TE_TRACER:
		case TE_STREAK_SPLASH:
		case TE_USERTRACER:
			msg.decompressCoords(1, 6);
			break;
		case TE_BEAMENTPOINT:
			msg.decompressCoords(3, 3);
			break;
		case TE_EXPLOSION:
		case TE_SMOKE:
		case TE_SPARKS:
		case TE_SPRITE:
		case TE_GLOWSPRITE:
		case TE_ARMOR_RICOCHET:
		case TE_DLIGHT:
		case TE_LARGEFUNNEL:
		case TE_BLOODSPRITE:
		case TE_FIREFIELD:
		case TE_GUNSHOT:
		case TE_TAREXPLOSION:
		case TE_EXPLOSION2:
		case TE_PARTICLEBURST:
		case TE_LAVASPLASH:
		case TE_TELEPORT:
		case TE_IMPLOSION:
		case TE_DECAL:
		case TE_GUNSHOTDECAL:
		case TE_DECALHIGH:
		case TE_WORLDDECAL:
		case TE_WORLDDECALHIGH:
		case TE_BSPDECAL:
			msg.decompressCoords(1, 3);
			break;
		case TE_ELIGHT:
			msg.decompressCoords(3, 4);
			msg.decompressCoords(23, 1);
			break;
		case TE_PLAYERATTACHMENT:
			msg.decompressCoords(2, 1, true);
			break;
		case TE_BUBBLES:
			msg.decompressCoords(1, 7);
			break;
		case TE_EXPLODEMODEL:
			msg.decompressCoords(1, 4);
			break;
		case TE_BREAKMODEL:
			msg.decompressCoords(1, 9);
			break;
		case TE_PLAYERDECAL:
			msg.decompressCoords(2, 3);
			break;
		case TE_MULTIGUNSHOT:
			msg.decompressCoords(1, 3);
			msg.decompressCoords(13, 3, true);
			msg.decompressCoords(25, 2);
			break;
		default:
			break;
		}
		break;
	}
#ifndef HLCOOP_BUILD
	case MSG_TracerDecal: {
		msg.decompressCoords(0, 6);
		return;
	}
	case MSG_StartSound: {
		uint16_t& flags = *(uint16_t*)msg.data;

		int oriOffset = 2;
		if (flags & SND_ENT) {
			oriOffset += 2;
		}
		if (flags & SND_VOLUME) {
			oriOffset += 1;
		}
		if (flags & SND_PITCH) {
			oriOffset += 1;
		}
		if (flags & SND_ATTENUATION) {
			oriOffset += 1;
		}

		if ((flags & SND_ORIGIN) == 0 && (flags & SND_ENT) != 0) {
			// add origin to suppress "sound without origin" messages.
			// It doesn't seem necesary otherwise because the sounds still work.
			// The messages seem random too which is weird, they come and go with the same replay.
			uint16_t entidx = *(uint16_t*)(msg.data + 2);
			if (entidx < MAX_EDICTS && fileedicts[entidx].edflags & EDFLAG_VALID) {
				netedict& ed = fileedicts[entidx];
				int32_t neworigin[3];
				for (int i = 0; i < 3; i++) {
					neworigin[i] = ed.origin[i] / 4; // 19.5 -> 16.3 fixed point
				}

				int newOriginSz = sizeof(int32_t) * 3;
				int moveSz = msg.sz - oriOffset;
				byte* originPtr = msg.data + oriOffset;
				memmove(originPtr + newOriginSz, originPtr, moveSz);
				memcpy(originPtr, neworigin, newOriginSz);
				msg.sz += newOriginSz;
				flags |= SND_ORIGIN;
			}
			else {
				println("Invalid ent index decompressing startsound %d / %d", (int)entidx, (int)replayEnts.size());
			}
			return;
		}

		if ((flags & SND_ORIGIN) == 0) {
			return;
		}

		// add fractional part of origin back in
		msg.decompressCoords(oriOffset, 3);
		return;
	}
#endif
	default:
		break;
	}
}

void DemoPlayer::validateFrame(DemoDataStream& reader, DemoDataTest* results) {
	uint32_t startOffset = reader.tell();

	replayData = &reader;
	isValidating = true;

	readDemoFrame(results);

	reader.seek(startOffset);
	replayData = NULL;
	isValidating = false;
}

bool DemoPlayer::readNetworkMessages(mstream& reader, DemoDataTest* validate) {
	uint32_t startOffset = reader.tell();

	uint16_t numMessages;
	reader.read(&numMessages, 2);

	if (validate) validate->msgCount = numMessages;

	if (numMessages > MAX_NETMSG_FRAME) {
		println("Invalid net msg count %d", (int)numMessages);
		return false;
	}

	static NetMessageData msg;

	for (int i = 0; i < numMessages; i++) {
		reader.read(&msg.header, sizeof(DemoNetMessage));
		msg.sz = (msg.header.szHighBit << 8) | msg.header.sz;

		if (msg.header.hasOrigin) {
			int sz = msg.header.hasLongOrigin ? 3 : 2;
			reader.read(&msg.origin[0], sz);
			reader.read(&msg.origin[1], sz);
			reader.read(&msg.origin[2], sz);
		}
		if (msg.header.hasEdict) {
			reader.read(&msg.eidx, 2);
		}
		else {
			msg.eidx = 0;
		}
		if (msg.sz > MAX_NETMSG_DATA) {
			println("Invalid net msg size %d", (int)msg.sz);
			return false; // data corrupted, abort before SVC_BAD
		}
		reader.read(msg.data, msg.sz);

		if ((msg.eidx >= replayEnts.size() && !validate) || (validate && msg.eidx > MAX_EDICTS)) {
			println("Invalid msg ent %d for %s", (int)msg.eidx, msgTypeStr(msg.header.type));
			if (validate) return false;
			continue;
		}

		if (!validate && msg.header.hasEdict && replayEnts[msg.eidx].h_ent.GetEdict() && !(replayEnts[msg.eidx].h_ent.GetEdict()->v.flags & FL_CLIENT)) {
			continue; // target is not a player (bots disabled probably)
		}

		decompressNetMessage(msg);

		if (validate) {
			NetMessageData& expected = validate->expectedMsg[i];
			if (memcmp(&expected.header, &msg.header, sizeof(DemoNetMessage))) {
				ALERT(at_console, "Read unexpected header for %s!\n", msgTypeStr(expected.header.type));
				return false;
			}
			for (int z = 0; z < 3 && expected.header.hasOrigin; z++) {
				if (expected.header.hasLongOrigin && !FIXED_EQUALS(expected.origin[z], msg.origin[z], 24)) {
					ALERT(at_console, "Read unexpected long origin[%d] for %s!\n", (int)z, msgTypeStr(expected.header.type));
					return false;
				}
				else if ((int16_t)expected.origin[z] != (int16_t)msg.origin[z]) {
					ALERT(at_console, "Read unexpected short origin[%d] for %s!\n", (int)z, msgTypeStr(expected.header.type));
					return false;
				}
			}
			if (expected.sz != msg.sz) {
				ALERT(at_console, "Read unexpected size for %s!\n", msgTypeStr(expected.header.type));
				return false;
			}
			if (expected.eidx != msg.eidx) {
				ALERT(at_console, "Read unexpected eidx for %s!\n", msgTypeStr(expected.header.type));
				return false;
			}
			if (memcmp(expected.data, msg.data, expected.sz)) {
				ALERT(at_console, "Read unexpected data for %s!\n", msgTypeStr(expected.header.type));
				return false;
			}
		}

		int parseResult = processDemoNetMessage(msg, validate);

		if (parseResult == -1) {
			return false;
		}
		else if (parseResult == 0) {
			continue;
		}

		if (!validate) {
			edict_t* ent = msg.header.hasEdict ? replayEnts[msg.eidx].h_ent.GetEdict() : NULL;

			if (CBaseEntity::Instance(ent)->IsPlayer()) {
				 // send individual messages to anyone who is observing this player
				for (int i = 1; i < gpGlobals->maxClients; i++) {
					CBasePlayer* spec = (CBasePlayer*)UTIL_PlayerByIndex(i);
					if (!spec || (spec->pev->flags & FL_FAKECLIENT)) {
						continue;
					}

					if (spec->m_hObserverTarget.GetEdict() == ent || (!spec->IsObserver() && msg.eidx == 1)) {
						msg.send(msg.header.dest, spec->edict());
					}
				}
			}
			else {
				msg.send(msg.header.dest, ent);
			}
		}
	}

	g_stats.msgCurrentSz = reader.tell() - startOffset;
	g_stats.msgCount += numMessages;

	return true;
}

bool DemoPlayer::readEvents(mstream& reader, DemoDataTest* validate) {
	uint32_t startOffset = reader.tell();

	uint8_t numEvents;
	reader.read(&numEvents, 1);

	if (validate) validate->evtCount = numEvents;

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
			fparam1 = FIXED_TO_FLOAT(ev.fparam1, 24, 0) / 128.0f;
		}
		if (ev.header.hasFparam2) {
			reader.read(&ev.fparam2, 3);
			fparam2 = FIXED_TO_FLOAT(ev.fparam2, 24, 0) / 128.0f;
		}
		if (ev.header.hasIparam1) {
			reader.read(&ev.iparam1, 2);
		}
		if (ev.header.hasIparam2) {
			reader.read(&ev.iparam2, 2);
		}

		uint16_t eidx = ev.header.entindex;
		if (validate) {
			if (eidx > MAX_EDICTS) {
				println("Invalid event edict %d", (int)ev.header.entindex);
				return false;
			}
		}
		else { // play the event
			convReplayEntIdx((byte*)&eidx, 0, 2);
			if ((!useBots && eidx >= replayEnts.size()) || (useBots && eidx > gpGlobals->maxClients)) {
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
		
	}

	g_stats.eventCurrentSz = reader.tell() - startOffset;
	g_stats.eventCount += numEvents;

	return true;
}

bool DemoPlayer::readClientCommands(mstream& reader, DemoDataTest* validate) {
	uint32_t startOffset = reader.tell();

	uint8_t numCommands;
	reader.read(&numCommands, 1);

	if (validate) validate->cmdCount = numCommands;

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
			if (validate) return false;
			continue;
		}
		if (cmd.idx == 0) {
			if (!validate)
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

		if (!validate) {
			println("[Cmd][%s][%s] %s", legacySteamId, plr.name, commandChars);
		}
	}

	g_stats.cmdCurrentSz = reader.tell() - startOffset;
	g_stats.cmdCount += numCommands;

	return true;
}

bool DemoPlayer::readDemoFrame(DemoDataTest* validate) {
	uint32_t t = (getEpochMillis() - replayStartTime) * replaySpeed;

	if (!validate)
		replayData->seek(nextFrameOffset);

	if (validate) validate->success = false;

	DemoFrame header;
	if (!replayData->read(&header, sizeof(DemoFrame))) {
		UTIL_ClientPrintAll(HUD_PRINTTALK, "[HLTV] Unexpected EOF\n");
		closeReplayFile();
		return false;
	}
	uint32_t demoTime = 0;
	uint32_t frameSize = 0;
	uint32_t headerSz = sizeof(DemoFrame);
	if (header.isGiantFrame) {
		replayData->read(&demoTime, sizeof(uint32_t));
		replayData->read(&frameSize, sizeof(uint32_t));
		headerSz += 8;
	}
	else if (header.isBigFrame) {
		replayData->read(&demoTime, sizeof(uint8_t));
		replayData->read(&frameSize, sizeof(uint16_t));
		demoTime = lastFrameDemoTime + demoTime;
		headerSz += 3;
	}
	else {
		replayData->read(&demoTime, sizeof(uint8_t));
		replayData->read(&frameSize, sizeof(uint8_t));
		demoTime = lastFrameDemoTime + demoTime;
		headerSz += 2;
	}

	if (validate) {
		memcpy(&validate->header, &header, sizeof(DemoFrame));
		validate->demoTime = demoTime;
	}

	frameProgress = 1.0f;
	if (demoTime > lastFrameDemoTime) {
		frameProgress = 1.0f - ((demoTime - t) / (float)(demoTime - lastFrameDemoTime));
	}

	if (demoTime > t && !validate) {
		//println("Wait %u > %u", header.demoTime, t);
		interpolateEdicts();
		return false;
	}

	g_stats.bigFrameCount += header.isBigFrame;
	g_stats.giantFrameCount += header.isGiantFrame;
	lastFrameDemoTime = demoTime;

	if (frameSize > 1024 * 1024 * 32 || frameSize == 0) {
		UTIL_ClientPrintAll(HUD_PRINTTALK, "[HLTV] Invalid frame size\n");
		closeReplayFile();
		return false;
	}
	nextFrameOffset += frameSize;
	frameSize -= headerSz;

	if (frameSize == 0) {
		g_stats.incTotals();
		if (validate) validate->success = true;
		return true; // nothing changed
	}

	//println("Frame %d (%.1f kb), Time: %.1f", replayFrame, header.frameSize / 1024.0f, (float)TimeDifference(0, header.demoTime));

	/*
	if (replayFrame == 242) {
		println("debug");
	}
	*/

	char* frameData = new char[frameSize];
	if (!replayData->read(frameData, frameSize)) {
		delete[] frameData;
		UTIL_ClientPrintAll(HUD_PRINTTALK, "[HLTV] Unexpected EOF\n");
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

	if (header.hasEntityDeltas && (!readEntDeltas(reader, validate))) {
		delete[] frameData;
		return false;
	}

	if (header.hasPlayerDeltas && !readPlayerDeltas(reader, validate)) {
		delete[] frameData;
		return false;
	}
	if (header.hasNetworkMessages && !readNetworkMessages(reader, validate)) {
		delete[] frameData;
		return false;
	}
	if (header.hasEvents && !readEvents(reader, validate)) {
		delete[] frameData;
		return false;
	}
	if (header.hasCommands && !readClientCommands(reader, validate)) {
		delete[] frameData;
		return false;
	}
	if (!validate && !simulate(header)) {
		delete[] frameData;
		return false;
	}

	g_stats.incTotals();

	replayFrame++;

	memcpy(&lastReplayFrame, &header, sizeof(DemoFrame));
	delete[] frameData;

	if (validate) validate->success = true;

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

	for (int i = 0; i < (int)replayEnts.size(); i++) {
		if (!fileedicts[i].edflags) {
			continue;
		}

		if (!replayEnts[i].h_ent || replayEnts[i].h_ent.GetEdict()->v.effects & EF_NODRAW) {
			continue;
		}

		edict_t* ent = replayEnts[i].h_ent.GetEdict();
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
					ent->v.angles.x = normalizeRangef(ent->v.angles.x, -180.0f, 180.0f) * -0.33f;
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

	//float dtScale = 1.0f / dt;
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
	if (!replayData) {
		return;
	}

	while (readDemoFrame());
}

int DemoPlayer::GetWeaponData(edict_t* player, weapon_data_t* info) {
	CBasePlayer* pl = (CBasePlayer*)CBasePlayer::Instance(player);

	if (pl->pev->iuser1 != OBS_IN_EYE) {
		// not first-person spectating a replay bot, so give them their own weapon data
		return -1;
	}

	memset(info, 0, 32 * sizeof(weapon_data_t));

	int originalEntIdx = pl->m_hObserverTarget.GetEdict()->v.iuser4;
	DemoPlayerEnt& pinfo = fileplayerinfos[originalEntIdx-1];

	// set up weapon state for spectators
	weapon_data_t* item = &info[pinfo.weaponId];

	item->m_iId = pinfo.weaponId;
	item->m_iClip = pinfo.clip;

	item->m_flTimeWeaponIdle = 10;
	item->m_flNextPrimaryAttack = 10;
	item->m_flNextSecondaryAttack = 10;
	item->m_fInReload = pinfo.inReload;
	item->m_fInSpecialReload = pinfo.inReloadSpecial;
	item->fuser1 = 10; // V_max( gun->pev->fuser1, -0.001f );
	item->fuser2 = 10; // gun->m_flStartThrow;
	item->fuser3 = 10; // gun->m_flReleaseThrow;
	item->iuser1 = pinfo.chargeReady;
	item->iuser2 = pinfo.inAttack;
	item->iuser3 = pinfo.fireState;

	return 1;
}

void DemoPlayer::OverrideClientData(const edict_t* ent, int sendweapons, clientdata_t* cd) {

	if (ent->v.iuser1 != OBS_IN_EYE) {
		// not first-person spectating a replay bot, so give them their own weapon data
		return;
	}

	CBasePlayer* pl = (CBasePlayer*)CBasePlayer::Instance(ent);
	int originalEntIdx = pl->m_hObserverTarget.GetEdict()->v.iuser4;

	cd->m_iId = fileplayerinfos[originalEntIdx - 1].weaponId;
}