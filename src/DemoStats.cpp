#include "DemoStats.h"
#include "main.h"
#include "netedict.h"
#include "DemoPlayerEnt.h"
#include "DemoFile.h"

string getMessageName(int messageType) {
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

	if (messageType >= 256) {
		uint8_t teType = messageType - 256;
		const char* name = teType < TE_NAMES ? te_names[teType] : "TE_???";
		return name ? name : "TE_???";
	}
	if (messageType < 146) {
		return svc_names[messageType];
	}

	return "MSG_" + to_string(messageType);
}

void DemoStats::incTotals() {
	frameCount++;

	entDeltaTotalSz += entDeltaCurrentSz;
	plrDeltaTotalSz += plrDeltaCurrentSz;
	msgTotalSz += msgCurrentSz;
	cmdTotalSz += cmdCurrentSz;
	eventTotalSz += eventCurrentSz;

	plrDeltaCount += plrDeltaCurrentSz != 0;

	calcFrameSize();
	totalWriteSz += currentWriteSz;
}

void DemoStats::calcFrameSize() {
	currentWriteSz = entDeltaCurrentSz + plrDeltaCurrentSz + msgCurrentSz
		+ cmdCurrentSz + eventCurrentSz + sizeof(DemoFrame);
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

bool compareByBytes(const DeltaStat& a, const DeltaStat& b)
{
	return a.bytes > b.bytes;
}

void DemoStats::showStats(edict_t* ent) {
	hudtextparms_t params;
	memset(&params, 0, sizeof(params));

	params.x = 0.5;
	params.y = 1;
	params.r1 = 255; params.g1 = 255; params.b1 = 255; params.a1 = 255;
	params.channel = 0;
	params.holdTime = 1.0f;

	string hdrTotal = formatSize(g_stats.frameCount * sizeof(DemoFrame));
	string entTotal = formatSize(g_stats.entDeltaTotalSz);
	string plrTotal = formatSize(g_stats.plrDeltaTotalSz);
	string msgTotal = formatSize(g_stats.msgTotalSz);
	string evTotal = formatSize(g_stats.eventTotalSz);
	string cmdTotal = formatSize(g_stats.cmdTotalSz);
	string totalSz = formatSize(g_stats.totalWriteSz);

	string txt = UTIL_VarArgs("Demo (%s, %u):\n", totalSz.c_str(), g_stats.currentWriteSz);
	txt += UTIL_VarArgs("ent: %s (%d)\n", entTotal.c_str(), g_stats.entDeltaCurrentSz);
	txt += UTIL_VarArgs("hdr: %s (%d, %d)\n", hdrTotal.c_str(), g_stats.bigFrameCount, g_stats.frameCount- g_stats.bigFrameCount);
	txt += UTIL_VarArgs("plr: %s (%d)\n", plrTotal.c_str(), g_stats.plrDeltaCurrentSz);
	txt += UTIL_VarArgs("msg: %s (%d)\n", msgTotal.c_str(), g_stats.msgCurrentSz);
	txt += UTIL_VarArgs("evt: %s (%d)\n", evTotal.c_str(), g_stats.eventCurrentSz);
	txt += UTIL_VarArgs("cmd: %s (%d)\n", cmdTotal.c_str(), g_stats.cmdCount);

	HudMessage(ent, params, txt.c_str(), MSG_ONE_UNRELIABLE);

	{
		vector<DeltaStat> deltaStats;
		ADD_DELTA_STAT(deltaStats, g_stats.entDeltaSz, FL_DELTA_ORIGIN_X);
		ADD_DELTA_STAT(deltaStats, g_stats.entDeltaSz, FL_DELTA_ORIGIN_Y);
		ADD_DELTA_STAT(deltaStats, g_stats.entDeltaSz, FL_DELTA_ORIGIN_Z);
		ADD_DELTA_STAT(deltaStats, g_stats.entDeltaSz, FL_DELTA_ANGLES_X);
		ADD_DELTA_STAT(deltaStats, g_stats.entDeltaSz, FL_DELTA_ANGLES_Y);
		ADD_DELTA_STAT(deltaStats, g_stats.entDeltaSz, FL_DELTA_ANGLES_Z);
		ADD_DELTA_STAT(deltaStats, g_stats.entDeltaSz, FL_DELTA_MODELINDEX);
		ADD_DELTA_STAT(deltaStats, g_stats.entDeltaSz, FL_DELTA_SKIN);
		ADD_DELTA_STAT(deltaStats, g_stats.entDeltaSz, FL_DELTA_BODY);
		ADD_DELTA_STAT(deltaStats, g_stats.entDeltaSz, FL_DELTA_EFFECTS);
		ADD_DELTA_STAT(deltaStats, g_stats.entDeltaSz, FL_DELTA_SEQUENCE);
		ADD_DELTA_STAT(deltaStats, g_stats.entDeltaSz, FL_DELTA_GAITBLEND);
		ADD_DELTA_STAT(deltaStats, g_stats.entDeltaSz, FL_DELTA_FRAME);
		ADD_DELTA_STAT(deltaStats, g_stats.entDeltaSz, FL_DELTA_FRAMERATE);
		ADD_DELTA_STAT(deltaStats, g_stats.entDeltaSz, FL_DELTA_CONTROLLER_LO);
		ADD_DELTA_STAT(deltaStats, g_stats.entDeltaSz, FL_DELTA_CONTROLLER_HI);
		ADD_DELTA_STAT(deltaStats, g_stats.entDeltaSz, FL_DELTA_SCALE);
		ADD_DELTA_STAT(deltaStats, g_stats.entDeltaSz, FL_DELTA_RENDERMODEFX);
		ADD_DELTA_STAT(deltaStats, g_stats.entDeltaSz, FL_DELTA_RENDERAMT);
		ADD_DELTA_STAT(deltaStats, g_stats.entDeltaSz, FL_DELTA_RENDERCOLOR_0);
		ADD_DELTA_STAT(deltaStats, g_stats.entDeltaSz, FL_DELTA_RENDERCOLOR_1);
		ADD_DELTA_STAT(deltaStats, g_stats.entDeltaSz, FL_DELTA_RENDERCOLOR_2);
		ADD_DELTA_STAT(deltaStats, g_stats.entDeltaSz, FL_DELTA_AIMENT);
		ADD_DELTA_STAT(deltaStats, g_stats.entDeltaSz, FL_DELTA_HEALTH);
		ADD_DELTA_STAT(deltaStats, g_stats.entDeltaSz, FL_DELTA_COLORMAP);
		ADD_DELTA_STAT(deltaStats, g_stats.entDeltaSz, FL_DELTA_CLASSIFY);

		DeltaStat indexStat;
		indexStat.field = "indexes";
		indexStat.bytes = g_stats.entIndexTotalSz;
		deltaStats.push_back(indexStat);

		uint32_t sum = 0;
		for (int i = 0; i < deltaStats.size(); i++) {
			sum += deltaStats[i].bytes;
		}

		DeltaStat headerStat;
		headerStat.field = "headers";
		headerStat.bytes = g_stats.entDeltaTotalSz - (sum);
		deltaStats.push_back(headerStat);

		std::sort(deltaStats.begin(), deltaStats.end(), compareByBytes);

		string sumStr = formatSize(sum);
		uint32_t smallUpdates = g_stats.entUpdateCount - (g_stats.entBigUpdates+g_stats.entMedUpdates);
		txt = UTIL_VarArgs("ent deltas (%u, %u, %u, %s):\n", g_stats.entBigUpdates, g_stats.entMedUpdates,
			smallUpdates, sumStr.c_str());
		for (int i = 0; i < deltaStats.size() && i < 10; i++) {
			txt += string(formatSize(deltaStats[i].bytes)) + " " + deltaStats[i].field + "\n";
		}

		params.x = 0;
		params.y = 0;
		params.channel = 1;
		HudMessage(ent, params, txt.c_str(), MSG_ONE_UNRELIABLE);
	}

	{
		vector<DeltaStat> deltaStats;
		ADD_DELTA_STAT(deltaStats, g_stats.entDeltaBigReason, FL_DELTA_ORIGIN_X);
		ADD_DELTA_STAT(deltaStats, g_stats.entDeltaBigReason, FL_DELTA_ORIGIN_Y);
		ADD_DELTA_STAT(deltaStats, g_stats.entDeltaBigReason, FL_DELTA_ORIGIN_Z);
		ADD_DELTA_STAT(deltaStats, g_stats.entDeltaBigReason, FL_DELTA_ANGLES_X);
		ADD_DELTA_STAT(deltaStats, g_stats.entDeltaBigReason, FL_DELTA_ANGLES_Y);
		ADD_DELTA_STAT(deltaStats, g_stats.entDeltaBigReason, FL_DELTA_ANGLES_Z);
		ADD_DELTA_STAT(deltaStats, g_stats.entDeltaBigReason, FL_DELTA_MODELINDEX);
		ADD_DELTA_STAT(deltaStats, g_stats.entDeltaBigReason, FL_DELTA_SKIN);
		ADD_DELTA_STAT(deltaStats, g_stats.entDeltaBigReason, FL_DELTA_BODY);
		ADD_DELTA_STAT(deltaStats, g_stats.entDeltaBigReason, FL_DELTA_EFFECTS);
		ADD_DELTA_STAT(deltaStats, g_stats.entDeltaBigReason, FL_DELTA_SEQUENCE);
		ADD_DELTA_STAT(deltaStats, g_stats.entDeltaBigReason, FL_DELTA_GAITBLEND);
		ADD_DELTA_STAT(deltaStats, g_stats.entDeltaBigReason, FL_DELTA_FRAME);
		ADD_DELTA_STAT(deltaStats, g_stats.entDeltaBigReason, FL_DELTA_FRAMERATE);
		ADD_DELTA_STAT(deltaStats, g_stats.entDeltaBigReason, FL_DELTA_CONTROLLER_LO);
		ADD_DELTA_STAT(deltaStats, g_stats.entDeltaBigReason, FL_DELTA_CONTROLLER_HI);
		ADD_DELTA_STAT(deltaStats, g_stats.entDeltaBigReason, FL_DELTA_SCALE);
		ADD_DELTA_STAT(deltaStats, g_stats.entDeltaBigReason, FL_DELTA_RENDERMODEFX);
		ADD_DELTA_STAT(deltaStats, g_stats.entDeltaBigReason, FL_DELTA_RENDERAMT);
		ADD_DELTA_STAT(deltaStats, g_stats.entDeltaBigReason, FL_DELTA_RENDERCOLOR_0);
		ADD_DELTA_STAT(deltaStats, g_stats.entDeltaBigReason, FL_DELTA_RENDERCOLOR_1);
		ADD_DELTA_STAT(deltaStats, g_stats.entDeltaBigReason, FL_DELTA_RENDERCOLOR_2);
		ADD_DELTA_STAT(deltaStats, g_stats.entDeltaBigReason, FL_DELTA_AIMENT);
		ADD_DELTA_STAT(deltaStats, g_stats.entDeltaBigReason, FL_DELTA_HEALTH);
		ADD_DELTA_STAT(deltaStats, g_stats.entDeltaBigReason, FL_DELTA_COLORMAP);
		ADD_DELTA_STAT(deltaStats, g_stats.entDeltaBigReason, FL_DELTA_CLASSIFY);

		std::sort(deltaStats.begin(), deltaStats.end(), compareByBytes);

		txt = UTIL_VarArgs("big ent delta reason:\n");
		for (int i = 0; i < deltaStats.size() && i < 10; i++) {
			txt += string(formatSize(deltaStats[i].bytes)) + " " + deltaStats[i].field + "\n";
		}

		params.x = 0.5f;
		params.y = 0;
		params.channel = 4;
		HudMessage(ent, params, txt.c_str(), MSG_ONE_UNRELIABLE);
	}

	{
		vector<DeltaStat> deltaStats;
		ADD_DELTA_STAT(deltaStats, g_stats.plrDeltaSz, FL_DELTA_FLAGS);
		ADD_DELTA_STAT(deltaStats, g_stats.plrDeltaSz, FL_DELTA_NAME);
		ADD_DELTA_STAT(deltaStats, g_stats.plrDeltaSz, FL_DELTA_MODEL);
		ADD_DELTA_STAT(deltaStats, g_stats.plrDeltaSz, FL_DELTA_STEAMID);
		ADD_DELTA_STAT(deltaStats, g_stats.plrDeltaSz, FL_DELTA_COLORS);
		ADD_DELTA_STAT(deltaStats, g_stats.plrDeltaSz, FL_DELTA_PING);
		ADD_DELTA_STAT(deltaStats, g_stats.plrDeltaSz, FL_DELTA_PMMOVE);
		ADD_DELTA_STAT(deltaStats, g_stats.plrDeltaSz, FL_DELTA_PUNCHANGLE_X);
		ADD_DELTA_STAT(deltaStats, g_stats.plrDeltaSz, FL_DELTA_PUNCHANGLE_Y);
		ADD_DELTA_STAT(deltaStats, g_stats.plrDeltaSz, FL_DELTA_PUNCHANGLE_Z);
		ADD_DELTA_STAT(deltaStats, g_stats.plrDeltaSz, FL_DELTA_VIEWMODEL);
		ADD_DELTA_STAT(deltaStats, g_stats.plrDeltaSz, FL_DELTA_WEAPONMODEL);
		ADD_DELTA_STAT(deltaStats, g_stats.plrDeltaSz, FL_DELTA_WEAPONANIM);
		ADD_DELTA_STAT(deltaStats, g_stats.plrDeltaSz, FL_DELTA_ARMORVALUE);
		ADD_DELTA_STAT(deltaStats, g_stats.plrDeltaSz, FL_DELTA_BUTTON);
		ADD_DELTA_STAT(deltaStats, g_stats.plrDeltaSz, FL_DELTA_VIEWOFS);
		ADD_DELTA_STAT(deltaStats, g_stats.plrDeltaSz, FL_DELTA_FRAGS);
		ADD_DELTA_STAT(deltaStats, g_stats.plrDeltaSz, FL_DELTA_FOV);
		ADD_DELTA_STAT(deltaStats, g_stats.plrDeltaSz, FL_DELTA_CLIP);
		ADD_DELTA_STAT(deltaStats, g_stats.plrDeltaSz, FL_DELTA_CLIP2);
		ADD_DELTA_STAT(deltaStats, g_stats.plrDeltaSz, FL_DELTA_AMMO);
		ADD_DELTA_STAT(deltaStats, g_stats.plrDeltaSz, FL_DELTA_AMMO2);
		ADD_DELTA_STAT(deltaStats, g_stats.plrDeltaSz, FL_DELTA_OBSERVER);

		uint32_t sum = 0;
		for (int i = 0; i < deltaStats.size(); i++) {
			sum += deltaStats[i].bytes;
		}

		DeltaStat headerStat;
		headerStat.field = "headers";
		headerStat.bytes = g_stats.plrDeltaTotalSz - sum;
		deltaStats.push_back(headerStat);

		std::sort(deltaStats.begin(), deltaStats.end(), compareByBytes);

		string sumStr = formatSize(sum);
		txt = UTIL_VarArgs("plr deltas (%s):\n", sumStr.c_str());
		for (int i = 0; i < deltaStats.size() && i < 10; i++) {
			txt += string(formatSize(deltaStats[i].bytes)) + " " + deltaStats[i].field + "\n";
		}

		params.x = 0;
		params.y = -1;
		params.channel = 2;
		HudMessage(ent, params, txt.c_str(), MSG_ONE_UNRELIABLE);
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
		for (int i = 0; i < deltaStats.size(); i++) {
			sum += deltaStats[i].bytes;
		}

		DeltaStat headerStat;
		headerStat.field = "headers";
		headerStat.bytes = g_stats.msgTotalSz - sum;
		deltaStats.push_back(headerStat);

		std::sort(deltaStats.begin(), deltaStats.end(), compareByBytes);

		string sumStr = formatSize(sum);
		txt = UTIL_VarArgs("msg data (%d, %s):\n", g_stats.msgCount, sumStr.c_str());
		for (int i = 0; i < deltaStats.size() && i < 10; i++) {
			//txt += string(formatSize(deltaStats[i].bytes)) + " " + deltaStats[i].field + "\n";
			txt += string(formatSize(deltaStats[i].bytes)) + " " + deltaStats[i].field + "\n";
		}

		params.x = 0;
		params.y = 1;
		params.channel = 3;
		HudMessage(ent, params, txt.c_str(), MSG_ONE_UNRELIABLE);
	}
}

int bitoffset(uint32_t flag) {
	int bitOffset = 0;

	while (flag) {
		flag >>= 1;
		bitOffset++;
	}

	return bitOffset;
}