#include "StreamViewers.h"
#include <string.h>
#include <algorithm>

bool isSpecCamera(const edict_t& ed) {
	return !ed.free && ed.v.classname && !strcmp(STRING(ed.v.classname), SPEC_CAMERA_CLASS);
}

#ifdef HLCOOP_BUILD
#include "rehlds.h"
#include "SvenTV.h"
#include "PluginManager.h"
#include "playerdb.h"
#include <unordered_set>
#include <unordered_map>

#define WEB_SPEC_TEAM "Web Spectators"
#define WEB_SPEC_STATUS 2 // shown as SPEC on the SevenKewp scoreboard
#define WEB_SPEC_COLOR 4 // green on the scoreboard (FRIEND_TEAM_COLOR)
#define SCOREBOARD_UPDATE_SECONDS 1.0f
#define MAX_SLOT_NAME 31
#define MAX_PLAYER_NAME 31 // bytes in a player name, not counting the terminator (32 with it)
#define MIN_CAMERA_LERP 0.02f // seconds to move a camera to a new view, if updates come quicker
#define MAX_CAMERA_LERP 0.2f // ...or if they come slower
// How long a viewer must be gone to count as stopped, rather than reconnecting. Viewers reconnect
// after every map change, which can take a while, so ones from the last map get longer.
#define WATCH_END_MS (5 * 1000) // left during the map (reloading the page takes a few seconds)
#define WATCH_END_MAP_CHANGE_MS (30 * 1000) // from the start of the new map
#define MAX_TALK_SECONDS 60.0f // longest a chat sound can keep a camera shown
#define TALK_LINGER_SECONDS 0.2f // the camera and mic icon stay this long after the sound ends

#ifndef DEFAULT_TEAM_NAME
#define DEFAULT_TEAM_NAME "Team"
#endif
#ifndef ENEMY_TEAM_NAME
#define ENEMY_TEAM_NAME "Bad Guys"
#endif

extern SvenTV* g_sventv;
extern bool g_pause_message_logging;

PluginFuncHandle<const char*, uint64_t> g_func_web_name("HLCoop", "get_web_name_for_id");
PluginFuncHandle<void, void*, uint64_t, const char*, const char*, uint8_t> g_func_web_chat("HLCoop", "send_webserver_chat");
PluginFuncHandle<void, std::string> g_func_write_log("HLCoop", "writeLog"); // HLCoop's log of important events

#define WEBMSG_CHAT_TYPE_WEB_USER 2 // from HLCoop's web_util.h

class CHltvCamera : public CBaseEntity {
public:
	void Precache() {
		PRECACHE_MODEL(SPEC_CAMERA_MODEL);
	}

	void Spawn() {
		Precache();
		pev->solid = SOLID_NOT;
		pev->movetype = MOVETYPE_NOCLIP; // interpolated by clients. It has no velocity, so it stays put
		pev->effects |= EF_NODRAW;
		SET_MODEL(edict(), SPEC_CAMERA_MODEL);
		UTIL_SetSize(pev, Vector(-8, -8, -8), Vector(8, 8, 8));
	}
};

LINK_ENTITY_TO_CLASS(hltv_camera, CHltvCamera)

// The icon above a camera while its viewer's chat sound plays. It looks like the one the client
// draws above players who are talking (CVoiceStatus::CreateEntities), which it can't draw for a
// camera because cameras aren't players.
class CHltvCameraMic : public CBaseEntity {
public:
	void Precache() {
		PRECACHE_MODEL(SPEC_CAMERA_MIC_MODEL);
	}

	void Spawn() {
		Precache();
		pev->solid = SOLID_NOT;
		pev->movetype = MOVETYPE_NOCLIP; // interpolated, like the camera
		pev->rendermode = kRenderTransAdd;
		pev->renderamt = 255;
		pev->renderfx = kRenderFxNoDissipation;
		pev->scale = 0.5f;
		pev->effects |= EF_NODRAW;
		SET_MODEL(edict(), SPEC_CAMERA_MIC_MODEL);
		UTIL_SetSize(pev, g_vecZero, g_vecZero);
	}
};

LINK_ENTITY_TO_CLASS(hltv_camera_mic, CHltvCameraMic)

#define MIC_HEIGHT 20 // above the camera's center. The client puts it 45 above a player's.

// a stream viewer who logged in with a website token
struct WebViewer {
	uint64_t clientId; // stream client
	uint64_t steamid64;
	std::string name;
	bool nameKnown = false; // false if name is the Steam ID (see loadViewerInfo)
	float lastInfoLoad = 0;
	uint8_t topColor = 0;
	uint8_t bottomColor = 0;
	uint32_t delaySec = 0;
	StreamViewInfo view; // what they're watching
	uint8_t loadState = VIEW_LOAD_MAP; // VIEW_LOAD_*
	EHANDLE h_camera;
	EHANDLE h_mic; // the icon above the camera while their chat sound plays

	// The camera glides from where it was to the latest view the viewer sent, over about as long
	// as it took for that view to arrive. Views come in a few times a second, so jumping straight
	// to each one would look choppy.
	Vector viewOrigin; // latest view received
	Vector viewAngles;
	Vector startOrigin; // where the camera was when that view arrived
	Vector startAngles;
	float viewTime = 0; // when it arrived
	float lerpTime = 0; // how long to take getting there
};

static std::vector<WebViewer> g_viewers;
static std::vector<StreamViewer> g_sentViewers; // last list given to the stream

// players who turned the cameras on, by Steam ID so it lasts through map changes
static std::unordered_set<uint64_t> g_showSpecs;

// Viewers whose chat sound is playing, by Steam ID. Their cameras are shown to these players, with
// the mic icon above them, whether or not they turned the cameras on.
struct TalkingViewer {
	uint32_t players; // PLRBIT mask
	float endTime;
};
static std::unordered_map<uint64_t, TalkingViewer> g_talking;

// scoreboard names sent to each player for each empty slot. [receiver][slot], "" = nothing sent
static std::string g_sentSlotNames[MAX_PLAYERS + 1][MAX_PLAYERS + 1];
static int g_receiverUserIds[MAX_PLAYERS + 1];
static bool g_sentTeamNames[MAX_PLAYERS + 1]; // renamed the green team for the receiver
static float g_lastScoreboardUpdate = 0;
static uint64_t g_slotViewerIds[MAX_PLAYERS + 1]; // steam ID of the viewer shown in each slot, 0 = none

// Website users watching the stream, by Steam ID, for the chat announcements and HLCoop's log. A
// user with the stream open in several tabs counts once. Kept through map changes, while viewers
// reconnect.
struct Watcher {
	std::string name;
	uint64_t startTime; // epoch ms
	uint64_t lastSeen; // epoch ms
};
static std::unordered_map<uint64_t, Watcher> g_watchers;
// Viewers who used the stream password, by IP, for HLCoop's log. They have no other identity, so
// several connections from one IP count as one viewer.
static std::unordered_map<std::string, Watcher> g_passwordWatchers;
static uint64_t g_mapStartTime = 0; // epoch ms

void streamViewersMapInit() {
	UTIL_PrecacheOther(SPEC_CAMERA_CLASS);
	UTIL_PrecacheOther(SPEC_CAMERA_MIC_CLASS);
	g_mapStartTime = getEpochMillis();
	g_viewers.clear();
	g_talking.clear();
	g_sentViewers.clear();
	g_lastScoreboardUpdate = 0;
	memset(g_slotViewerIds, 0, sizeof(g_slotViewerIds));

	// clients forget all player info when the map changes
	for (int i = 0; i <= MAX_PLAYERS; i++) {
		g_sentTeamNames[i] = false;
		for (int k = 0; k <= MAX_PLAYERS; k++) {
			g_sentSlotNames[i][k].clear();
		}
	}
}

void streamViewersMapEnd() {
	g_viewers.clear();
	g_talking.clear();
	memset(g_slotViewerIds, 0, sizeof(g_slotViewerIds));
}

// rounded, so names that include it don't change constantly
static uint32_t roundDelay(uint32_t delayMs) {
	uint32_t s = delayMs / 1000;
	if (s < 5) {
		return 0; // live
	}
	if (s < 60) {
		return s - s % 5;
	}
	if (s < 60 * 60) {
		return s - s % 60;
	}
	return s - s % (60 * 60);
}

static std::string delaySuffix(uint32_t delaySec) {
	if (!delaySec) {
		return "";
	}
	if (delaySec < 60) {
		return UTIL_VarArgs(" (%us ago)", delaySec);
	}
	if (delaySec < 60 * 60) {
		return UTIL_VarArgs(" (%um ago)", delaySec / 60);
	}
	return UTIL_VarArgs(" (%uh ago)", delaySec / (60 * 60));
}

// removes the last UTF-8 character
static void popChar(std::string& s) {
	while (!s.empty()) {
		unsigned char c = s.back();
		s.pop_back();
		if (c < 0x80 || c >= 0xC0) {
			break;
		}
	}
}

// The name the website shows for the viewer: their in-game name, or their Steam name if they've never
// joined the server. The Steam name is fetched when they sign in on the website, so it may not be
// known yet when they start watching. Until it is, the Steam ID is shown and nameKnown stays false.
static void loadViewerInfo(WebViewer& viewer) {
	player_data_t* data = playerdb_load_state(viewer.steamid64);
	const char* webName = g_func_web_name.call(viewer.steamid64);
	std::string name = webName ? webName : "";
	if (name.empty() && data && data->steamname[0]) {
		// Steam names can be longer than the game allows, so shorten it to fit like an in-game name
		name = data->steamname;
		while (name.size() > MAX_PLAYER_NAME) {
			popChar(name);
		}
	}

	viewer.nameKnown = !name.empty();
	viewer.name = viewer.nameKnown ? name : steamid64_to_steamid(viewer.steamid64);
	viewer.lastInfoLoad = gpGlobals->time;

	if (data) {
		viewer.topColor = data->topcolor;
		viewer.bottomColor = data->bottomcolor;
	}
}

// The views a viewer sends are a little old by the time they arrive, and they come a few times a
// second, so a camera placed from them trails behind the player being spectated. A viewer watching
// live sees the players where they are now, give or take the stream delay, so their camera is put
// where the player is: in their eyes for first person, or behind them at the chasecam distance and
// the viewer's rotation (the player's yaw when locked). Watching the past, the views are used as is.
static bool isWatchingLive(const StreamClientInfo& client) {
	const StreamViewInfo& view = client.view;
	return view.known && !client.delayMs && view.target && view.camMode != VIEW_CAM_FREE;
}

static void snapCamera(const StreamClientInfo& client, Vector& origin, Vector& angles) {
	const StreamViewInfo& view = client.view;
	if (!isWatchingLive(client)) {
		return;
	}

	CBasePlayer* plr = UTIL_PlayerByIndex(view.target);
	if (!plr) {
		return;
	}

	Vector eye = plr->pev->origin + plr->pev->view_ofs;

	// model pitch is the opposite of view pitch
	if (view.camMode == VIEW_CAM_FIRST_PERSON) {
		origin = eye;
		angles = Vector(-plr->pev->v_angle.x, plr->pev->v_angle.y, 0);
		return;
	}

	if (view.camMode == VIEW_CAM_CHASE_LOCKED) {
		angles.y = plr->pev->v_angle.y;
	}

	// same as DemoViewer::updateCamera in bspguy
	Vector forward;
	UTIL_MakeVectorsPrivate(Vector(-angles.x, angles.y, 0), forward, NULL, NULL);
	Vector desired = eye - forward * view.chaseDist;

	TraceResult tr;
	UTIL_TraceLine(eye, desired, ignore_monsters, plr->edict(), &tr);
	if (tr.flFraction < 1.0f) {
		desired = eye + (desired - eye) * V_max(0.0f, tr.flFraction - 0.05f);
	}
	origin = desired;
}

static void removeEntity(EHANDLE& h) {
	CBaseEntity* ent = h.GetEntity();
	if (ent) {
		UTIL_Remove(ent);
	}
	h = NULL;
}

// Shows the mic icon above the camera to these players, or removes it if there are none
static void updateMic(WebViewer& viewer, CBaseEntity* cam, uint32_t players) {
	if (!players || !cam) {
		removeEntity(viewer.h_mic);
		return;
	}

	CBaseEntity* mic = viewer.h_mic.GetEntity();
	if (!mic) {
		mic = CBaseEntity::Create(SPEC_CAMERA_MIC_CLASS, cam->pev->origin + Vector(0, 0, MIC_HEIGHT), g_vecZero);
		if (!mic) {
			ALERT(at_error, "HLTV: failed to create a spectator camera's mic icon\n");
			return;
		}
		viewer.h_mic = mic;
	}

	UTIL_SetOrigin(mic->pev, cam->pev->origin + Vector(0, 0, MIC_HEIGHT));
	mic->pev->effects &= ~EF_NODRAW;
	mic->m_hidePlayers = ~players;
}

// showMask: players who turned the cameras on. talkMask: players who are shown this camera and its
// mic icon because the viewer's chat sound is playing.
static void updateCamera(WebViewer& viewer, const StreamClientInfo& client, uint32_t showMask, uint32_t talkMask) {
	CBaseEntity* cam = viewer.h_camera.GetEntity();

	// viewers whose player hasn't sent where it's looking from don't get a camera
	if (!client.joined || !client.hasCamera) {
		removeEntity(viewer.h_camera);
		removeEntity(viewer.h_mic);
		return;
	}

	bool isNewCamera = !cam;
	if (!cam) {
		cam = CBaseEntity::Create(SPEC_CAMERA_CLASS, g_vecZero, g_vecZero);
		if (!cam) {
			ALERT(at_error, "HLTV: failed to create a spectator camera\n");
			return;
		}
		cam->pev->colormap = viewer.topColor | (viewer.bottomColor << 8);
		viewer.h_camera = cam;
	}

	Vector origin(client.camOrigin[0], client.camOrigin[1], client.camOrigin[2]);
	// model pitch is the opposite of view pitch
	Vector angles(-client.camAngles[0], client.camAngles[1], 0);
	float now = gpGlobals->time;

	if (isNewCamera) {
		viewer.startOrigin = viewer.viewOrigin = origin;
		viewer.startAngles = viewer.viewAngles = angles;
		viewer.viewTime = now;
		viewer.lerpTime = 0;
	}
	else if (origin != viewer.viewOrigin || angles != viewer.viewAngles) {
		viewer.startOrigin = cam->pev->origin; // carry on from wherever it is now
		viewer.startAngles = cam->pev->angles;
		viewer.lerpTime = V_min(V_max(now - viewer.viewTime, MIN_CAMERA_LERP), MAX_CAMERA_LERP);
		viewer.viewOrigin = origin;
		viewer.viewAngles = angles;
		viewer.viewTime = now;
	}

	float t = viewer.lerpTime > 0 ? (now - viewer.viewTime) / viewer.lerpTime : 1;
	t = V_min(V_max(t, 0.0f), 1.0f);
	Vector camAngles;
	for (int i = 0; i < 3; i++) {
		float delta = UTIL_AngleDiff(viewer.viewAngles[i], viewer.startAngles[i]); // shortest way round
		camAngles[i] = viewer.startAngles[i] + delta * t;
	}
	Vector camOrigin = viewer.startOrigin + (viewer.viewOrigin - viewer.startOrigin) * t;
	snapCamera(client, camOrigin, camAngles);
	UTIL_SetOrigin(cam->pev, camOrigin);
	cam->pev->angles = camAngles;

	// Nobody sees EF_NODRAW entities, but the demo writer records cameras anyway. A camera in first
	// person would be inside the player's head, so it's hidden. The demo player shows those viewers'
	// names above the player's instead. While their chat sound plays, it's shown anyway, so players
	// can tell who it's from, except to the player it's inside.
	bool inHead = isWatchingLive(client) && client.view.camMode == VIEW_CAM_FIRST_PERSON;
	uint32_t visMask = inHead ? 0 : showMask;
	if (talkMask) {
		if (inHead) {
			talkMask &= ~PLRBIT((uint32_t)client.view.target);
		}
		visMask |= talkMask;
	}

	if (visMask) {
		cam->pev->effects &= ~EF_NODRAW;
	}
	else {
		cam->pev->effects |= EF_NODRAW;
	}
	cam->m_hidePlayers = ~visMask;

	updateMic(viewer, cam, talkMask);
}

static bool isSlotEmpty(int idx) {
	if (UTIL_PlayerByIndex(idx)) {
		return false;
	}
	if (g_RehldsSvs) {
		// also catches players who are still connecting
		IGameClient* client = g_RehldsSvs->GetClient(idx - 1);
		return client && !client->IsConnected();
	}
	char* info = g_engfuncs.pfnGetInfoKeyBuffer(INDEXENT(idx));
	return !info || !info[0];
}

// Arial advance widths of ' ' to '~', in 1/1000 of the font size. Arial is the scoreboard's font.
static const uint16_t g_arialWidths[95] = {
	278, 278, 355, 556, 556, 889, 667, 191, 333, 333, 389, 584, 278, 333, 278, 278,
	556, 556, 556, 556, 556, 556, 556, 556, 556, 556, 278, 278, 584, 584, 584, 556,
	1015, 667, 667, 722, 722, 667, 611, 778, 722, 278, 500, 667, 556, 833, 722, 778,
	667, 778, 722, 667, 611, 722, 667, 944, 667, 667, 611, 278, 278, 278, 469, 556,
	333, 556, 556, 500, 556, 556, 278, 556, 556, 222, 222, 500, 222, 833, 556, 556,
	556, 556, 333, 500, 278, 556, 500, 722, 500, 500, 500, 334, 260, 334, 584,
};
#define OTHER_CHAR_WIDTH 600 // for characters outside the table

// How much text fits in the scoreboard's name column, in the units of g_arialWidths. The column is
// 140 px wide at 640x480 (vgui_ScorePanel.cpp) and scales with the screen width, and so does the
// font ("Scoreboard Text" in the *_textscheme.txt files, 12 px at 640x480). The scoreboard adds two
// spaces after the name.
#define SCOREBOARD_NAME_WIDTH 11500

static int nameWidth(const std::string& s) {
	int w = 0;
	for (unsigned char c : s) {
		if (c >= 32 && c <= 126) {
			w += g_arialWidths[c - 32];
		}
		else if (c >= 0xC0) {
			w += OTHER_CHAR_WIDTH; // first byte of a UTF-8 character
		}
	}
	return w;
}

// removes the last character, which can be several bytes in UTF-8
// A name for a scoreboard slot. The suffix is always shown in full, so the name is shortened if
// there isn't room for both.
static std::string slotName(std::string name, const std::string& suffix = "") {
	// these would break the userinfo string
	std::replace(name.begin(), name.end(), '\\', '/');
	std::replace(name.begin(), name.end(), '"', '\'');

	while (!name.empty() && name.size() + suffix.size() > MAX_SLOT_NAME) {
		popChar(name);
	}
	if (!suffix.empty()) {
		while (!name.empty() && nameWidth(name + suffix) > SCOREBOARD_NAME_WIDTH) {
			popChar(name);
		}
		while (!name.empty() && name.back() == ' ') {
			name.pop_back();
		}
	}
	return name + suffix;
}

// The viewer is watching the receiver live, so the receiver's scoreboard says they're on them
static bool isSpectating(const WebViewer& viewer, int playerIdx) {
	const StreamViewInfo& view = viewer.view;
	return view.known && view.target == playerIdx && view.camMode != VIEW_CAM_FREE && !viewer.delaySec;
}

static std::string viewerSlotName(const WebViewer& viewer, int receiver) {
	std::string suffix = delaySuffix(viewer.delaySec);
	if (isSpectating(viewer, receiver)) {
		suffix += viewer.view.camMode == VIEW_CAM_FIRST_PERSON ? " (in you)" : " (on you)";
	}
	return slotName(viewer.name, suffix);
}

// shows a fake player in an empty slot on a player's scoreboard, or removes it if name is empty
static void sendSlot(CBasePlayer* to, int slot, const std::string& name) {
	g_pause_message_logging = true; // not part of the demo

	std::string info = name.empty() ? "" : "\\name\\" + name + "\\model\\player\\topcolor\\0\\bottomcolor\\0";
	UTIL_SendUserInfo(to->edict(), INDEXENT(slot), (char*)info.c_str());

	if (!name.empty()) {
		MESSAGE_BEGIN(MSG_ONE, g_umsg.ScoreInfo, NULL, to->edict());
		WRITE_BYTE(slot);
		WRITE_SHORT(0); // score
		WRITE_SHORT(0); // deaths
		WRITE_SHORT(WEB_SPEC_STATUS);
		WRITE_SHORT(WEB_SPEC_COLOR);
		MESSAGE_END();
	}

	MESSAGE_BEGIN(MSG_ONE, g_umsg.TeamInfo, NULL, to->edict());
	WRITE_BYTE(slot);
	WRITE_STRING(name.empty() ? "" : WEB_SPEC_TEAM);
	MESSAGE_END();

	g_pause_message_logging = false;
}

// The scoreboard labels each team with the name of its color, not the team name in TeamInfo.
// These are the names the game sends when a player joins, except green becomes the viewers' team.
static void sendTeamNames(CBasePlayer* to) {
	g_pause_message_logging = true; // not part of the demo
	MESSAGE_BEGIN(MSG_ONE, g_umsg.TeamNames, NULL, to->edict());
	WRITE_BYTE(4);
	WRITE_STRING(DEFAULT_TEAM_NAME); // blue
	WRITE_STRING(ENEMY_TEAM_NAME); // red
	WRITE_STRING(DEFAULT_TEAM_NAME); // yellow
	WRITE_STRING(WEB_SPEC_TEAM); // green (WEB_SPEC_COLOR)
	MESSAGE_END();
	g_pause_message_logging = false;
}

// Lists the viewers in empty player slots. The highest slots are used so that the team sorts below
// the players when scores are tied, except the last one (see below). If the server is full, nothing is shown. Viewers who are still
// loading the map aren't shown, because they can't see the game yet.
//
// One empty slot is left out for the chatsounds_hl plugin. When a player's voice is playing, it
// plays their chat sound from the highest empty slot below maxClients that isn't showing a viewer
// (get_stream_viewer_slots), so that the two streams don't interrupt each other. A viewer's name
// there would make it look like the viewer made the sound. If only one slot is empty, the viewers
// get it, and chat sounds play from the player's own slot.
static void updateScoreboard() {
	std::vector<const WebViewer*> viewers;
	for (const WebViewer& viewer : g_viewers) {
		if (viewer.loadState != VIEW_LOAD_MAP) {
			viewers.push_back(&viewer);
		}
	}

	bool empty[MAX_PLAYERS + 1] = { false };
	std::vector<int> emptySlots;
	for (int i = 1; i <= gpGlobals->maxClients && i <= MAX_PLAYERS; i++) {
		empty[i] = isSlotEmpty(i);
		if (empty[i]) {
			emptySlots.push_back(i);
		}
	}

	for (int i = (int)emptySlots.size() - 1; i >= 0 && emptySlots.size() > 1; i--) {
		if (emptySlots[i] < gpGlobals->maxClients) {
			emptySlots.erase(emptySlots.begin() + i); // the chat sound slot
			break;
		}
	}

	// which viewer goes in each slot. The names can differ for each player, who sees " (on you)"
	// (or " (in you)" in first person) after the viewers spectating them.
	const WebViewer* slotViewers[MAX_PLAYERS + 1] = { NULL };
	std::string moreName[MAX_PLAYERS + 1]; // "(+N more viewers)" when they don't all fit
	int numSlots = emptySlots.size();
	int numNamed = viewers.size();

	// Players hear no voice from the last slot (maxClients): the label flickers with no sound, which
	// is why chatsounds_hl never plays from it. Web viewers' chat sounds play through their slot, so
	// they get it last. The "(+N more viewers)" entry never talks, so when there is one, it gets it.
	// Viewers are placed from the end of emptySlots, so it goes to the front.
	if (numNamed <= numSlots && numSlots > 1 && emptySlots.back() == gpGlobals->maxClients) {
		emptySlots.pop_back();
		emptySlots.insert(emptySlots.begin(), gpGlobals->maxClients);
	}

	if (numSlots && numNamed) {
		int shown = numNamed <= numSlots ? numNamed : numSlots - 1;
		int more = numNamed - shown;
		int first = numSlots - (shown + (more > 0 ? 1 : 0));

		for (int i = 0; i < shown; i++) {
			slotViewers[emptySlots[first + i]] = viewers[i];
		}
		if (more > 0) {
			moreName[emptySlots[first + shown]] = UTIL_VarArgs("(+%d more viewer%s)", more, more == 1 ? "" : "s");
		}
	}
	for (int s = 0; s <= MAX_PLAYERS; s++) {
		g_slotViewerIds[s] = slotViewers[s] ? slotViewers[s]->steamid64 : 0;
	}

	for (int r = 1; r <= gpGlobals->maxClients && r <= MAX_PLAYERS; r++) {
		CBasePlayer* plr = UTIL_PlayerByIndex(r);
		int userid = plr ? g_engfuncs.pfnGetPlayerUserId(plr->edict()) : -1;

		if (userid != g_receiverUserIds[r]) {
			// a different player, who hasn't been sent anything
			g_receiverUserIds[r] = userid;
			g_sentTeamNames[r] = false;
			for (int s = 0; s <= MAX_PLAYERS; s++) {
				g_sentSlotNames[r][s].clear();
			}
		}

		if (!plr || (plr->pev->flags & FL_FAKECLIENT) || plr->m_fInitHUD) {
			continue;
		}

		if (!g_sentTeamNames[r]) {
			sendTeamNames(plr);
			g_sentTeamNames[r] = true;
		}

		for (int s = 1; s <= gpGlobals->maxClients && s <= MAX_PLAYERS; s++) {
			std::string& sent = g_sentSlotNames[r][s];
			if (!empty[s]) {
				sent.clear(); // a player took the slot, and the engine sent their info
				continue;
			}
			std::string wanted = slotViewers[s] ? viewerSlotName(*slotViewers[s], r) : moreName[s];
			if (sent != wanted) {
				sendSlot(plr, s, wanted);
				sent = wanted;
			}
		}
	}
}

uint64_t getStreamViewerSlots() {
	uint64_t slots = 0;
	for (int r = 1; r <= MAX_PLAYERS; r++) {
		for (int s = 1; s <= MAX_PLAYERS; s++) {
			if (!g_sentSlotNames[r][s].empty()) {
				slots |= 1ULL << s;
			}
		}
	}
	return slots;
}

int getStreamViewerSoundSource(uint64_t steamid64, int& cameraEnt) {
	cameraEnt = 0;
	int slot = 0;
	// no voice plays from the last slot (see updateScoreboard). A viewer only gets it when every
	// other slot is taken.
	for (int s = 1; s < gpGlobals->maxClients && s <= MAX_PLAYERS && !slot; s++) {
		if (g_slotViewerIds[s] == steamid64 && isSlotEmpty(s)) {
			slot = s;
		}
	}

	// the same user can watch in several tabs, each with a camera. Any of them will do.
	for (WebViewer& viewer : g_viewers) {
		CBaseEntity* cam = viewer.h_camera.GetEntity();
		if (slot && viewer.steamid64 == steamid64 && cam) {
			cameraEnt = cam->entindex();
			return slot;
		}
	}
	return 0;
}

void setStreamViewerTalking(uint64_t steamid64, uint32_t players, float seconds) {
	if (!steamid64) {
		return;
	}
	if (!players) {
		g_talking.erase(steamid64);
		return;
	}

	float endTime = gpGlobals->time + V_min(V_max(seconds, 0.0f), MAX_TALK_SECONDS) + TALK_LINGER_SECONDS;
	auto it = g_talking.find(steamid64);
	if (seconds <= 0) {
		// the sound was stopped early. It still lingers, but never past when it would have ended.
		if (it != g_talking.end()) {
			it->second.endTime = V_min(it->second.endTime, endTime);
		}
		return;
	}

	TalkingViewer& talking = g_talking[steamid64];
	talking.players = players;
	talking.endTime = endTime;
}

// Says in the game's chat and the website's chat that someone started or stopped watching, like
// the game's own spectating messages: "(WEB) <name> is now spectating" / "... stopped spectating".
static void announceWatcher(uint64_t steamid64, const std::string& name, bool started) {
	if (!g_stream_announce || g_stream_announce->value <= 0) {
		return;
	}

	const char* action = started ? "is now spectating" : "stopped spectating";

	// The name is green, like chat sent from the website. Like HLCoop's web chat relay, this
	// briefly gives a player the viewer's name and team color, so the message needs a player
	// to send it, and there's nobody to tell if there isn't one.
	CBasePlayer* anyPlayer = NULL;
	for (int i = 1; i <= gpGlobals->maxClients && !anyPlayer; i++) {
		anyPlayer = UTIL_PlayerByIndex(i);
	}
	if (anyPlayer) {
		std::string prefix = "(WEB) " + name + " ";
		UTIL_ClientSay(anyPlayer, action, prefix.c_str(), false, NULL, 0, FRIEND_TEAM_COLOR, name.c_str());
	}

	// The website shows web users' names as a green "(WEB) <name>" in place of ;name;
	std::string webmsg = std::string(";name; ") + action;
	g_func_web_chat.call(NULL, steamid64, name.c_str(), webmsg.c_str(), WEBMSG_CHAT_TYPE_WEB_USER);
}

static bool isWatchStopped(const Watcher& watcher, uint64_t now) {
	if (watcher.lastSeen >= g_mapStartTime) {
		return now - watcher.lastSeen >= WATCH_END_MS;
	}
	// last seen on the previous map. They can't reconnect until this one has started, however long
	// the map change took.
	return now - g_mapStartTime >= WATCH_END_MAP_CHANGE_MS;
}

static std::string watchDuration(const Watcher& watcher) {
	uint64_t sec = (watcher.lastSeen - watcher.startTime) / 1000;
	if (sec < 60) {
		return UTIL_VarArgs("%d sec", (int)sec);
	}
	if (sec < 60 * 60) {
		return UTIL_VarArgs("%d min", (int)(sec / 60));
	}
	return UTIL_VarArgs("%dh %02dm", (int)(sec / 3600), (int)(sec / 60 % 60));
}

// "1.2.3.4:27015" -> "1.2.3.4"
static std::string addressIp(const std::string& address) {
	size_t colon = address.rfind(':');
	return colon == std::string::npos ? address : address.substr(0, colon);
}

// passwordClients: connections that logged in with the stream password
static void updateWatchers(const std::vector<StreamClientInfo>& passwordClients) {
	uint64_t now = getEpochMillis();

	for (const WebViewer& viewer : g_viewers) {
		auto it = g_watchers.find(viewer.steamid64);
		if (it == g_watchers.end()) {
			Watcher watcher;
			watcher.name = viewer.name;
			watcher.startTime = watcher.lastSeen = now;
			g_watchers[viewer.steamid64] = watcher;
			announceWatcher(viewer.steamid64, viewer.name, true);
			g_func_write_log.call(UTIL_VarArgs("!HLTV:`%s` (%s) started watching the stream", viewer.name.c_str(),
				steamid64_to_steamid(viewer.steamid64).c_str()));
		}
		else {
			it->second.lastSeen = now;
		}
	}

	for (auto it = g_watchers.begin(); it != g_watchers.end();) {
		if (isWatchStopped(it->second, now)) {
			announceWatcher(it->first, it->second.name, false);
			g_func_write_log.call(UTIL_VarArgs("!HLTV:`%s` (%s) stopped watching the stream after %s",
				it->second.name.c_str(), steamid64_to_steamid(it->first).c_str(), watchDuration(it->second).c_str()));
			it = g_watchers.erase(it);
		}
		else {
			++it;
		}
	}

	for (const StreamClientInfo& client : passwordClients) {
		std::string ip = addressIp(client.address);
		auto it = g_passwordWatchers.find(ip);
		if (it == g_passwordWatchers.end()) {
			Watcher watcher;
			watcher.name = ip;
			watcher.startTime = watcher.lastSeen = now;
			g_passwordWatchers[ip] = watcher;
			g_func_write_log.call(UTIL_VarArgs("!HLTV:%s started watching the stream with the stream password", ip.c_str()));
		}
		else {
			it->second.lastSeen = now;
		}
	}

	for (auto it = g_passwordWatchers.begin(); it != g_passwordWatchers.end();) {
		if (isWatchStopped(it->second, now)) {
			g_func_write_log.call(UTIL_VarArgs("!HLTV:%s stopped watching the stream with the stream password after %s",
				it->first.c_str(), watchDuration(it->second).c_str()));
			it = g_passwordWatchers.erase(it);
		}
		else {
			++it;
		}
	}
}

void updateStreamViewers() {
	std::vector<StreamClientInfo> clients;
	if (g_sventv && g_sventv->enableStream) {
		clients = g_sventv->getStreamClients();
	}

	// viewers who used the stream password have no Steam ID, and aren't shown. They're only logged.
	std::vector<StreamClientInfo> passwordClients;
	for (const StreamClientInfo& c : clients) {
		if (c.authed && !c.steamid64) {
			passwordClients.push_back(c);
		}
	}
	clients.erase(std::remove_if(clients.begin(), clients.end(), [](const StreamClientInfo& c) {
		return !c.authed || !c.steamid64;
	}), clients.end());

	g_viewers.erase(std::remove_if(g_viewers.begin(), g_viewers.end(), [&](WebViewer& viewer) {
		bool watching = std::any_of(clients.begin(), clients.end(), [&](const StreamClientInfo& c) {
			return c.id == viewer.clientId;
		});
		if (!watching) {
			removeEntity(viewer.h_camera);
			removeEntity(viewer.h_mic);
		}
		return !watching;
	}), g_viewers.end());

	uint32_t showMask = 0;
	uint32_t playerMask = 0;
	for (int i = 1; i <= gpGlobals->maxClients; i++) {
		CBasePlayer* plr = UTIL_PlayerByIndex(i);
		if (plr) {
			playerMask |= PLRBIT(i);
		}
		if (plr && g_showSpecs.count(plr->GetSteamID64())) {
			showMask |= PLRBIT(i);
		}
	}

	for (auto it = g_talking.begin(); it != g_talking.end();) {
		float left = it->second.endTime - gpGlobals->time;
		if (left <= 0 || left > MAX_TALK_SECONDS + TALK_LINGER_SECONDS) {
			it = g_talking.erase(it); // finished, or the clock went backwards
		}
		else {
			++it;
		}
	}

	for (const StreamClientInfo& client : clients) {
		WebViewer* viewer = NULL;
		for (WebViewer& v : g_viewers) {
			if (v.clientId == client.id) {
				viewer = &v;
				break;
			}
		}

		if (!viewer) {
			WebViewer newViewer;
			newViewer.clientId = client.id;
			newViewer.steamid64 = client.steamid64;
			loadViewerInfo(newViewer);
			g_viewers.push_back(newViewer);
			viewer = &g_viewers.back();
		}
		else if (!viewer->nameKnown && (gpGlobals->time - viewer->lastInfoLoad >= 1.0f
			|| viewer->lastInfoLoad > gpGlobals->time)) {
			loadViewerInfo(*viewer);
		}

		viewer->delaySec = client.hasCamera ? roundDelay(client.delayMs) : 0;
		viewer->view = client.joined ? client.view : StreamViewInfo();
		viewer->loadState = client.loadState;
		auto talking = g_talking.find(viewer->steamid64);
		uint32_t talkMask = talking != g_talking.end() ? talking->second.players & playerMask : 0;
		updateCamera(*viewer, client, showMask, talkMask);
	}

	std::vector<StreamViewer> list;
	for (WebViewer& viewer : g_viewers) {
		CBaseEntity* cam = viewer.h_camera.GetEntity();
		StreamViewer sv;
		sv.clientId = viewer.clientId;
		sv.steamid64 = viewer.steamid64;
		sv.entindex = cam ? cam->entindex() : 0;
		sv.topColor = viewer.topColor;
		sv.bottomColor = viewer.bottomColor;
		sv.delaySec = viewer.delaySec;
		sv.name = viewer.name;
		sv.view = viewer.view;
		list.push_back(sv);
	}
	if (g_sventv && list != g_sentViewers) {
		g_sventv->setStreamViewers(list);
		g_sentViewers = list;
	}

	updateWatchers(passwordClients);

	if (gpGlobals->time - g_lastScoreboardUpdate >= SCOREBOARD_UPDATE_SECONDS || g_lastScoreboardUpdate > gpGlobals->time) {
		g_lastScoreboardUpdate = gpGlobals->time;
		updateScoreboard();
	}
}

bool cmd_showspecs(CBasePlayer* plr, const CommandArgs& args) {
	if (!plr) {
		return false;
	}

	uint64_t steamid64 = plr->GetSteamID64();
	bool show = !g_showSpecs.count(steamid64);
	if (args.ArgC() > 1) {
		show = atoi(args.ArgV(1).c_str()) != 0;
	}

	if (show) {
		g_showSpecs.insert(steamid64);
		UTIL_ClientPrint(plr, print_chat, "[HLTV] Showing the cameras of stream viewers.\n");
	}
	else {
		g_showSpecs.erase(steamid64);
		UTIL_ClientPrint(plr, print_chat, "[HLTV] Hiding the cameras of stream viewers.\n");
	}

	return true;
}
#endif
