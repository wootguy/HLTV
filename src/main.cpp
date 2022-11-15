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

void tv_think();

void PluginInit() {
	g_plugin_exiting = false;

	g_dll_hooks.pfnServerActivate = MapInit;
	g_dll_hooks.pfnStartFrame = StartFrame;
	g_dll_hooks.pfnClientDisconnect = ClientLeave;

	const char* stringPoolStart = gpGlobals->pStringBase;

	g_main_thread_id = std::this_thread::get_id();
	
	if (gpGlobals->time > 3.0f)
		g_sventv = new SvenTV(singleThreadMode);
}

void ClientLeave(edict_t* plr) {
	RETURN_META(MRES_IGNORED);
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

void StartFrame() {
	handleThreadPrints();

	if (g_sventv)
		g_sventv->think_mainThread();

	RETURN_META(MRES_IGNORED);
}

void PluginExit() {
	delete g_sventv;

	println("Plugin exit finish");
}
