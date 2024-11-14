#include "main.h"
#include <string>
#include "ThreadSafeInt.h"
#include "SvenTV.h"

#ifndef HLCOOP_BUILD
#include "mmlib.h"
#include "misc_utils.h"

// Description of plugin
plugin_info_t Plugin_info = {
	META_INTERFACE_VERSION,	// ifvers
	"SvenTV",	// name
	"1.0",	// version
	__DATE__,	// date
	"w00tguy",	// author
	"https://github.com/wootguy/",	// url
	"SVENTV",	// logtag, all caps please
	PT_ANYTIME,	// (when) loadable
	PT_ANYPAUSE,	// (when) unloadable
};

#define DEFAULT_HOOK_RETURN RETURN_META(MRES_IGNORED)

#define isValidPlayer IsValidPlayer
#else

bool g_compressMessages = false;
bool g_userInfoDirty[33];

#define DEFAULT_HOOK_RETURN return HOOK_CONTINUE

bool cgetline(FILE* file, string& output) {
	static char buffer[4096];

	if (fgets(buffer, sizeof(buffer), file)) {
		output = string(buffer);
		if (output[output.length() - 1] == '\n') {
			output = output.substr(0, output.length() - 1);
		}
		return true;
	}

	return false;
}
#endif

volatile bool g_plugin_exiting = false;
const bool singleThreadMode = true;

SvenTV* g_sventv = NULL;
DemoPlayer* g_demoPlayer = NULL;
NetMessageData* g_netmessages = NULL;
int g_netmessage_count = 0;
DemoUserCmdData* g_usercmds[32];
int g_usercmd_count[32];
CommandData* g_cmds = NULL;
int g_command_count = 0;
DemoEventData* g_events = NULL;
int g_event_count = 0;
uint32_t g_server_frame_count = 0;
int g_copyTime = 0;
volatile int g_thinkTime = 0;
bool g_should_write_next_message = false;
bool g_pause_message_logging = false;
bool g_can_autostart_demo = true;

PlayerSpray g_playerSprays[32];

char g_stringpool[STRING_POOL_SIZE]; // pool for storing misc strings used by the current map
uint16_t g_stringpool_idx = 1; // points to the end of the string data, 0 index is for NULL strings
unordered_map<string_t, uint16_t> g_stringtToClassIdx; // maps a string_t to an offset in g_stringpool


// maps indexes to model names, for all models that were used so far in this map
set<string> g_playerModels; // all player model names used during the game

cvar_t* g_auto_demo_file;
cvar_t* g_min_storage_megabytes; // minimum megabytes left on storage for demos to start recording
cvar_t* g_max_demo_megabytes; // max size of a demo before aborting
cvar_t* g_demo_file_path;
cvar_t* g_validate_output;
cvar_t* g_compress_demos; // compress demos with lzma after writing
cvar_t* g_write_debug_info; // write debugging info to the demo files (monster state, health, etc.)
cvar_t* g_write_user_cmds; // write user commands (movement, buttons, etc.)

DemoStats g_stats;
bool demoStatPlayers[33] = { false };

#define EVT_GAUSS_CHARGE 13

struct GaussChargeEvt {
	float time;
	int charge;
};

// gauss charging spams events too much. Use this to slow them down
GaussChargeEvt lastGaussCharge[33];

const char* te_names[TE_NAMES] = {
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

uint16_t getPoolOffsetForString(string_t classname) {
	auto item = g_stringtToClassIdx.find(classname);

	if (item != g_stringtToClassIdx.end()) {
		return item->second;
	}

	int len = strlen(STRING(classname));
	if (len + g_stringpool_idx + 1 >= STRING_POOL_SIZE) {
		ALERT(at_console, "Overflowed string pool!\n");
		return 0;
	}

	uint16_t ret = g_stringpool_idx;
	g_stringtToClassIdx[classname] = g_stringpool_idx;

	strncpy(g_stringpool + g_stringpool_idx, STRING(classname), len+1);
	g_stringpool_idx += len + 1;

	return ret;
}

std::string formatTime(int seconds, bool forceHours) {
	int hours = seconds / (60 * 60);
	int minutes = (seconds - (hours * 60 * 60)) / 60;
	int s = seconds % 60;

	if (hours > 0) {
		return UTIL_VarArgs("%02d:%02d:%02d", hours, minutes, s);
	}
	else {
		return UTIL_VarArgs("%02d:%02d", minutes, s);
	}
}

HOOK_RET_VOID ClientLeaveHook(CBasePlayer* ent) {
	demoStatPlayers[ent->entindex()] = false;
	
	DEFAULT_HOOK_RETURN;
}

HOOK_RET_VOID Changelevel() {
	if (g_sventv) {
		g_sventv->enableDemoFile = false;
		g_demoPlayer->stopReplay();
		g_server_frame_count = 0;
		g_netmessage_count = 0;
		g_server_frame_count = 0;
		g_playerModels.clear();
	}

	memset(g_playerSprays, 0, sizeof(PlayerSpray)*32);

	DEFAULT_HOOK_RETURN;
}

#ifdef HLCOOP_BUILD
HOOK_RET_VOID MapInitHook()
#else
HOOK_RET_VOID MapInitHook(edict_t * pEdictList, int edictCount, int maxClients)
#endif
{
	if (!g_sventv) {
		g_sventv = new SvenTV(singleThreadMode);
		g_demoPlayer = new DemoPlayer();
	}
	//g_demoPlayer->precacheLastDemo();
	memset(demoStatPlayers, 0, sizeof(demoStatPlayers));
	memset(lastGaussCharge, 0, sizeof(GaussChargeEvt) * MAX_PLAYERS);
	g_can_autostart_demo = true;
	
	memset(g_stringpool, 0, STRING_POOL_SIZE);
	g_stringpool_idx = 1; // 0 index is for NULL strings
	g_stringtToClassIdx.clear();

	DEFAULT_HOOK_RETURN;
}

#ifdef HLCOOP_BUILD
HOOK_RET_VOID MapInit_post()
#else
HOOK_RET_VOID MapInit_post(edict_t * pEdictList, int edictCount, int maxClients)
#endif
{
#ifndef HLCOOP_BUILD
	loadSoundCacheFile();
#endif

	DEFAULT_HOOK_RETURN;
}

HOOK_RET_VOID ClientUserInfoChangedHook(edict_t* pEntity, char* infobuffer) {
	if (!IsValidPlayer(pEntity)) {
		DEFAULT_HOOK_RETURN;
	}

	g_userInfoDirty[ENTINDEX(pEntity)] = true;

	DEFAULT_HOOK_RETURN;
}

float lastPingUpdate = 0;

// http://forum.tsgk.com/viewtopic.php?t=26238
uint64_t getSteamId64(edict_t* ent) {
	string steamid = g_engfuncs.pfnGetPlayerAuthId(ent);
	vector<string> parts = splitString(steamid, ":");
	if (steamid == "STEAM_ID_LAN") {
		return 1;
	}
	else if (steamid == "BOT") {
		return 2;
	}
	else if (parts.size() != 3) {
		return 0; // indicates invalid ID
	}
	else {
		uint64_t x = atoi(parts[1].c_str());
		uint64_t y = atoi(parts[2].c_str());
		return ((y * 2LLU) - x) + 76561197960265728LLU;
	}
}

void printEdSlots() {
	int edFree = 0;
	int edUsed = 0;
	int edPend = 0;
	edict_t* edicts = ENT(0);
	for (int i = 0; i < gpGlobals->maxEntities; i++) {
		if (edicts[i].free) {
			if (gpGlobals->time - edicts[i].freetime > 0.5) {
				edFree++;
			}
			else {
				edPend++;
			}
		}
		else {
			edUsed++;
		}
	}
	ALERT(at_console, "Edicts: %04d Used, %04d Pending, %04d Free\n", edUsed, edPend, edFree);
}

HOOK_RET_VOID StartFrameHook() {
#ifndef HLCOOP_BUILD
	handleThreadPrints();
	g_Scheduler.Think();
#endif

	g_server_frame_count++;

	if (!g_sventv) {
		DEFAULT_HOOK_RETURN;
	}

	if (!g_sventv->enableDemoFile && g_auto_demo_file->value > 0 && gpGlobals->time > 1.0f && g_can_autostart_demo) {
		g_sventv->enableDemoFile = true;
	}

	static float lastStats = 0;

	if (gpGlobals->time - lastStats >= 0.05f || lastStats > gpGlobals->time) {
		lastStats = gpGlobals->time;

		for (int i = 1; i <= gpGlobals->maxClients; i++) {
			edict_t* ent = INDEXENT(i);
			if (demoStatPlayers[i]) {
				g_pause_message_logging = true;
				g_stats.showStats(ent);
				g_pause_message_logging = false;
			}
		}
	}

	if (g_demoPlayer)
		g_demoPlayer->playDemo();

	if (!g_sventv->enableDemoFile && !g_sventv->enableServer) {
		if (singleThreadMode) {
			g_sventv->think_mainThread();
		}

		DEFAULT_HOOK_RETURN;
	}

	for (int i = 1; i <= gpGlobals->maxClients; i++) {
		edict_t* ent = INDEXENT(i);
		CBasePlayer* plr = (CBasePlayer*)GET_PRIVATE(ent);

		if (!IsValidPlayer(ent) || !plr) {
			continue;
		}
	}

	if (g_sventv) {
		g_sventv->think_mainThread();
	}

	DEFAULT_HOOK_RETURN;
}

HOOK_RET_VOID MessageBegin(int msg_dest, int msg_type, const float* pOrigin, edict_t* ed) {
	//ALERT(at_console, "NET MESG: %d\n", msg_type);
	if (!g_sventv || (!g_sventv->enableDemoFile && !g_sventv->enableServer) || g_pause_message_logging) {
		g_should_write_next_message = false;

		DEFAULT_HOOK_RETURN;
	}

	bool usesOrigin = false;
	switch (msg_dest) {
	case MSG_PVS:
	case MSG_PVS_R:
	case MSG_PAS:
	case MSG_PAS_R:
		usesOrigin = true;
		break;
	default:
		break;
	}

	if (pOrigin && !usesOrigin) {
		//ALERT(at_console, "Message %s sent with origin (dest %d)??\n", msgTypeStr(msg_type), msg_dest);
	}

	if (ed && !(ed->v.flags & FL_CLIENT)) {
		ALERT(at_console, "Message %s sent to non-client\n", msgTypeStr(msg_type));
		DEFAULT_HOOK_RETURN;
	}

	g_should_write_next_message = true;

	NetMessageData& msg = g_netmessages[g_netmessage_count];
	msg.dest = msg_dest;
	msg.type = msg_type;
	msg.targets = 0;
	msg.eidx = 0;
	msg.sz = 0;
	msg.hasLongOrigin = 0;
	msg.hasOrigin = 0;
	memset(msg.origin, 0, sizeof(uint32_t) * 3);

	if (usesOrigin && pOrigin) {
		msg.hasOrigin = 1;
		if (abs(pOrigin[0]) > INT16_MAX || abs(pOrigin[1]) > INT16_MAX || abs(pOrigin[2]) > INT16_MAX) {
			msg.hasLongOrigin = 1;
			msg.origin[0] = FLOAT_TO_FIXED(pOrigin[0], 19, 5);
			msg.origin[1] = FLOAT_TO_FIXED(pOrigin[1], 19, 5);
			msg.origin[2] = FLOAT_TO_FIXED(pOrigin[2], 19, 5);
		}
		else {
			msg.origin[0] = (int16_t)pOrigin[0];
			msg.origin[1] = (int16_t)pOrigin[1];
			msg.origin[2] = (int16_t)pOrigin[2];
		}
	}

	if (ed) {
		msg.eidx = ENTINDEX(ed);
	}

	DEFAULT_HOOK_RETURN;
}

HOOK_RET_VOID MessageEnd() {
	if (g_should_write_next_message) {
		NetMessageData& m1 = g_netmessages[g_netmessage_count];
		
		// individual message?
		if (m1.eidx) {
			// check to see if an identical message was captured already, then merge them
			for (int i = 0; i < g_netmessage_count; i++) {
				NetMessageData& m2 = g_netmessages[i];

				bool matchingHeaders = m1.type == m2.type && m1.sz == m2.sz && m1.dest == m2.dest
					&& m1.origin[0] == m2.origin[0] && m1.origin[1] == m2.origin[1] && m1.origin[2] == m2.origin[2];

				if (matchingHeaders && (m2.eidx || m2.targets) && !memcmp(m1.data, m2.data, m1.sz)) {
					// convert to a multi-target message
					if (m2.eidx) {
						m2.targets |= PLRBIT(m2.eidx);
						m2.eidx = 0;
					}

					// add a new target to the existing message
					m2.targets |= PLRBIT(m1.eidx);
					
					// don't write the duplicate
					DEFAULT_HOOK_RETURN;
				}
			}
		}

		g_netmessage_count++;

		if (g_netmessage_count >= MAX_NETMSG_FRAME) {
			g_netmessage_count--; // overwrite last message
			ALERT(at_console, "Network message capture overflow!\n");
		}
	}
	
	DEFAULT_HOOK_RETURN;
}

HOOK_RET_VOID WriteAngle(float angle) {
	NetMessageData& msg = g_netmessages[g_netmessage_count];
	if (g_should_write_next_message && msg.sz + sizeof(byte) < MAX_NETMSG_DATA) {
		byte dat = (int64)(fmod((double)angle, 360.0) * 256.0 / 360.0) & 0xFF;
		memcpy(msg.data + msg.sz, &dat, sizeof(byte));
		msg.sz += sizeof(byte);
	}
	
	DEFAULT_HOOK_RETURN;
}

HOOK_RET_VOID WriteByte(int b) {
	NetMessageData& msg = g_netmessages[g_netmessage_count];
	if (g_should_write_next_message && msg.sz + sizeof(byte) < MAX_NETMSG_DATA) {
		byte dat = b;
		memcpy(msg.data + msg.sz, &dat, sizeof(byte));
		msg.sz += sizeof(byte);
	}
	
	DEFAULT_HOOK_RETURN;
}

HOOK_RET_VOID WriteChar(int c) {
	NetMessageData& msg = g_netmessages[g_netmessage_count];
	if (g_should_write_next_message && msg.sz + sizeof(byte) < MAX_NETMSG_DATA) {
		byte dat = c;
		memcpy(msg.data + msg.sz, &dat, sizeof(byte));
		msg.sz += sizeof(byte);
	}
	
	DEFAULT_HOOK_RETURN;
}

HOOK_RET_VOID WriteCoord(float coord) {
	NetMessageData& msg = g_netmessages[g_netmessage_count];

#ifdef HLCOOP_BUILD
	if (g_should_write_next_message && msg.sz + sizeof(int16_t) < MAX_NETMSG_DATA) {
		int16_t arg = coord * 8;
		memcpy(msg.data + msg.sz, &arg, sizeof(int16_t));
		msg.sz += sizeof(int16_t);
	}
#else
	if (g_should_write_next_message && msg.sz + sizeof(int32_t) < MAX_NETMSG_DATA) {
		int32_t arg = coord * 8;
		memcpy(msg.data + msg.sz, &arg, sizeof(int32_t));
		msg.sz += sizeof(int32_t);
	}
#endif
	
	DEFAULT_HOOK_RETURN;
}

HOOK_RET_VOID WriteEntity(int ent) {
	NetMessageData& msg = g_netmessages[g_netmessage_count];
	if (g_should_write_next_message && msg.sz + sizeof(uint16_t) < MAX_NETMSG_DATA) {
		uint16_t dat = ent;
		memcpy(msg.data + msg.sz, &dat, sizeof(uint16_t));
		msg.sz += sizeof(uint16_t);
	}
	
	DEFAULT_HOOK_RETURN;
}

HOOK_RET_VOID WriteLong(int val) {
	NetMessageData& msg = g_netmessages[g_netmessage_count];
	if (g_should_write_next_message && msg.sz + sizeof(int) < MAX_NETMSG_DATA) {
		memcpy(msg.data + msg.sz, &val, sizeof(int));
		msg.sz += sizeof(int);
	}
	
	DEFAULT_HOOK_RETURN;
}

HOOK_RET_VOID WriteShort(int val) {
	NetMessageData& msg = g_netmessages[g_netmessage_count];
	if (g_should_write_next_message && msg.sz + sizeof(int16_t) < MAX_NETMSG_DATA) {
		int16_t dat = val;
		memcpy(msg.data + msg.sz, &dat, sizeof(int16_t));
		msg.sz += sizeof(int16_t);
	}
	
	DEFAULT_HOOK_RETURN;
}

HOOK_RET_VOID WriteString(const char* s) {
	NetMessageData& msg = g_netmessages[g_netmessage_count];
	int len = strlen(s)+1;
	if (g_should_write_next_message && msg.sz + len < MAX_NETMSG_DATA) {
		memcpy(msg.data + msg.sz, s, len);
		msg.sz += len;
	}
	
	DEFAULT_HOOK_RETURN;
}

bool replay_demo(CBasePlayer* plr, const CommandArgs& args) {
	string path = g_demo_file_path->string + args.ArgV(2);
	if (args.ArgV(2).empty()) {
		path += string(STRING(gpGlobals->mapname)) + ".demo";
	}

	float offsetSeconds = 0;
	if (args.ArgC() > 1) {
		string offsetArg = args.ArgV(1);

		if (offsetArg.find(":") != string::npos) {
			vector<string> parts = splitString(offsetArg, ":");

			if (parts.size() > 2) {
				int hours = atoi(parts[0].c_str());
				int mins = atoi(parts[1].c_str());
				offsetSeconds = atoi(parts[2].c_str()) + mins*60 + hours*60*60;
			}
			else if (parts.size() == 2) {
				int mins = atoi(parts[0].c_str());
				offsetSeconds = atoi(parts[1].c_str()) + mins*60;
			}
		}
		else {
			offsetSeconds = atof(offsetArg.c_str());
		}
	}

	g_demoPlayer->stopReplay();
	g_demoPlayer->prepareDemo();
	g_demoPlayer->openDemo(plr, path, offsetSeconds, true);
	
	g_sventv->enableDemoFile = false;

	return true;
}

bool record_demo(CBasePlayer* plr, const CommandArgs& args) {
	g_sventv->enableDemoFile = !g_sventv->enableDemoFile;
	if (!g_sventv->enableDemoFile)
		g_can_autostart_demo = false;
	return true;
}

bool show_demo_stats(CBasePlayer* plr, const CommandArgs& args) {
	demoStatPlayers[plr->entindex()] = !demoStatPlayers[plr->entindex()];
	return true;
}

bool seek_demo(CBasePlayer* plr, const CommandArgs& args) {
	std::string offset = args.ArgV(1);
	int seekPos = atoi(offset.c_str());
	bool relative = offset[0] == '-' || offset[0] == '+';
	g_demoPlayer->seek(seekPos, relative);
	return true;
}

bool set_demo_speed(CBasePlayer* plr, const CommandArgs& args) {
	float speed = atof(args.ArgV(1).c_str());
	g_demoPlayer->setPlaybackSpeed(speed);
	UTIL_ClientPrintAll(print_center, UTIL_VarArgs("Playback speed: %.2fx\n", speed));
	return true;
}

bool search_demo(CBasePlayer* plr, const CommandArgs& args) {
	g_demoPlayer->searchCommand(plr, args.ArgV(1));
	return true;
}

#ifdef HLCOOP_BUILD
HOOK_RET_VOID ClientCommand(CBasePlayer* pPlayer)
#else
HOOK_RET_VOID ClientCommand(edict_t* pEntity)
#endif
{
#ifdef HLCOOP_BUILD
	edict_t* pEntity = pPlayer->edict();
#endif

	if (g_sventv->enableDemoFile || g_sventv->enableServer) {
		string lowerArg0 = toLowerCase(CMD_ARGV(0));
		string cmd = CMD_ARGC() > 1 ? CMD_ARGS() : "";
		cmd = CMD_ARGV(0) + string(" ") + cmd;

		CommandData& dat = g_cmds[g_command_count];
		dat.idx = ENTINDEX(pEntity);
		dat.len = cmd.size();
		memcpy(dat.data, cmd.c_str(), cmd.size());

		g_command_count++;
		if (g_command_count >= MAX_CMD_FRAME) {
			ALERT(at_console, "Command capture overflow!\n");
			g_command_count--; // overwrite last command
		}
	}	

	DEFAULT_HOOK_RETURN;
}

HOOK_RET_VOID PlaybackEvent(int flags, const edict_t* pInvoker, unsigned short eventindex, float delay, 
	float* origin, float* angles, float fparam1, float fparam2,
	int iparam1, int iparam2, int bparam1, int bparam2) {
	
	if (!g_sventv->enableDemoFile && !g_sventv->enableServer) {
		DEFAULT_HOOK_RETURN;
	}

	if (pInvoker && ENTINDEX(pInvoker) <= gpGlobals->maxClients && eventindex == EVT_GAUSS_CHARGE) {
		GaussChargeEvt& lastEvent = lastGaussCharge[ENTINDEX(pInvoker)];
		float dtime = gpGlobals->time - lastEvent.time;
		if ((lastEvent.charge != iparam1 && dtime > 0.1f) || dtime > 0.2f) {
			lastEvent.time = gpGlobals->time;
			lastEvent.charge = iparam1;
		}
		else {
			// reduce event spam from gauss charging
			DEFAULT_HOOK_RETURN;
		}
	}
	/*
	ALERT(at_console, "RECORD EVT: %d %d %d %f (%.1f %.1f %.1f) (%.1f %.1f %.1f) %f %f %d %d %d %d\n",
		flags, pInvoker ? ENTINDEX(pInvoker) : 0, (int)eventindex, delay,
		origin[0], origin[1], origin[2], angles[0], angles[1], angles[2],
		fparam1, fparam2, iparam1, iparam2, bparam1, bparam2);
		*/
	DemoEventData& ev = g_events[g_event_count];
	memset(&ev, 0, (int)sizeof(DemoEventData));
	
	ev.header.eventindex = eventindex;
	ev.header.entindex = pInvoker ? ENTINDEX(pInvoker) : 0;
	ev.header.flags = flags & (~FEV_NOTHOST); // don't skip sending this to listen server host player

	if (origin[0] != 0 || origin[1] != 0 || origin[2] != 0) {
		ev.header.hasOrigin = 1;
		ev.origin[0] = FLOAT_TO_FIXED(origin[0], 21, 3);
		ev.origin[1] = FLOAT_TO_FIXED(origin[1], 21, 3);
		ev.origin[2] = FLOAT_TO_FIXED(origin[2], 21, 3);
	}
	if (angles[0] != 0 || angles[1] != 0 || angles[2] != 0) {
		ev.header.hasAngles = 1;
		ev.angles[0] = angles[0] * 8;
		ev.angles[1] = angles[1] * 8;
		ev.angles[2] = angles[2] * 8;
	}
	if (fparam1 != 0) {
		ev.header.hasFparam1 = 1;
		ev.fparam1 = fparam1 * 128;
	}
	if (fparam2 != 0) {
		ev.header.hasFparam2 = 1;
		ev.fparam2 = fparam2 * 128;
	}
	if (iparam1 != 0) {
		ev.header.hasIparam1 = 1;
		ev.iparam1 = iparam1;
	}
	if (iparam2 != 0) {
		ev.header.hasIparam2 = 1;
		ev.iparam2 = iparam2;
	}
	ev.header.bparam1 = bparam1;
	ev.header.bparam2 = bparam2;

	g_event_count++;
	if (g_event_count >= MAX_EVENT_FRAME) {
		ALERT(at_console, "Event capture overflow!\n");
		g_event_count--; // overwrite last event
	}

	DEFAULT_HOOK_RETURN;
}

HOOK_RET_VOID EMIT_SOUND_HOOK(edict_t* entity, int channel, const char* sample, float fvolume,
	float attenuation, int fFlags, int pitch, const float* origin, uint32_t recipients, bool reliable) {

	mstream* stream = BuildStartSoundMessage(entity, channel, sample, fvolume, attenuation, fFlags, pitch, origin);

	if (!stream) {
		DEFAULT_HOOK_RETURN;
	}

	int sz = stream->tell() + 1;
	char* buffer = stream->getBuffer();
	bool sendPAS = channel != CHAN_STATIC && !(fFlags & SND_STOP);

	if (fFlags & SND_FL_PREDICTED) {
		// sound is predicted by the client that emitted it, but the demo player doesn't run any
		// movement prediction code for the dummy player entities, so let the emitter hear it too
		uint32_t targets = PLRBIT(entity);

		MessageBegin(MSG_ONE, SVC_SOUND, NULL, entity);
		for (int i = 0; i < sz; i++) {
			WriteByte(buffer[i]);
		}
		MessageEnd();

		DEFAULT_HOOK_RETURN;
	}

	if (fFlags & SND_FL_MOD) {
		DEFAULT_HOOK_RETURN; // will catch this as a normal network message
	}

	// can't hook engine messages, so pretend the game sent this
	MessageBegin(sendPAS ? MSG_PAS : MSG_BROADCAST, SVC_SOUND, sendPAS ? origin : NULL, NULL);
	//MessageBegin(MSG_BROADCAST, SVC_SOUND, NULL, NULL); // save space in the demo
	for (int i = 0; i < sz; i++) {
		WriteByte(buffer[i]);
	}
	MessageEnd();

	DEFAULT_HOOK_RETURN;
}

HOOK_RET_VOID EMIT_AMBIENT_SOUND_HOOK(edict_t* entity, const float* pos, const char* samp, float vol, float attenuation, int fFlags, int pitch) {
	int soundnum;

	if (samp[0] == '!')
	{
		fFlags |= SND_SENTENCE;
		soundnum = atoi(samp + 1);
		if (soundnum >= CVOXFILESENTENCEMAX)
		{
			ALERT(at_error, "invalid sentence number: %s", &samp[1]);
			DEFAULT_HOOK_RETURN;
		}
	}
	else
	{
		soundnum = PRECACHE_SOUND_ENT(NULL, samp);
	}

	// can't hook engine messages, so pretend the game sent this
	MessageBegin(MSG_BROADCAST, SVC_SPAWNSTATICSOUND, NULL, NULL);
	WriteCoord(pos[0]);
	WriteCoord(pos[1]);
	WriteCoord(pos[2]);

	WriteShort(soundnum);
	WriteByte(vol * 255.0);
	WriteByte(attenuation * 64.0);
	WriteShort(ENTINDEX(entity));
	WriteByte(pitch);
	WriteByte(fFlags);
	MessageEnd();	

	DEFAULT_HOOK_RETURN;
}

HOOK_RET_VOID GetWeaponData(edict_t* player, weapon_data_t* info) {	
	CBasePlayer* pl = (CBasePlayer*)CBasePlayer::Instance(player);

	if (!g_demoPlayer || !g_demoPlayer->isPlaying() || !pl)
		return HOOK_CONTINUE;

	int ret = g_demoPlayer->GetWeaponData(player, info);

	if (ret == -1) {
		return HOOK_CONTINUE;
	}
	else {
		return HOOK_HANDLED_OVERRIDE(ret);
	}
}

HOOK_RET_VOID UpdateClientDataPost(const edict_t* ent, int sendweapons, clientdata_t* cd) {
	CBasePlayer* pl = (CBasePlayer*)CBasePlayer::Instance(ent);
	
	if (g_demoPlayer && g_demoPlayer->isPlaying() && pl)
		g_demoPlayer->OverrideClientData(ent, sendweapons, cd);
	return HOOK_CONTINUE;
}

HOOK_RET_VOID CmdStart(const edict_t* player, const struct usercmd_s* cmd, unsigned int random_seed) {
	if (!g_write_user_cmds->value || (!g_sventv->enableDemoFile && !g_sventv->enableServer)) {
		DEFAULT_HOOK_RETURN;
	}

	int eidx = ENTINDEX(player) - 1;

	DemoUserCmdData& dcmd = g_usercmds[eidx][g_usercmd_count[eidx]];
	memset(&dcmd, 0, sizeof(DemoUserCmdData));

	dcmd.lerp_msec = cmd->lerp_msec;
	dcmd.msec = cmd->msec;
	dcmd.viewangles[0] = normalizeRangef(cmd->viewangles[0], 0, 360.0f) * (65535.0f / 360.0f);
	dcmd.viewangles[1] = normalizeRangef(cmd->viewangles[1], 0, 360.0f) * (65535.0f / 360.0f);
	dcmd.viewangles[2] = normalizeRangef(cmd->viewangles[2], 0, 360.0f) * (65535.0f / 360.0f);
	dcmd.forwardmove = cmd->forwardmove;
	dcmd.sidemove = cmd->sidemove;
	dcmd.upmove = cmd->upmove;
	dcmd.lightlevel = cmd->lightlevel;
	dcmd.buttons = cmd->buttons;
	dcmd.impulse = cmd->impulse;
	dcmd.weaponselect = cmd->weaponselect;

	g_usercmd_count[eidx]++;
	if (g_usercmd_count[eidx] >= MAX_USERCMD_FRAME) {
		ALERT(at_console, "usercmd capture overflow for player %d!\n", eidx);
		g_usercmd_count[eidx]--; // overwrite last event
	}

	DEFAULT_HOOK_RETURN;
}

HOOK_RET_VOID PlayerCustomization(edict_t* pEntity, customization_t* pCust) {

	CBasePlayer* pPlayer = (CBasePlayer*)GET_PRIVATE(pEntity);

	if (!pPlayer || !pCust || pCust->resource.type != t_decal) {
		DEFAULT_HOOK_RETURN;
	}

	if (pCust->resource.nDownloadSize > 65535) {
		ALERT(at_console, "player uploaded huge invalid spray. ignoring.");
		DEFAULT_HOOK_RETURN;
	}

	int eidx = pPlayer->entindex() - 1;
	bool newSz = pCust->resource.nDownloadSize != g_playerSprays[eidx].sz;
	
	if (newSz || memcmp(g_playerSprays[eidx].data, pCust->pBuffer, pCust->resource.nDownloadSize)) {
		g_playerSprays[eidx].dirty = true;
		g_playerSprays[eidx].sz = pCust->resource.nDownloadSize;
		memcpy(g_playerSprays[eidx].data, pCust->pBuffer, pCust->resource.nDownloadSize);
	}
	else {
		ALERT(at_console, "player uploaded the same spray. ignoring.");
	}

	DEFAULT_HOOK_RETURN;
}

#ifdef HLCOOP_BUILD
HLCOOP_PLUGIN_HOOKS g_hooks;

HOOK_RETURN_DATA SendVoiceData(int senderidx, int receiveridx, uint8_t* data, int sz, bool& mute) {
	if (sz >= MAX_NETMSG_DATA) {
		ALERT(at_error, "Dropped huge voice packet (%d bytes)\n", sz);
		return HOOK_CONTINUE;
	}

	if (!g_engfuncs.pfnVoice_GetClientListening(receiveridx, senderidx)) {
		return HOOK_CONTINUE; // mod will not send the voice packet to this player
	}

	// save to the demo as a normal message
	MessageBegin(MSG_ONE_UNRELIABLE, SVC_VOICEDATA, NULL, INDEXENT(receiveridx));
	WriteByte(senderidx - 1);
	WriteShort(sz);
	int longCount = sz / 4;
	int byteCount = sz % 4;

	int32_t* longPtr = (int32_t*)data;
	for (int i = 0; i < longCount; i++) {
		WriteLong(*longPtr);
		longPtr++;
	}

	uint8_t* bytePtr = data + longCount * 4;
	for (int i = 0; i < byteCount; i++) {
		WriteByte(*bytePtr);
		bytePtr++;
	}
	MessageEnd();

	return HOOK_CONTINUE;
}

extern "C" int DLLEXPORT PluginInit() {
	g_plugin_exiting = false;

	g_hooks.pfnMapInit = MapInitHook;
	g_hooks.pfnServerActivate = MapInit_post;
	g_hooks.pfnServerDeactivate = Changelevel;
	g_hooks.pfnStartFrame = StartFrameHook;
	g_hooks.pfnClientDisconnect = ClientLeaveHook;
	g_hooks.pfnClientUserInfoChanged = ClientUserInfoChangedHook;
	g_hooks.pfnClientCommand = ClientCommand;
	g_hooks.pfnSendVoiceData = SendVoiceData;
	g_hooks.pfnCmdStart = CmdStart;
	g_hooks.pfnPlayerCustomization = PlayerCustomization;

	g_hooks.pfnMessageBegin = MessageBegin;
	g_hooks.pfnWriteAngle = WriteAngle;
	g_hooks.pfnWriteByte = WriteByte;
	g_hooks.pfnWriteChar = WriteChar;
	g_hooks.pfnWriteCoord = WriteCoord;
	g_hooks.pfnWriteEntity = WriteEntity;
	g_hooks.pfnWriteLong = WriteLong;
	g_hooks.pfnWriteShort = WriteShort;
	g_hooks.pfnWriteString = WriteString;
	g_hooks.pfnMessageEnd = MessageEnd;

	g_hooks.pfnPlaybackEvent = PlaybackEvent;
	g_hooks.pfnEmitSound = EMIT_SOUND_HOOK;
	g_hooks.pfnEmitAmbientSound = EMIT_AMBIENT_SOUND_HOOK;
	g_hooks.pfnGetWeaponData = GetWeaponData;
	g_hooks.pfnUpdateClientDataPost = UpdateClientDataPost;
	
	RegisterPluginCommand(".demo", record_demo, FL_CMD_ANY | FL_CMD_ADMIN);
	RegisterPluginCommand(".replay", replay_demo, FL_CMD_ANY | FL_CMD_ADMIN);
	RegisterPluginCommand(".seek", seek_demo, FL_CMD_ANY | FL_CMD_ADMIN);
	RegisterPluginCommand(".speed", set_demo_speed, FL_CMD_ANY | FL_CMD_ADMIN);
	RegisterPluginCommand(".search", search_demo, FL_CMD_ANY | FL_CMD_ADMIN);

#ifdef HLCOOP_BUILD
	// start writing demo file automatically when map starts, if 1
	g_auto_demo_file = RegisterPluginCVar("hltv.auto_demo", "0", 0, 0);
	g_min_storage_megabytes = RegisterPluginCVar("hltv.min_storage_mb", "500", 500, 0);
	g_max_demo_megabytes = RegisterPluginCVar("hltv.max_demo_mb", "100", 100, 0);
	g_compress_demos = RegisterPluginCVar("hltv.compress", "1", 1, 0);
	g_demo_file_path = RegisterPluginCVar("hltv.path", "hltv/", 0, 0);
	g_validate_output = RegisterPluginCVar("hltv.validate", "0", 0, 0);
	g_write_debug_info = RegisterPluginCVar("hltv.debug_info", "1", 0, 0);
	g_write_user_cmds = RegisterPluginCVar("hltv.write_user_cmds", "0", 0, 0);
#else
	g_main_thread_id = std::this_thread::get_id();

	// start writing demo file automatically when map starts, if 1
	g_auto_demo_file = RegisterCVar("sventv.autodemofile", "0", 0, 0);
	g_demo_file_path = RegisterCVar("sventv.demofilepath", "svencoop_addon/scripts/plugins/metamod/SvenTV/", 0, 0);
#endif


	if (gpGlobals->time > 3.0f) {
		g_sventv = new SvenTV(singleThreadMode);
		g_demoPlayer = new DemoPlayer();
	}

	g_netmessages = new NetMessageData[MAX_NETMSG_FRAME];
	g_cmds = new CommandData[MAX_CMD_FRAME];
	g_events = new DemoEventData[MAX_EVENT_FRAME];

	for (int i = 0; i < MAX_PLAYERS; i++)
		g_usercmds[i] = new DemoUserCmdData[MAX_USERCMD_FRAME];
	memset(lastGaussCharge, 0, sizeof(GaussChargeEvt) * MAX_PLAYERS);

	return RegisterPlugin(&g_hooks);
}

extern "C" void DLLEXPORT PluginExit() {
	if (g_sventv) delete g_sventv;
	if (g_demoPlayer) delete g_demoPlayer;
	delete[] g_netmessages;
	delete[] g_cmds;
	delete[] g_events;
	for (int i = 0; i < MAX_PLAYERS; i++)
		delete[] g_usercmds[i];
}
#else

void PluginInit() {
	g_plugin_exiting = false;

	g_dll_hooks.pfnServerActivate = MapInit;
	g_dll_hooks_post.pfnServerActivate = MapInit_post;
	g_dll_hooks.pfnServerDeactivate = Changelevel;
	g_dll_hooks.pfnStartFrame = StartFrame;
	g_dll_hooks.pfnClientDisconnect = ClientLeave;
	g_dll_hooks.pfnClientUserInfoChanged = ClientUserInfoChanged;
	g_dll_hooks.pfnClientPutInServer = ClientJoin;
	g_dll_hooks.pfnClientCommand = ClientCommand;

	g_engine_hooks.pfnMessageBegin = MessageBegin;
	g_engine_hooks.pfnWriteAngle = WriteAngle;
	g_engine_hooks.pfnWriteByte = WriteByte;
	g_engine_hooks.pfnWriteChar = WriteChar;
	g_engine_hooks.pfnWriteCoord = WriteCoord;
	g_engine_hooks.pfnWriteEntity = WriteEntity;
	g_engine_hooks.pfnWriteLong = WriteLong;
	g_engine_hooks.pfnWriteShort = WriteShort;
	g_engine_hooks.pfnWriteString = WriteString;
	g_engine_hooks.pfnMessageEnd = MessageEnd;

	g_engine_hooks.pfnPlaybackEvent = PlaybackEvent;

	g_engine_hooks_post.pfnSetModel = SetModel;
	g_engine_hooks_post.pfnPrecacheModel = PrecacheModel_post;

	const char* stringPoolStart = gpGlobals->pStringBase;

	g_main_thread_id = std::this_thread::get_id();

	// start writing demo file automatically when map starts
	g_auto_demo_file = RegisterCVar("sventv.autodemofile", "0", 0, 0);

	g_demo_file_path = RegisterCVar("sventv.demofilepath", "svencoop_addon/scripts/plugins/metamod/SvenTV/", 0, 0);

	if (gpGlobals->time > 3.0f) {
		g_sventv = new SvenTV(singleThreadMode);
		g_demoPlayer = new DemoPlayer();
		loadSoundCacheFile();
		loadSvenTvState();
	}

	g_demoplayers = new DemoPlayerEnt[32];
	g_netmessages = new NetMessageData[MAX_NETMSG_FRAME];
	g_cmds = new CommandData[MAX_CMD_FRAME];
	g_events = new DemoEventData[MAX_EVENT_FRAME];
	memset(g_demoplayers, 0, 32*sizeof(DemoPlayerEnt));
	memset(lastGaussCharge, 0, sizeof(GaussChargeEvt) * MAX_PLAYERS);
}

void PluginExit() {
	writeSvenTvState();
	if (g_sventv) delete g_sventv;
	if (g_demoPlayer) delete g_demoPlayer;
	delete[] g_demoplayers;
	delete[] g_netmessages;
	delete[] g_cmds;
	delete[] g_events;

	ALERT(at_console, "Plugin exit finish\n");
}

#endif