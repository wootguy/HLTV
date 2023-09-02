#include "main.h"
#include <string>
#include "misc_utils.h"
#include "ThreadSafeInt.h"
#include "SvenTV.h"

using namespace std;

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

volatile bool g_plugin_exiting = false;
const bool singleThreadMode = false;

SvenTV* g_sventv = NULL;
DemoPlayer* g_demoplayers = NULL;
NetMessageData* g_netmessages = NULL;
int g_netmessage_count = 0;
CommandData* g_cmds = NULL;
int g_command_count = 0;
uint32_t g_server_frame_count = 0;

void ClientLeave(edict_t* ent) {
	DemoPlayer& plr = g_demoplayers[ENTINDEX(ent) - 1];
	plr.isConnected = false;
	RETURN_META(MRES_IGNORED);
}

void Changelevel() {
	if (g_sventv) {
		g_sventv->enableDemoFile = false;
	}
}

void MapInit(edict_t* pEdictList, int edictCount, int maxClients) {
	if (!g_sventv) {
		g_sventv = new SvenTV(singleThreadMode);
	}
	
	RETURN_META(MRES_IGNORED);
}

void handleThreadPrints() {
	string msg;
	for (int failsafe = 0; failsafe < 10; failsafe++) {
		if (g_thread_prints.dequeue(msg)) {
			println(msg.c_str());
		}
		else {
			break;
		}
	}

	for (int failsafe = 0; failsafe < 10; failsafe++) {
		if (g_thread_logs.dequeue(msg)) {
			logln(msg.c_str());
		}
		else {
			break;
		}
	}
}

void loadPlayerInfo(edict_t* pEntity, char* infobuffer) {
	vector<string> parts = splitString(infobuffer, "\\");
	DemoPlayer& plr = g_demoplayers[ENTINDEX(pEntity) - 1];

	for (int i = 1; i < parts.size() - 1; i += 2) {
		string key = parts[i];
		string value = parts[i + 1];

		if (key == "topcolor") {
			plr.topColor = atoi(value.c_str());
		}
		else if (key == "bottomcolor") {
			plr.bottomColor = atoi(value.c_str());
		}
		else if (key == "model") {
			strncpy(plr.model, value.c_str(), 22);
			plr.model[22] = 0;
		}
		else if (key == "name") {
			strncpy(plr.name, value.c_str(), 31);
			plr.name[31] = 0;
		}
	}
}

void ClientUserInfoChanged(edict_t* pEntity, char* infobuffer) {
	if (!isValidPlayer(pEntity)) {
		RETURN_META(MRES_IGNORED);
	}

	loadPlayerInfo(pEntity, infobuffer);

	RETURN_META(MRES_IGNORED);
}

float lastPingUpdate = 0;

// http://forum.tsgk.com/viewtopic.php?t=26238
uint64_t getSteamId64(edict_t* ent) {
	string steamid = g_engfuncs.pfnGetPlayerAuthId(ent);
	vector<string> parts = splitString(steamid, ":");
	if (parts.size() != 3) {
		return 1; // indicates LAN id
	}
	else {
		uint64_t x = atoi(parts[1].c_str());
		uint64_t y = atoi(parts[2].c_str());
		return ((y * 2LLU) - x) + 76561197960265728LLU;
	}
}

void StartFrame() {
	handleThreadPrints();

	g_server_frame_count++;

	if (!g_sventv->enableDemoFile && gpGlobals->time > 1.0f) {
		g_sventv->enableDemoFile = true;
		g_server_frame_count = 0;
	}

	if (g_engfuncs.pfnTime() - lastPingUpdate > 1.0f) {
		lastPingUpdate = g_engfuncs.pfnTime();

		for (int i = 1; i <= gpGlobals->maxClients; i++) {
			DemoPlayer& plr = g_demoplayers[i - 1];
			edict_t* ent = INDEXENT(i);
			
			if (!isValidPlayer(ent)) {
				plr.isConnected = false;
				continue;
			}
			
			int ping;
			int loss;
			g_engfuncs.pfnGetPlayerStats(ent, &ping, &loss);
			plr.ping = clamp(ping, 0, 65535);

			if (plr.steamid64 == 0) {
				// plugin reloaded mid-map
				println("INIT PLAYER LATE");
				plr.isConnected = true;
				plr.steamid64 = getSteamId64(ent);
				char* infobuffer = g_engfuncs.pfnGetInfoKeyBuffer(ent);
				loadPlayerInfo(ent, infobuffer);
			}
		}

	}
	

	if (g_sventv) {
		g_sventv->think_mainThread();
	}

	RETURN_META(MRES_IGNORED);
}

void ClientJoin(edict_t* ent) {
	DemoPlayer& plr = g_demoplayers[ENTINDEX(ent) - 1];
	plr.steamid64 = getSteamId64(ent);
	plr.isConnected = true;

	RETURN_META(MRES_IGNORED);
}

void PM_Move(playermove_s* ppmove, int server) {
	DemoPlayer& plr = g_demoplayers[ppmove->player_index];
	plr.pmMoveCounter++;
	RETURN_META(MRES_IGNORED);
}

bool g_should_write_next_message = false;

void MessageBegin(int msg_dest, int msg_type, const float* pOrigin, edict_t* ed) {
	if (g_netmessage_count >= MAX_NETMSG_FRAME) {
		g_netmessage_count--; // overwrite last message
		println("[SvenTV] Network message capture overflow!");
	}

	g_should_write_next_message = msg_dest != MSG_ONE && msg_dest != MSG_ONE_UNRELIABLE && msg_dest != MSG_INIT;

	NetMessageData& msg = g_netmessages[g_netmessage_count];
	msg.dest = msg_dest;
	msg.type = msg_type;
	if (pOrigin) {
		msg.hasOrigin = 1;
		memcpy(msg.origin, pOrigin, 3 * sizeof(float));
	}
	else {
		msg.hasOrigin = 0;
	}
	if (ed) {
		msg.hasEdict = 1;
		msg.eidx = ENTINDEX(ed);
	}
	else {
		msg.hasEdict = 0;
	}
	msg.sz = 0;

	RETURN_META(MRES_IGNORED);
}

void MessageEnd() {
	if (g_should_write_next_message)
		g_netmessage_count++;
	RETURN_META(MRES_IGNORED);
}

void WriteAngle(float angle) {
	NetMessageData& msg = g_netmessages[g_netmessage_count];
	if (msg.sz + sizeof(byte) < MAX_NETMSG_DATA) {
		byte dat = (int64)(fmod((double)angle, 360.0) * 256.0 / 360.0) & 0xFF;
		memcpy(msg.data + msg.sz, &angle, sizeof(byte));
		msg.sz += sizeof(byte);
	}
	RETURN_META(MRES_IGNORED);
}

void WriteByte(int b) {
	NetMessageData& msg = g_netmessages[g_netmessage_count];
	if (msg.sz + sizeof(byte) < MAX_NETMSG_DATA) {
		byte dat = b;
		memcpy(msg.data + msg.sz, &dat, sizeof(byte));
		msg.sz += sizeof(float);
	}
	RETURN_META(MRES_IGNORED);
}

void WriteChar(int c) {
	NetMessageData& msg = g_netmessages[g_netmessage_count];
	if (msg.sz + sizeof(byte) < MAX_NETMSG_DATA) {
		byte dat = c;
		memcpy(msg.data + msg.sz, &dat, sizeof(byte));
		msg.sz += sizeof(float);
	}
	RETURN_META(MRES_IGNORED);
}

void WriteCoord(float coord) {
	NetMessageData& msg = g_netmessages[g_netmessage_count];
	if (msg.sz + sizeof(int16_t) < MAX_NETMSG_DATA) {
		int16_t dat = coord * 8.0f;
		memcpy(msg.data + msg.sz, &dat, sizeof(int16_t));
		msg.sz += sizeof(int16_t);
	}
	RETURN_META(MRES_IGNORED);
}

void WriteEntity(int ent) {
	NetMessageData& msg = g_netmessages[g_netmessage_count];
	if (msg.sz + sizeof(uint16_t) < MAX_NETMSG_DATA) {
		uint16_t dat = ent;
		memcpy(msg.data + msg.sz, &dat, sizeof(uint16_t));
		msg.sz += sizeof(uint16_t);
	}
	RETURN_META(MRES_IGNORED);
}

void WriteLong(int val) {
	NetMessageData& msg = g_netmessages[g_netmessage_count];
	if (msg.sz + sizeof(int) < MAX_NETMSG_DATA) {
		memcpy(msg.data + msg.sz, &val, sizeof(int));
		msg.sz += sizeof(int);
	}
	RETURN_META(MRES_IGNORED);
}

void WriteShort(int val) {
	NetMessageData& msg = g_netmessages[g_netmessage_count];
	if (msg.sz + sizeof(int16_t) < MAX_NETMSG_DATA) {
		int16_t dat = val;
		memcpy(msg.data + msg.sz, &dat, sizeof(int16_t));
		msg.sz += sizeof(int16_t);
	}
	RETURN_META(MRES_IGNORED);
}

void WriteString(const char* s) {
	NetMessageData& msg = g_netmessages[g_netmessage_count];
	int len = strlen(s)+1;
	if (msg.sz + len < MAX_NETMSG_DATA) {
		memcpy(msg.data + msg.sz, s, len);
		msg.sz += len;
	}
	RETURN_META(MRES_IGNORED);
}

void ClientCommand(edict_t* pEntity) {
	string lowerArg0 = toLowerCase(CMD_ARGV(0));
	bool isConsoleCmd = lowerArg0 != "say" && lowerArg0 != "say_team";
	string cmd = CMD_ARGC() > 1 ? CMD_ARGS() : "";

	if (isConsoleCmd) {
		cmd = CMD_ARGV(0) + string(" ") + cmd;
	}

	if (g_command_count >= MAX_CMD_FRAME) {
		println("[SvenTV] Command capture overflow!");
		g_command_count--; // overwrite last command
	}

	CommandData& dat = g_cmds[g_command_count];
	dat.idx = ENTINDEX(pEntity)-1;
	dat.len = cmd.size();
	memcpy(dat.data, cmd.c_str(), cmd.size());
	g_command_count++;

	RETURN_META(MRES_IGNORED);
}

void PluginInit() {
	g_plugin_exiting = false;

	g_dll_hooks.pfnServerActivate = MapInit;
	g_dll_hooks.pfnServerDeactivate = Changelevel;
	g_dll_hooks.pfnStartFrame = StartFrame;
	g_dll_hooks.pfnClientDisconnect = ClientLeave;
	g_dll_hooks.pfnClientUserInfoChanged = ClientUserInfoChanged;
	g_dll_hooks.pfnClientPutInServer = ClientJoin;
	g_dll_hooks.pfnPM_Move = PM_Move;
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

	const char* stringPoolStart = gpGlobals->pStringBase;

	g_main_thread_id = std::this_thread::get_id();

	if (gpGlobals->time > 3.0f)
		g_sventv = new SvenTV(singleThreadMode);

	g_demoplayers = new DemoPlayer[32];
	g_netmessages = new NetMessageData[MAX_NETMSG_FRAME];
	g_cmds = new CommandData[MAX_CMD_FRAME];
	memset(g_demoplayers, 0, 32*sizeof(DemoPlayer));
}

void PluginExit() {
	delete g_sventv;
	delete[] g_demoplayers;
	delete[] g_netmessages;
	delete[] g_cmds;

	println("Plugin exit finish");
}
