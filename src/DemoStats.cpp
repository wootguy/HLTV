#include "DemoStats.h"
#include "main.h"
#include "netedict.h"
#include "DemoFile.h"
#include "DemoPlayer.h"
#include "SvenTV.h"

string getMessageName(int messageType) {
#ifndef HLCOOP_BUILD
	static const char* svc_names[146] = {
		"SVC_BAD", "SVC_NOP", "SVC_DISCONNECT", "SVC_EVENT",
		"SVC_VERSION", "SVC_SETVIEW", "SVC_SOUND", "SVC_TIME",
		"SVC_PRINT", "SVC_STUFFTEXT", "SVC_SETANGLE", "SVC_SERVERINFO",
		"SVC_LIGHTSTYLE", "SVC_UPDATEUSERINFO", "SVC_DELTADESCRIPTION", "SVC_CLIENTDATA",
		"SVC_STOPSOUND", "SVC_PINGS", "SVC_PARTICLE", "SVC_DAMAGE",
		"SVC_SPAWNSTATIC", "SVC_EVENT_RELIABLE", "SVC_SPAWNBASELINE", "SVC_TEMPENTITY",
		"SVC_SETPAUSE", "SVC_SIGNONNUM", "SVC_CENTERPRINT", "SVC_KILLEDMONSTER",
		"SVC_FOUNDSECRET", "SVC_SPAWNSTATICSOUND", "SVC_INTERMISSION", "SVC_FINALE",
		"SVC_CDTRACK", "SVC_RESTORE", "SVC_CUTSCENE", "SVC_WEAPONANIM",
		"SVC_DECALNAME", "SVC_ROOMTYPE", "SVC_ADDANGLE", "SVC_NEWUSERMSG",
		"SVC_PACKETENTITIES", "SVC_DELTAPACKETENTITIES", "SVC_CHOKE", "SVC_RESOURCELIST",
		"SVC_NEWMOVEVARS", "SVC_RESOURCEREQUEST", "SVC_CUSTOMIZATION", "SVC_CROSSHAIRANGLE",
		"SVC_SOUNDFADE", "SVC_FILETXFERFAILED", "SVC_HLTV", "SVC_DIRECTOR",
		"SVC_VOICEINIT", "SVC_VOICEDATA", "SVC_SENDEXTRAINFO", "SVC_TIMESCALE",
		"SVC_RESOURCELOCATION", "SVC_SENDCVARVALUE", "SVC_SENDCVARVALUE2",
		"???", "???", "???", "???", "???", // 59-63
		"SelAmmo", "CurWeapon", "Geiger", "Flashlight",
		"FlashBat", "Health", "Damage", "Battery",
		"Train", "HudText", "SayText", "TextMsg",
		"WeaponList", "CustWeapon", "ResetHUD", "InitHUD",
		"CdAudio", "GameTitle", "DeathMsg", "ScoreInfo",
		"TeamInfo", "TeamScore", "GameMode", "MOTD",
		"AmmoPickup", "WeapPickup", "ItemPickup", "HideHUD",
		"SetFOV", "ShowMenu", "ScreenShake", "ScreenFade",
		"AmmoX", "Gib", "Spectator", "TE_CUSTOM",
		"Speaksent", "TimeEnd", "MapList", "CbElec",
		"EndVote", "VoteMenu", "NextMap", "StartSound",
		"SoundList", "ToxicCloud", "ShkFlash", "CreateBlood",
		"GargSplash", "SporeTrail", "TracerDecal", "SRDetonate",
		"SRPrimed", "SRPrimedOff", "RampSprite", "ShieldRic",
		"Playlist", "VGUIMenu", "ServerName", "TeamNames",
		"ServerVer", "ServerBuild", "WeatherFX", "CameraMouse",
		"Fog", "PrtlUpdt", "ASScriptName", "PrintKB",
		"InvAdd", "InvRemove", "Concuss", "ViewMode",
		"Flamethwr", "ClassicMode", "WeaponSpr", "ToggleElem",
		"CustSpr", "NumDisplay", "UpdateNum", "TimeDisplay",
		"UpdateTime", "VModelPos"
	};
#endif

	if (messageType >= 256) {
		uint8_t teType = messageType - 256;
		const char* name = teType < TE_NAMES ? te_names[teType] : "TE_???";
		return name ? name : "TE_???";
	}
	if (messageType < 146) {
#ifdef HLCOOP_BUILD
		return msgTypeStr(messageType);
#else
		return svc_names[messageType];
#endif
		
	}

	return "MSG_" + to_string(messageType);
}

void DemoStats::incTotals() {
	frameCount++;

	entDeltaTotalSz += entDeltaCurrentSz;
	msgTotalSz += msgCurrentSz;
	cmdTotalSz += cmdCurrentSz;
	eventTotalSz += eventCurrentSz;
	usercmdTotalSz += usercmdCurrentSz;

	calcFrameSize();
	totalWriteSz += currentWriteSz;
}

void DemoStats::calcFrameSize() {
	currentWriteSz = entDeltaCurrentSz + msgCurrentSz
		+ cmdCurrentSz + eventCurrentSz + usercmdCurrentSz + sizeof(DemoFrame);
}

const char* formatSize(uint32_t bytes) {
	if (bytes >= 1024 * 1024 * 10) {
		return UTIL_VarArgs("%4u MB", bytes / (1024 * 1024));
	}
	else if (bytes >= 1024 * 10) {
		return UTIL_VarArgs("%4u KB", bytes / 1024);
	}
	else {
		return UTIL_VarArgs("%4u B", bytes);
	}
}

struct DeltaStat {
	string field;
	int bytes;
};

#define ADD_DELTA_STAT(vec, statArray, flag) {\
	DeltaStat stat; \
	stat.field = string( #flag ).substr(strlen("FL_DELTA_")); \
	stat.bytes = statArray[bitoffset(flag)]; \
	if (stat.bytes > 0) \
		vec.push_back(stat); \
}

#define ADD_CAT_DELTA_STAT(vec, statArray, flag, skipPrefix) {\
	DeltaStat stat; \
	stat.field = string( #flag ).substr(strlen(skipPrefix)); \
	stat.bytes = (statArray[flag]+7) / 8; \
	if (stat.bytes > 0) \
		vec.push_back(stat); \
}

bool compareByBytes(const DeltaStat& a, const DeltaStat& b)
{
	return a.bytes > b.bytes;
}

void DemoStats::showStats(edict_t* edt) {
	hudtextparms_t params;
	memset(&params, 0, sizeof(params));

	CBaseEntity* ent = CBaseEntity::Instance(edt);

	params.x = 0.5;
	params.y = 1;
	params.r1 = 255; params.g1 = 255; params.b1 = 255; params.a1 = 255;
	params.channel = 0;
	params.holdTime = 1.0f;

	string hdrTotal = formatSize(g_stats.frameCount * sizeof(DemoFrame));
	string entTotal = formatSize(g_stats.entDeltaTotalSz);
	string msgTotal = formatSize(g_stats.msgTotalSz);
	string evTotal = formatSize(g_stats.eventTotalSz);
	string cmdTotal = formatSize(g_stats.cmdTotalSz);
	string usrTotal = formatSize(g_stats.usercmdTotalSz);
	string totalSz = formatSize(g_stats.totalWriteSz);

	bool recording = !g_demoPlayer->isPlaying();
	string txt;
	
	if (recording)
		txt = UTIL_VarArgs("Demo (%s, %s, %d+%d ms):\n", totalSz.c_str(),
			formatTime(g_sventv->getRecordingTime() / 1000).c_str(), g_copyTime, g_thinkTime);
	else
		txt = UTIL_VarArgs("Demo (%s):\n", totalSz.c_str());

	txt += UTIL_VarArgs("ent: %s (%d)\n", entTotal.c_str(), g_stats.entDeltaCurrentSz);
	txt += UTIL_VarArgs("hdr: %s (%d, %d, %d)\n", hdrTotal.c_str(), g_stats.giantFrameCount, g_stats.bigFrameCount, g_stats.frameCount- g_stats.bigFrameCount);
	txt += UTIL_VarArgs("msg: %s (%d)\n", msgTotal.c_str(), g_stats.msgCurrentSz);
	txt += UTIL_VarArgs("evt: %s (%d)\n", evTotal.c_str(), g_stats.eventCurrentSz);
	txt += UTIL_VarArgs("cmd: %s (%d)\n", cmdTotal.c_str(), g_stats.cmdCount);
	txt += UTIL_VarArgs("usr: %s (%d)\n", usrTotal.c_str(), g_stats.usercmdCount);

	if (recording) {
		txt += UTIL_VarArgs("str: %d / %d\n", g_stringpool_idx, STRING_POOL_SIZE);
	}

	UTIL_HudMessage(ent, params, txt.c_str(), MSG_ONE_UNRELIABLE);

	{
		vector<DeltaStat> deltaStats;
		ADD_CAT_DELTA_STAT(deltaStats, g_stats.entDeltaCatSz, FL_DELTA_CAT_EDFLAG, "FL_DELTA_CAT_");
		ADD_CAT_DELTA_STAT(deltaStats, g_stats.entDeltaCatSz, FL_DELTA_CAT_ORIGIN, "FL_DELTA_CAT_");
		ADD_CAT_DELTA_STAT(deltaStats, g_stats.entDeltaCatSz, FL_DELTA_CAT_ANGLES, "FL_DELTA_CAT_");
		ADD_CAT_DELTA_STAT(deltaStats, g_stats.entDeltaCatSz, FL_DELTA_CAT_DISPLAY, "FL_DELTA_CAT_");
		ADD_CAT_DELTA_STAT(deltaStats, g_stats.entDeltaCatSz, FL_DELTA_CAT_MISC, "FL_DELTA_CAT_");
		ADD_CAT_DELTA_STAT(deltaStats, g_stats.entDeltaCatSz, FL_DELTA_CAT_INTERNAL, "FL_DELTA_CAT_");
		ADD_CAT_DELTA_STAT(deltaStats, g_stats.entDeltaCatSz, FL_DELTA_CAT_PLAYER, "FL_DELTA_CAT_");

		DeltaStat indexStat;
		indexStat.field = "indexes";
		indexStat.bytes = (g_stats.entIndexTotalSz+7) / 8;
		deltaStats.push_back(indexStat);

		uint32_t sum = 0;
		for (int i = 0; i < (int)deltaStats.size(); i++) {
			sum += deltaStats[i].bytes;
		}

		DeltaStat headerStat;
		headerStat.field = "headers";
		headerStat.bytes = g_stats.entDeltaTotalSz - sum;
		deltaStats.push_back(headerStat);
		sum += headerStat.bytes;

		std::sort(deltaStats.begin(), deltaStats.end(), compareByBytes);

		string sumStr = formatSize(sum);
		txt = UTIL_VarArgs("ent deltas (%u, %s):\n", g_stats.entUpdateCount, sumStr.c_str());
		for (int i = 0; i < (int)deltaStats.size() && i < 10; i++) {
			txt += string(formatSize(deltaStats[i].bytes)) + " " + deltaStats[i].field + "\n";
		}

		params.x = 0;
		params.y = 0;
		params.channel = 1;
		UTIL_HudMessage(ent, params, txt.c_str(), MSG_ONE_UNRELIABLE);
	}

	{
		vector<DeltaStat> deltaStats;
		ADD_CAT_DELTA_STAT(deltaStats, g_stats.usercmdSz, DELTA_CAT_USERCMD_LERP, "DELTA_CAT_USERCMD_");
		ADD_CAT_DELTA_STAT(deltaStats, g_stats.usercmdSz, DELTA_CAT_USERCMD_MSEC, "DELTA_CAT_USERCMD_");
		ADD_CAT_DELTA_STAT(deltaStats, g_stats.usercmdSz, DELTA_CAT_USERCMD_ANGLES, "DELTA_CAT_USERCMD_");
		ADD_CAT_DELTA_STAT(deltaStats, g_stats.usercmdSz, DELTA_CAT_USERCMD_MOVE, "DELTA_CAT_USERCMD_");
		ADD_CAT_DELTA_STAT(deltaStats, g_stats.usercmdSz, DELTA_CAT_USERCMD_LIGHTLEVEL, "DELTA_CAT_USERCMD_");
		ADD_CAT_DELTA_STAT(deltaStats, g_stats.usercmdSz, DELTA_CAT_USERCMD_BUTTONS, "DELTA_CAT_USERCMD_");
		ADD_CAT_DELTA_STAT(deltaStats, g_stats.usercmdSz, DELTA_CAT_USERCMD_IMPULSE, "DELTA_CAT_USERCMD_");
		ADD_CAT_DELTA_STAT(deltaStats, g_stats.usercmdSz, DELTA_CAT_USERCMD_WEAPON, "DELTA_CAT_USERCMD_");

		uint32_t sum = 0;
		for (int i = 0; i < (int)deltaStats.size(); i++) {
			sum += deltaStats[i].bytes;
		}

		DeltaStat headerStat;
		headerStat.field = "headers";
		headerStat.bytes = g_stats.usercmdTotalSz - sum;
		deltaStats.push_back(headerStat);
		sum += headerStat.bytes;

		std::sort(deltaStats.begin(), deltaStats.end(), compareByBytes);

		string sumStr = formatSize(sum);
		txt = UTIL_VarArgs("usercmd deltas (%u, %s):\n", g_stats.usercmdCount, sumStr.c_str());
		for (int i = 0; i < (int)deltaStats.size() && i < 10; i++) {
			txt += string(formatSize(deltaStats[i].bytes)) + " " + deltaStats[i].field + "\n";
		}

		params.x = 0.8f;
		params.y = 0;
		params.channel = 2;
		UTIL_HudMessage(ent, params, txt.c_str(), MSG_ONE_UNRELIABLE);
	}

	{
		vector<DeltaStat> deltaStats;
		for (int i = 0; i < 512; i++) {
			DeltaStat stat;
			stat.field = getMessageName(i);
			stat.bytes = g_stats.msgSz[i];
			if (i == SVC_TEMPENTITY)
				continue; // broken down into specific types later
			if (stat.bytes > 0)
				deltaStats.push_back(stat);
		}

		uint32_t sum = 0;
		for (int i = 0; i < (int)deltaStats.size(); i++) {
			sum += deltaStats[i].bytes;
		}

		DeltaStat headerStat;
		headerStat.field = "headers";
		headerStat.bytes = g_stats.msgTotalSz - sum;
		deltaStats.push_back(headerStat);
		sum += headerStat.bytes;

		std::sort(deltaStats.begin(), deltaStats.end(), compareByBytes);

		string sumStr = formatSize(sum);
		txt = UTIL_VarArgs("msg data (%d, %s):\n", g_stats.msgCount, sumStr.c_str());
		for (int i = 0; i < (int)deltaStats.size() && i < 10; i++) {
			//txt += string(formatSize(deltaStats[i].bytes)) + " " + deltaStats[i].field + "\n";
			txt += string(formatSize(deltaStats[i].bytes)) + " " + deltaStats[i].field + "\n";
		}

		params.x = 0;
		params.y = 1;
		params.channel = 3;
		UTIL_HudMessage(ent, params, txt.c_str(), MSG_ONE_UNRELIABLE);
	}
	{
		vector<DeltaStat> deltaStats;
		for (int i = 0; i < 256; i++) {
			DeltaStat stat;
			stat.field = "Event_" + to_string(i);
			stat.bytes = g_stats.evtSize[i];
			if (i == SVC_TEMPENTITY)
				continue; // broken down into specific types later
			if (stat.bytes > 0)
				deltaStats.push_back(stat);
		}

		uint32_t sum = 0;
		for (int i = 0; i < (int)deltaStats.size(); i++) {
			sum += deltaStats[i].bytes;
		}

		DeltaStat headerStat;
		headerStat.field = "headers";
		headerStat.bytes = g_stats.eventTotalSz - sum;
		deltaStats.push_back(headerStat);
		sum += headerStat.bytes;

		std::sort(deltaStats.begin(), deltaStats.end(), compareByBytes);

		string sumStr = formatSize(sum);
		txt = UTIL_VarArgs("event data (%d, %s):\n", g_stats.eventCount, sumStr.c_str());
		for (int i = 0; i < (int)deltaStats.size() && i < 10; i++) {
			//txt += string(formatSize(deltaStats[i].bytes)) + " " + deltaStats[i].field + "\n";
			txt += string(formatSize(deltaStats[i].bytes)) + " " + deltaStats[i].field + "\n";
		}

		params.x = 0.8;
		params.y = -1;
		params.channel = -1;
		params.holdTime = 0.05f;
		params.fadeinTime = 0.025f;
		params.fadeoutTime = 0.025f;
		UTIL_HudMessage(ent, params, txt.c_str(), MSG_ONE_UNRELIABLE);
	}

	/*
	{
		static const char* cond_names[] = {
			... TODO: update for latest ordering
		};

		vector<DeltaStat> deltaStats;
		for (int i = 0; i < 32; i++) {
			int bit = 1 << i;
			if (bit == bits_COND_TASK_FAILED || bit == bits_COND_LIGHT_DAMAGE || bit == bits_COND_ENEMY_OCCLUDED 
				|| bit == bits_COND_SEE_ENEMY || bit == bits_COND_SEE_CLIENT || bit == bits_COND_SEE_DISLIKE
				|| bit == bits_COND_SEE_HATE || bit == bits_COND_ENEMY_TOOFAR || bit == bits_COND_ENEMY_FACING_ME)
				continue;

			DeltaStat stat;
			stat.field = cond_names[i];
			stat.bytes = g_stats.entCondSz[i];
			if (stat.bytes > 0)
				deltaStats.push_back(stat);
		}

		std::sort(deltaStats.begin(), deltaStats.end(), compareByBytes);

		txt = "cond count:\n";
		for (int i = 0; i < (int)deltaStats.size() && i < 10; i++) {
			//txt += string(formatSize(deltaStats[i].bytes)) + " " + deltaStats[i].field + "\n";
			txt += string(formatSize(deltaStats[i].bytes)) + " " + deltaStats[i].field + "\n";
		}

		params.x = 0.5;
		params.y = 0;
		params.channel = -1;
		params.holdTime = 0.05f;
		params.fadeinTime = 0.025f;
		params.fadeoutTime = 0.025f;
		UTIL_HudMessage(ent, params, txt.c_str(), MSG_ONE_UNRELIABLE);
	}
	*/
}

int bitoffset(uint32_t flag) {
	int bitOffset = 0;

	while (flag) {
		flag >>= 1;
		bitOffset++;
	}

	return bitOffset;
}