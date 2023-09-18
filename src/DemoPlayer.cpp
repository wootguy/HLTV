#include "DemoPlayer.h"
#include "mmlib.h"
#include "SvenTV.h"

DemoPlayer::DemoPlayer() {
	fileedicts = new netedict[MAX_EDICTS];
	fileplayerinfos = new DemoPlayerEnt[32];

	memset(fileplayerinfos, 0, 32 * sizeof(DemoPlayerEnt));
}

DemoPlayer::~DemoPlayer() {
	delete[] fileplayerinfos;
	delete[] fileedicts;

	if (replayFile) {
		fclose(replayFile);
	}
}

bool DemoPlayer::openDemo(edict_t* plr, string path, float offsetSeconds, bool skipPrecache) {
	this->offsetSeconds = offsetSeconds;
	stopReplay();
	replayFile = fopen(path.c_str(), "rb");

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

	botInitDone = false;

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
		if (replayEnts[i].IsValid() && (replayEnts[i].GetEdict()->v.flags & FL_CLIENT) == 0)
			REMOVE_ENTITY(replayEnts[i]);
	}
	replayEnts.clear();

	for (int i = 0; i < bots.size(); i++) {
		if (!bots[i].IsValid()) {
			continue;
		}
		int userid = g_engfuncs.pfnGetPlayerUserId(bots[i]);
		g_engfuncs.pfnServerCommand(UTIL_VarArgs("kick #%d\n", userid));
	}
	g_engfuncs.pfnServerExecute();
	bots.clear();
}

bool DemoPlayer::readEntDeltas(mstream& reader) {
	uint16_t numEntDeltas = 0;
	reader.read(&numEntDeltas, 2);

	//println("Reading %d deltas", (int)numEntDeltas);

	int loop = -1;
	uint16_t fullIndex = 0;
	for (int i = 0; i < numEntDeltas; i++) {
		loop++;
		uint8_t offset = 0;
		reader.read(&offset, 1);

		if (offset == 0) {
			reader.read(&fullIndex, 2);
		}
		else {
			fullIndex += offset;
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

	return true;
}

bool DemoPlayer::validateEdicts() {
	for (int i = 0; i < MAX_EDICTS; i++) {
		if (fileedicts[i].edtype != NETED_INVALID && fileedicts[i].edtype != NETED_BEAM && fileedicts[i].aiment > 8192) {
			println("Invalid edict %d has %d", i, (int)fileedicts[i].aiment);
			return false;
		}
	}
	return true;
}

bool DemoPlayer::applyEntDeltas(DemoFrame& header) {
	int errorSprIdx = g_engfuncs.pfnModelIndex("sprites/error.spr");
	const char* model_entity = "env_sprite";

	for (int i = 1; i < MAX_EDICTS; i++) {
		if (!fileedicts[i].edtype) {
			if (i < replayEnts.size()) {
				replayEnts[i].GetEdict()->v.effects |= EF_NODRAW;
			}
			continue;
		}

		// create ents until the client has enough to handle this demo frame
		while (i >= replayEnts.size()) {
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
					REMOVE_ENTITY(replayEnts[i]);
				}
				closeReplayFile();
				return false;
			}

			replayEnts.push_back(ent);
		}

		edict_t* ent = replayEnts[i];

		if (useBots && (i-1) < bots.size() && fileedicts[i].edtype == NETED_PLAYER && (ent->v.flags & FL_CLIENT) == 0) {
			REMOVE_ENTITY(replayEnts[i]);
			replayEnts[i] = ent = bots[i-1];
			ent->v.solid = SOLID_SLIDEBOX;
		}
		else if (fileedicts[i].edtype == NETED_BEAM && (ent->v.flags & (FL_MONSTER | FL_CLIENT))) {
			map<string, string> keys;
			keys["model"] = "sprites/error.spr";
			CBaseEntity* newEnt = CreateEntity("beam", keys, true);

			if (ent->v.flags & FL_CLIENT) {
				ent->v.effects |= EF_NODRAW;
				ent->v.solid = SOLID_NOT;
			}
			else {
				REMOVE_ENTITY(replayEnts[i]);
			}
			
			replayEnts[i] = ent = newEnt->edict();
			ent->v.flags |= FL_CUSTOMENTITY;
			ent->v.effects |= EF_NODRAW;
		}
		else if (fileedicts[i].edtype != NETED_BEAM && (ent->v.flags & (FL_MONSTER|FL_CLIENT)) == 0) {
			map<string, string> keys;
			keys["model"] = "sprites/error.spr";

			CBaseEntity* newEnt = CreateEntity(model_entity, keys, true);

			if (ent->v.flags & FL_CLIENT) {
				ent->v.effects |= EF_NODRAW;
				ent->v.solid = SOLID_NOT;
			}
			else {
				REMOVE_ENTITY(replayEnts[i]);
			}

			replayEnts[i] = ent = newEnt->edict();
			ent->v.solid = SOLID_NOT;
			ent->v.effects |= EF_NODRAW;
			ent->v.movetype = MOVETYPE_NONE;
			ent->v.flags |= FL_MONSTER;
		}
		
		int oldModelIdx = ent->v.modelindex;
		int oldSeq = ent->v.sequence;
		float lastFrame = ent->v.frame;

		fileedicts[i].apply(ent, replayEnts);
		
		// interpolation setup
		if (fileedicts[i].edtype != NETED_MONSTER) {
			ent->v.vuser3 = ent->v.vuser4; // previous origin
			ent->v.vuser4 = ent->v.origin; // target origin
			ent->v.vuser1 = ent->v.vuser2; // previous angles
			ent->v.vuser2 = ent->v.angles; // target angles
			ent->v.fuser1 = ent->v.fuser2; // previous frame
			ent->v.fuser2 = ent->v.frame; // target frame
			ent->v.iuser4 = oldSeq; // previous sequence
		}
		else if (fileedicts[i].edtype == NETED_MONSTER) {
			// monster data is updated at a framerate independent of the server
			// TODO: add a delta for this instead of trying to detect an update
			if (fabs(ent->v.fuser2 - ent->v.frame) > 0.1f || (ent->v.vuser4 - ent->v.origin).Length() > 0.1f) {
				ent->v.vuser3 = ent->v.vuser4; // previous origin
				ent->v.vuser4 = ent->v.origin; // target origin
				ent->v.vuser1 = ent->v.vuser2; // previous angles
				ent->v.vuser2 = ent->v.angles; // target angles

				ent->v.fuser1 = ent->v.fuser2; // previous frame
				ent->v.fuser2 = ent->v.frame; // target frame
				
				ent->v.starttime = clampf(gpGlobals->time - ent->v.fuser4, 0, 0.1f); // expected time between frames
				ent->v.fuser4 = gpGlobals->time; // previous animation start time (for next next update)
				ent->v.iuser4 = oldSeq;
			}
		}
		ent->v.fuser3 = ent->v.framerate;
		ent->v.framerate = 0.00001f;		

		if (oldModelIdx != ent->v.modelindex) {
			string newModel = getReplayModel(ent->v.modelindex);
			if (fileedicts[i].edtype == NETED_BEAM && newModel.find(".spr") == string::npos) {
				println("Invalid model set on beam: %s", newModel.c_str());
			}
			else if (fileedicts[i].edtype != NETED_PLAYER) {
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
	}
	eidx = ENTINDEX(replayEnts[eidx]);
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
	uint32_t includedPlayers = 0;
	reader.read(&includedPlayers, 4);

	int numPlayerDeltas = 0;
	for (int i = 0; i < gpGlobals->maxClients; i++) {
		if ((includedPlayers & (1 << i)) == 0) {
			continue;
		}

		DemoPlayerDelta deltas = fileplayerinfos[i].readDeltas(reader);

		if (i < bots.size()) {
			edict_t* bot = bots[i];
			int eidx = ENTINDEX(bot);
			char* infoBuffer = g_engfuncs.pfnGetInfoKeyBuffer(bot);
			DemoPlayerEnt& info = fileplayerinfos[i];

			if (deltas.nameChanged) {
				g_engfuncs.pfnSetClientKeyValue(eidx, infoBuffer, "name", info.name);
			}
			if (deltas.modelChanged) {
				g_engfuncs.pfnSetClientKeyValue(eidx, infoBuffer, "model", info.model);
			}
			if (deltas.colorsChanged) {
				g_engfuncs.pfnSetClientKeyValue(eidx, infoBuffer, "topcolor", UTIL_VarArgs("%d", info.topColor));
				g_engfuncs.pfnSetClientKeyValue(eidx, infoBuffer, "bottomcolor", UTIL_VarArgs("%d", info.bottomColor));
			}
			if (deltas.weaponmodelChanged) {
				bot->v.weaponmodel = ALLOC_STRING(getReplayModel(info.weaponmodel).c_str());
			}
			if (deltas.viewmodelChanged) {
				bot->v.viewmodel = ALLOC_STRING(getReplayModel(info.viewmodel).c_str());
			}
			bot->v.frags = info.frags;
			bot->v.weaponanim = info.weaponanim;
			bot->v.armorvalue = info.armorvalue;
			bot->v.view_ofs.z = info.view_ofs / 16.0f;
			bot->v.deadflag = info.observer & 1 ? DEAD_DEAD : DEAD_NO;
		}

		numPlayerDeltas++;
	}

	//println("Got %d player deltas", numPlayerDeltas);

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

	static const char* te_names[TE_USERTRACER + 1] = {
		"TE_BEAMPOINTS", "TE_BEAMENTPOINT", "TE_GUNSHOT", "TE_EXPLOSION",
		"TE_TAREXPLOSION", "TE_SMOKE", "TE_TRACER", "TE_LIGHTNING",
		"TE_BEAMENTS", "TE_SPARKS", "TE_LAVASPLASH", "TE_TELEPORT",
		"TE_EXPLOSION2", "TE_BSPDECAL", "TE_IMPLOSION", "TE_SPRITETRAIL", 0,
		"TE_SPRITE", "TE_BEAMSPRITE", "TE_BEAMTORUS", "TE_BEAMDISK",
		"TE_BEAMCYLINDER", "TE_BEAMFOLLOW", "TE_GLOWSPRITE", "TE_BEAMRING",
		"TE_STREAK_SPLASH", 0, "TE_DLIGHT", "TE_ELIGHT", "TE_TEXTMESSAGE",
		"TE_LINE", "TE_BOX",

		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,

		"TE_KILLBEAM", "TE_LARGEFUNNEL", "TE_BLOODSTREAM", "TE_SHOWLINE",
		"TE_BLOOD", "TE_DECAL", "TE_FIZZ", "TE_MODEL",
		"TE_EXPLODEMODEL", "TE_BREAKMODEL", "TE_GUNSHOTDECAL", "TE_SPRITE_SPRAY",
		"TE_ARMOR_RICOCHET", "TE_PLAYERDECAL", "TE_BUBBLES", "TE_BUBBLETRAIL",
		"TE_BLOODSPRITE", "TE_WORLDDECAL", "TE_WORLDDECALHIGH", "TE_DECALHIGH",
		"TE_PROJECTILE", "TE_SPRAY", "TE_PLAYERSPRITES", "TE_PARTICLEBURST",
		"TE_FIREFIELD", "TE_PLAYERATTACHMENT", "TE_KILLPLAYERATTACHMENTS",
		"TE_MULTIGUNSHOT", "TE_USERTRACER"
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
		if (plr && (plr->v.flags & FL_CLIENT) == 0) {
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
		if (plr && (plr->v.flags & FL_CLIENT) == 0) {
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
		if (plr && (plr->v.flags & FL_CLIENT) == 0) {
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

	switch (msg.header.type) {
	case SVC_TEMPENTITY:
		return processTempEntityMessage(msg);
	case MSG_StartSound: {
		uint16_t& soundIdx = *(uint16_t*)(args + (msg.header.sz - 2));
		convReplaySoundIdx(soundIdx);
		return true;
	}
	default:
		println("Unhandled netmsg %d", (int)msg.header.type);
		return false;
	}
}

bool DemoPlayer::readNetworkMessages(mstream& reader) {
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
			reader.read(&msg.origin, sizeof(float) * 3);
		}
		if (msg.header.hasEdict) {
			reader.read(&msg.eidx, 2);
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

		const float* ori = msg.header.hasOrigin ? msg.origin : NULL;
		edict_t* ent = msg.header.hasEdict ? replayEnts[msg.eidx].GetEdict() : NULL;
		if (!processDemoNetMessage(msg)) {
			continue;
		}

		//println("SEND MSG %d sz %d", (int)msg.header.type, (int)msg.header.sz);
		MESSAGE_BEGIN(msg.header.dest, msg.header.type, ori, ent);

		int numLongs = msg.header.sz / 4;
		int numBytes = msg.header.sz % 4;

		for (int i = 0; i < numLongs; i++) {
			WRITE_LONG(((uint32_t*)msg.data)[i]);
		}

		int byteOffset = numLongs * 4;
		for (int i = byteOffset; i < byteOffset + numBytes; i++) {
			WRITE_BYTE(msg.data[i]);
		}

		MESSAGE_END();

		//println("Read msg %d", (int)msg.type);
	}

	return true;
}

bool DemoPlayer::readClientCommands(mstream& reader) {
	uint16_t numCommands;
	reader.read(&numCommands, 2);

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

	return true;
}

bool DemoPlayer::readDemoFrame() {
	uint32_t t = (getEpochMillis() - replayStartTime) * 1.0f;

	fseek(replayFile, nextFrameOffset, SEEK_SET);

	DemoFrame header;
	if (!fread(&header, sizeof(DemoFrame), 1, replayFile)) {
		ClientPrintAll(HUD_PRINTTALK, "[SvenTV] Unexpected EOF\n");
		closeReplayFile();
		return false;
	}

	frameProgress = 1.0f;
	if (header.demoTime > lastReplayFrame.demoTime) {
		frameProgress = 1.0f - ((header.demoTime - t) / (float)(header.demoTime - lastReplayFrame.demoTime));
	}

	if (header.demoTime > t) {
		//println("Wait %u > %u", header.demoTime, t);
		interpolateEdicts();
		return false;
	}
	if (header.frameSize > 1024 * 1024 * 32 || header.frameSize == 0) {
		ClientPrintAll(HUD_PRINTTALK, "[SvenTV] Invalid frame size\n");
		closeReplayFile();
		return false;
	}
	nextFrameOffset = ftell(replayFile) + (header.frameSize - sizeof(DemoFrame));

	//println("Frame %d (%.1f kb), Time: %.1f", replayFrame, header.frameSize / 1024.0f, (float)TimeDifference(0, header.demoTime));

	/*
	if (replayFrame == 242) {
		println("debug");
	}
	*/

	char* frameData = new char[header.frameSize];
	if (!fread(frameData, header.frameSize, 1, replayFile)) {
		delete[] frameData;
		ClientPrintAll(HUD_PRINTTALK, "[SvenTV] Unexpected EOF\n");
		closeReplayFile();
		return false;
	}

	mstream reader = mstream(frameData, header.frameSize);

	if (header.isKeyFrame) {
		memset(fileedicts, 0, MAX_EDICTS * sizeof(netedict));
		memset(fileplayerinfos, 0, 32 * sizeof(DemoPlayerEnt));
	}

	if (header.hasEntityDeltas && (!readEntDeltas(reader) || !applyEntDeltas(header))) {
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

	if (header.hasCommands && !readClientCommands(reader)) {
		delete[] frameData;
		return false;
	}

	replayFrame++;

	memcpy(&lastReplayFrame, &header, sizeof(DemoFrame));
	delete[] frameData;

	return true;
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
		if (fileedicts[i].edtype == NETED_INVALID) {
			continue;
		}

		if (!replayEnts[i].IsValid() || replayEnts[i].GetEdict()->v.effects & EF_NODRAW) {
			continue;
		}

		edict_t* ent = replayEnts[i];

		if (fileedicts[i].edtype == NETED_MONSTER) {
			float t = 1;
			if (ent->v.iuser4 == ent->v.sequence && ent->v.starttime > 0) {
				t = clampf((gpGlobals->time - ent->v.fuser4) / ent->v.starttime, 0, 1);
			}

			float targetFrame = ent->v.fuser2;
			if (ent->v.fuser3 >= 0 && targetFrame < ent->v.fuser1) {
				targetFrame += 255;
			}
			if (ent->v.fuser3 <= 0 && targetFrame > ent->v.fuser1) {
				targetFrame -= 255;
			}

			ent->v.origin[0] = lerp(ent->v.vuser3[0], ent->v.vuser4[0], t);
			ent->v.origin[1] = lerp(ent->v.vuser3[1], ent->v.vuser4[1], t);
			ent->v.origin[2] = lerp(ent->v.vuser3[2], ent->v.vuser4[2], t);

			ent->v.angles[0] = anglelerp(ent->v.vuser1[0], ent->v.vuser2[0], t);
			ent->v.angles[1] = anglelerp(ent->v.vuser1[1], ent->v.vuser2[1], t);
			ent->v.angles[2] = anglelerp(ent->v.vuser1[2], ent->v.vuser2[2], t);

			ent->v.frame = lerp(ent->v.fuser1, targetFrame, t);

			if (ent->v.frame > 255) {
				ent->v.frame -= 255;
			}
			if (ent->v.frame < 0) {
				ent->v.frame += 255;
			}
		}
		else {
			float frameDelta = ent->v.fuser2 - ent->v.fuser1;
			bool sameDir = (frameDelta >= 0) == (ent->v.fuser3 >= 0);
			if (ent->v.iuser4 == ent->v.sequence && sameDir) {
				ent->v.frame = lerp(ent->v.fuser1, ent->v.fuser2, frameProgress);
			}

			ent->v.origin[0] = lerp(ent->v.vuser3[0], ent->v.vuser4[0], frameProgress);
			ent->v.origin[1] = lerp(ent->v.vuser3[1], ent->v.vuser4[1], frameProgress);
			ent->v.origin[2] = lerp(ent->v.vuser3[2], ent->v.vuser4[2], frameProgress);

			ent->v.angles[0] = anglelerp(ent->v.vuser1[0], ent->v.vuser2[0], frameProgress);
			ent->v.angles[1] = anglelerp(ent->v.vuser1[1], ent->v.vuser2[1], frameProgress);

			// fixes hud info
			g_engfuncs.pfnSetOrigin(ent, ent->v.origin);

			if (fileedicts[i].edtype == NETED_PLAYER) {
				updatePlayerModelPitchBlend(ent);

				if ((ent->v.flags & FL_CLIENT) == 0) {
					updatePlayerModelGait(ent, dt); // manual gait calculations for non-player entity
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

	/*
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
	*/
}

bool DemoPlayer::initBots() {
	edict_t* bot = g_engfuncs.pfnCreateFakeClient(UTIL_VarArgs("bot%d", bots.size()));
	if (!bot) {
		println("Failed to create bot %d", bots.size());
		return true; // now start setting user info
	}
	else {
		gpGamedllFuncs->dllapi_table->pfnClientPutInServer(bot);
		char* infoBuffer = g_engfuncs.pfnGetInfoKeyBuffer(bot);
		g_engfuncs.pfnSetClientKeyValue(ENTINDEX(bot), infoBuffer, "model", "player");
		bot->v.solid = SOLID_NOT;
		bot->v.takedamage = DAMAGE_NO;
		bot->v.movetype = MOVETYPE_NOCLIP;
		bot->v.effects |= EF_NODRAW;

		bots.push_back(bot);
		if (bots.size() == demoHeader.maxPlayers) {
			return true;
		}
	}

	return false;
}

void DemoPlayer::playDemo() {
	if (!replayFile) {
		return;
	}

	if (!botInitDone) {
		if (!useBots || initBots()) {
			botInitDone = true;
			replayStartTime = getEpochMillis() - (uint64_t)(offsetSeconds * 1000);
		}
		else {
			return;
		}
	}

	while (readDemoFrame());
}