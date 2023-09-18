#include "main.h"
#include "mmlib.h"
#include <string>
#include "misc_utils.h"
#include "ThreadSafeInt.h"
#include "SvenTV.h"

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
DemoPlayer* g_demoPlayer = NULL;
DemoPlayerEnt* g_demoplayers = NULL;
NetMessageData* g_netmessages = NULL;
int g_netmessage_count = 0;
CommandData* g_cmds = NULL;
int g_command_count = 0;
uint32_t g_server_frame_count = 0;
int g_copyTime = 0;
volatile int g_thinkTime = 0;
bool g_should_write_next_message = false;

// maps indexes to model names, for all models that were used so far in this map
map<int, string> g_indexToModel;
set<string> g_playerModels; // all player model names used during the game

cvar_t* g_auto_demo_file;
cvar_t* g_demo_file_path;

const char* stateFilePath = "svencoop/addons/metamod/store/sventv.txt";

void ClientLeave(edict_t* ent) {
	DemoPlayerEnt& plr = g_demoplayers[ENTINDEX(ent) - 1];
	plr.isConnected = false;
	RETURN_META(MRES_IGNORED);
}

void Changelevel() {
	if (g_sventv) {
		g_sventv->enableDemoFile = false;
		g_demoPlayer->stopReplay();
		g_server_frame_count = 0;
		g_netmessage_count = 0;
		g_server_frame_count = 0;
		g_indexToModel.clear();
		g_playerModels.clear();
	}
	remove(stateFilePath);
}

void writeSvenTvState() {
	FILE* file = fopen(stateFilePath, "w");
	if (!file) {
		println("Failed to write sventv state file");
		return;
	}

	for (auto item : g_indexToModel) {
		string line = to_string(item.first) + "=" + item.second + "\n";
		fwrite(line.c_str(), line.size(), 1, file);
	}

	fclose(file);
}

void loadSvenTvState() {
	FILE* file = fopen(stateFilePath, "r");
	if (!file) {
		println("Failed to open sventv state file");
		return;
	}

	string line;
	bool parsingSounds = false;
	while (cgetline(file, line)) {
		if (line.empty()) {
			continue;
		}

		vector<string> parts = splitString(line, "=");

		if (parts.size() != 2) {
			continue;
		}

		g_indexToModel[atoi(parts[0].c_str())] = parts[1];
	}

	fclose(file);
}

void MapInit(edict_t* pEdictList, int edictCount, int maxClients) {
	if (!g_sventv) {
		g_sventv = new SvenTV(singleThreadMode);
		g_demoPlayer = new DemoPlayer();
	}
	g_demoPlayer->precacheDemo();
	remove(stateFilePath);
	
	RETURN_META(MRES_IGNORED);
}

void MapInit_post(edict_t* pEdictList, int edictCount, int maxClients) {
	loadSoundCacheFile();

	writeSvenTvState();

	RETURN_META(MRES_IGNORED);
}

void loadPlayerInfo(edict_t* pEntity, char* infobuffer) {
	vector<string> parts = splitString(infobuffer, "\\");
	DemoPlayerEnt& plr = g_demoplayers[ENTINDEX(pEntity) - 1];

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
			g_playerModels.insert(value);
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

void StartFrame() {
	handleThreadPrints();
	g_Scheduler.Think();

	g_server_frame_count++;

	if (!g_sventv->enableDemoFile && g_auto_demo_file->value > 0 && gpGlobals->time > 1.0f) {
		g_sventv->enableDemoFile = true;
	}

	g_demoPlayer->playDemo();

	if (!g_sventv->enableDemoFile && !g_sventv->enableServer) {
		RETURN_META(MRES_IGNORED);
	}

	for (int i = 1; i <= gpGlobals->maxClients; i++) {
		edict_t* ent = INDEXENT(i);
		CBasePlayer* plr = (CBasePlayer*)GET_PRIVATE(ent);

		if (!isValidPlayer(ent) || !plr) {
			continue;
		}

		CBasePlayerWeapon* wep = (CBasePlayerWeapon*)plr->m_hActiveItem.GetEntity();

		if (wep) {
			DemoPlayerEnt& dplr = g_demoplayers[i - 1];
			int pammo = wep->m_iPrimaryAmmoType;
			int sammo = wep->m_iSecondaryAmmoType;
			dplr.clip = clamp(wep->m_iClip, 0, 65535);
			dplr.clip2 = clamp(wep->m_iClip2, 0, 65535);
			dplr.ammo = pammo >= 0 && pammo < 64 ? clamp(plr->m_rgAmmo[pammo], 0, 65535) : 0;
			dplr.ammo2 = sammo >= 0 && sammo < 64 ? clamp(plr->m_rgAmmo[sammo], 0, 65535) : 0;
		}
	}

	if (g_engfuncs.pfnTime() - lastPingUpdate > 1.0f) {
		lastPingUpdate = g_engfuncs.pfnTime();

		for (int i = 1; i <= gpGlobals->maxClients; i++) {
			DemoPlayerEnt& plr = g_demoplayers[i - 1];
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
	DemoPlayerEnt& plr = g_demoplayers[ENTINDEX(ent) - 1];
	plr.steamid64 = getSteamId64(ent);
	plr.isConnected = true;

	RETURN_META(MRES_IGNORED);
}

void PM_Move(playermove_s* ppmove, int server) {
	DemoPlayerEnt& plr = g_demoplayers[ppmove->player_index];
	plr.pmMoveCounter++;
	RETURN_META(MRES_IGNORED);
}

void MessageBegin(int msg_dest, int msg_type, const float* pOrigin, edict_t* ed) {
	//println("NET MESG: %d", msg_type);
	if (!g_sventv->enableDemoFile && !g_sventv->enableServer) {
		g_should_write_next_message = false;
		RETURN_META(MRES_IGNORED);
	}
	if (g_netmessage_count >= MAX_NETMSG_FRAME) {
		g_netmessage_count--; // overwrite last message
		println("[SvenTV] Network message capture overflow!");
	}

	g_should_write_next_message = msg_dest != MSG_ONE && msg_dest != MSG_ONE_UNRELIABLE && msg_dest != MSG_INIT;

	NetMessageData& msg = g_netmessages[g_netmessage_count];
	msg.header.dest = msg_dest;
	msg.header.type = msg_type;
	if (pOrigin) {
		msg.header.hasOrigin = 1;
		memcpy(msg.origin, pOrigin, 3 * sizeof(float));
	}
	else {
		msg.header.hasOrigin = 0;
	}
	if (ed) {
		msg.header.hasEdict = 1;
		msg.eidx = ENTINDEX(ed);
	}
	else {
		msg.header.hasEdict = 0;
	}
	msg.header.sz = 0;

	RETURN_META(MRES_IGNORED);
}

void MessageEnd() {
	if (g_should_write_next_message)
		g_netmessage_count++;
	RETURN_META(MRES_IGNORED);
}

void WriteAngle(float angle) {
	NetMessageData& msg = g_netmessages[g_netmessage_count];
	if (msg.header.sz + sizeof(byte) < MAX_NETMSG_DATA) {
		byte dat = (int64)(fmod((double)angle, 360.0) * 256.0 / 360.0) & 0xFF;
		memcpy(msg.data + msg.header.sz, &angle, sizeof(byte));
		msg.header.sz += sizeof(byte);
	}
	RETURN_META(MRES_IGNORED);
}

void WriteByte(int b) {
	NetMessageData& msg = g_netmessages[g_netmessage_count];
	if (msg.header.sz + sizeof(byte) < MAX_NETMSG_DATA) {
		byte dat = b;
		memcpy(msg.data + msg.header.sz, &dat, sizeof(byte));
		msg.header.sz += sizeof(byte);
	}
	RETURN_META(MRES_IGNORED);
}

void WriteChar(int c) {
	NetMessageData& msg = g_netmessages[g_netmessage_count];
	if (msg.header.sz + sizeof(byte) < MAX_NETMSG_DATA) {
		byte dat = c;
		memcpy(msg.data + msg.header.sz, &dat, sizeof(byte));
		msg.header.sz += sizeof(byte);
	}
	RETURN_META(MRES_IGNORED);
}

void WriteCoord(float coord) {
	NetMessageData& msg = g_netmessages[g_netmessage_count];
	if (msg.header.sz + sizeof(float) < MAX_NETMSG_DATA) {
		int32_t arg = coord * 8;
		memcpy(msg.data + msg.header.sz, &arg, sizeof(int32_t));
		msg.header.sz += sizeof(int32_t);
	}
	RETURN_META(MRES_IGNORED);
}

void WriteEntity(int ent) {
	NetMessageData& msg = g_netmessages[g_netmessage_count];
	if (msg.header.sz + sizeof(uint16_t) < MAX_NETMSG_DATA) {
		uint16_t dat = ent;
		memcpy(msg.data + msg.header.sz, &dat, sizeof(uint16_t));
		msg.header.sz += sizeof(uint16_t);
	}
	RETURN_META(MRES_IGNORED);
}

void WriteLong(int val) {
	NetMessageData& msg = g_netmessages[g_netmessage_count];
	if (msg.header.sz + sizeof(int) < MAX_NETMSG_DATA) {
		memcpy(msg.data + msg.header.sz, &val, sizeof(int));
		msg.header.sz += sizeof(int);
	}
	RETURN_META(MRES_IGNORED);
}

void WriteShort(int val) {
	NetMessageData& msg = g_netmessages[g_netmessage_count];
	if (msg.header.sz + sizeof(int16_t) < MAX_NETMSG_DATA) {
		int16_t dat = val;
		memcpy(msg.data + msg.header.sz, &dat, sizeof(int16_t));
		msg.header.sz += sizeof(int16_t);
	}
	RETURN_META(MRES_IGNORED);
}

void WriteString(const char* s) {
	NetMessageData& msg = g_netmessages[g_netmessage_count];
	int len = strlen(s)+1;
	if (msg.header.sz + len < MAX_NETMSG_DATA) {
		memcpy(msg.data + msg.header.sz, s, len);
		msg.header.sz += len;
	}
	RETURN_META(MRES_IGNORED);
}

bool doCommand(edict_t* plr) {
	bool isAdmin = AdminLevel(plr) >= ADMIN_YES;
	CommandArgs args = CommandArgs();
	args.loadArgs();
	string lowerArg = toLowerCase(args.ArgV(0));

	if (!isAdmin) {
		return false;
	}

	if (args.ArgC() > 0 && lowerArg == ".replay") {
		if (args.ArgC() > 1 || true) {
			string path = g_demo_file_path->string + args.ArgV(1);
			if (args.ArgV(1).empty()) {
				path += string(STRING(gpGlobals->mapname)) + ".demo";
			}
			float offsetSeconds = args.ArgC() > 2 ? atof(args.ArgV(2).c_str()) : 0;
			g_demoPlayer->openDemo(plr, path, offsetSeconds, true);
			g_sventv->enableDemoFile = false;
		}
		else {
			ClientPrint(plr, HUD_PRINTTALK, "Usage: .demo [demo file path]\n");
		}

		return true;
	}
	if (args.ArgC() > 0 && lowerArg == ".demo") {
		g_sventv->enableDemoFile = !g_sventv->enableDemoFile;
		return true;
	}
	/*
	if (args.ArgC() > 0 && lowerArg == ".bot") {
		edict_t* bot = g_engfuncs.pfnCreateFakeClient("botguy");
		if (bot) {
			gpGamedllFuncs->dllapi_table->pfnClientPutInServer(bot);
			char* infoBuffer = g_engfuncs.pfnGetInfoKeyBuffer(bot);
			g_engfuncs.pfnSetClientKeyValue(ENTINDEX(bot), infoBuffer, "model", "player");
			//g_Scheduler.SetInterval(updateBot, 0.1, -1, EHandle(plr), EHandle(bot));
			h_plr = plr;
			h_bot = bot;
		}
		return true;
	}
	*/
	if (args.ArgC() > 0 && lowerArg == ".kick") {
		for (int i = 1; i <= 32; i++) {
			edict_t* ent = INDEXENT(i);
			if (ent) {
				int userid = g_engfuncs.pfnGetPlayerUserId(ent);
				g_engfuncs.pfnServerCommand(UTIL_VarArgs("kick #%d\n", userid));
			}
		}
		g_engfuncs.pfnServerExecute();
		return true;
	}
	return false;
}

void ClientCommand(edict_t* pEntity) {
	META_RES ret = doCommand(pEntity) ? MRES_SUPERCEDE : MRES_IGNORED;

	if (g_sventv->enableDemoFile || g_sventv->enableServer) {
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
		dat.idx = ENTINDEX(pEntity);
		dat.len = cmd.size();
		memcpy(dat.data, cmd.c_str(), cmd.size());
		g_command_count++;
	}	

	RETURN_META(ret);
}

// maps BSP model indexes
void SetModel(edict_t* e, const char* m) {
	int index = MODEL_INDEX(m);
	g_indexToModel[index] = m;
}

// maps .mdl indexes
int PrecacheModel_post(char* m) {
	int index = MODEL_INDEX(m);
	g_indexToModel[index] = m;
	RETURN_META_VALUE(MRES_IGNORED, 0);
}

void PluginInit() {
	g_plugin_exiting = false;

	g_dll_hooks.pfnServerActivate = MapInit;
	g_dll_hooks_post.pfnServerActivate = MapInit_post;
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
	memset(g_demoplayers, 0, 32*sizeof(DemoPlayerEnt));
}

void PluginExit() {
	writeSvenTvState();
	if (g_sventv) delete g_sventv;
	if (g_demoPlayer) delete g_demoPlayer;
	delete[] g_demoplayers;
	delete[] g_netmessages;
	delete[] g_cmds;

	println("Plugin exit finish");
}
