#pragma once
#ifdef HLCOOP_BUILD
#include "extdll.h"
#include "util.h"
#include "CBaseEntity.h"
#define MAX_PLAYERS 32
#else
#include "meta_init.h"
#define EHANDLE EHandle
#endif

#include "mstream.h"
#include "DemoFile.h"
#include "NetClient.h"

// TODO:
// - shocktrooper beam
// - gonome/hgrunt gait
// - apache bullets
// - sentence mouth controller (scientist)
// - some sounds have no attn? (gib sound)
// - player footsteps (+swimming effects?)
// - save cvars to demo
// - charging wrench triggers constant animation resets (frame bytes)
// - do monsters ever reset sequences to a non-zero frame? (undo frame opt)
// - punchangle prediction
// - controller interp
// - capture SetView calls
// - NetMessage compression is WIP and broke replays or isn't working I forget

// optimize ideas:
// - init framerate to 16
// - predict angle from avelocity
// - predict origin from velocity

struct InterpInfo {
	Vector originStart;
	Vector originEnd;

	Vector anglesStart;
	Vector anglesEnd;

	float frameStart;
	float frameEnd; // used as a reference point for monsters
	float interpFrame; // for resetting when predicted frame is close enough to the real one

	float framerateEnt; // framerate modifier set in the entity
	float framerateSmd; // framerate of the model animation
	float groundspeed; // movement speed of the model animation on the ground
	float animTime; // world time when frame was set
	float lastMovementTime; // last time origin/angles were changed
	float estimatedUpdateDelay; // used to guess a framerate for origin/angle interpolation

	int sequenceStart;
	int sequenceEnd;
	bool sequenceLoops;
};

struct ReplayEntity {
	EHANDLE h_ent;
	InterpInfo interp;
};

class DemoDataStream {
public:
	DemoDataStream(FILE* file) : fileStream(file) {}
	DemoDataStream(mstream& mem) : memoryStream(mem) {}

	void seek(uint64_t to);
	size_t read(void* dst, size_t sz);
	void close();
	bool valid();
	size_t tell();

private:
	FILE* fileStream = NULL;
	mstream memoryStream;
};

struct DemoDataTest {
	DemoFrame header;
	uint32_t demoTime;
	uint32_t numEntDeltas;
	uint32_t playerDeltaBits;
	uint32_t msgCount;
	uint32_t evtCount;
	uint32_t cmdCount;
	uint32_t usrCount[32];
	bool success;

	int entDeltaSz[ABSOLUTE_MAX_EDICTS]; // hopefully big enough
	netedict oldEntState[ABSOLUTE_MAX_EDICTS];
	netedict newEntState[ABSOLUTE_MAX_EDICTS];

	NetMessageData expectedMsg[MAX_NETMSG_FRAME];
	DemoUserCmdData oldUsercmdState[32];
	DemoUserCmdData newUsercmdState[32];
};

class DemoPlayer {
public:
	const float demoFileFps = 60; // TODO: calculate this or smth
	bool clearMapForPlayback = true; // map may crash if entities are not cleared first
	bool useBots = true; // try to use bots for player entites
	float replaySpeed = 1.0f;
	int netmsgPlrIdx = 1; // player to replay network messages for
	DemoHeader demoHeader;

	DemoPlayer();
	~DemoPlayer();

	// validates and prepares demo file for playback
	// plr = player who opened the file
	bool openDemo(CBasePlayer* plr, string path, float offsetSeconds, bool skipPrecache = false);

	// call this every frame to replay the demo
	void playDemo();

	// call in MapInit after loading a demo file and restarting the map
	void precacheLastDemo();

	void stopReplay();

	bool isPlaying();

	// clears existing map entities for playback
	void prepareDemo();

	edict_t* getReplayEntity(int idx);

	// convert a model index read from a demo file into a model path
	const char* getReplayModel(uint16_t modelIdx);

	void validateFrame(DemoDataStream& reader, DemoDataTest* results);

	int GetWeaponData(edict_t* player, weapon_data_t* info);

	void OverrideClientData(const edict_t* ent, int sendweapons, clientdata_t* cd);

	void seek(int offsetSeconds, bool relative);

	void setPlaybackSpeed(float speed);

	// search the demo file for a player command
	void searchCommand(CBasePlayer* searcher, string searchStr);

	// return playback position in milliseconds
	uint32_t getPlaybackTime();

private:
	// vars for replaying a demo file
	DemoDataStream* replayData = NULL;
	uint32_t firstFrameOffset = 0;
	bool isValidating = false; // if true, demo data is being validated, so don't mess with the input data
	vector<string> precacheModels;
	vector<string> precacheSounds;
	uint64_t replayStartTime = 0;
	uint32_t replayFrame = 0;
	uint32_t nextFrameOffset = 0;
	uint64_t nextFrameTime = 0;
	uint32_t lastFrameDemoTime = 0;
	vector<ReplayEntity> replayEnts;
	map<int, string> replayModelPath; // maps model index in demo file to a path
	map<int, string> replaySoundPath; // maps a sound index in a demo file to a path
	DemoFrame lastReplayFrame;
	char* stringPool = NULL;
	string cmdSearchStr;
	CBasePlayer* searchingPlayer = NULL;
	vector<string> searchResults;
	bool wasSeeking;

	netedict* fileedicts = NULL; // last edicts read from file
	DemoUserCmdData usrstates[32];

	float offsetSeconds;
	float frameProgress; // for interpolation (0-1 progress to next frame)

	string oldPlayerNames[MAX_PLAYERS]; // for resetting conflicted names after the replay finishes

	void closeReplayFile();

	// returns true if more frames are needed to catch up with current playback time
	// if validate passed, only validate the frame and set results in the var passed
	bool readDemoFrame(DemoDataTest* validate=NULL);

	bool simulate(DemoFrame& header); // create entities and replay the demo through them
	bool readEntDeltas(mstream& reader, DemoDataTest* validate = NULL);
	bool readPlayerDeltas(mstream& reader, DemoDataTest* validate = NULL);
	bool readNetworkMessages(mstream& reader, DemoDataTest* validate=NULL, bool seeking=false); // only validate if given a validate arg
	bool readEvents(mstream& reader, DemoDataTest* validate = NULL, bool seeking = false);
	bool readUserCmds(mstream& reader, DemoDataTest* validate = NULL, bool seeking = false);
	bool readClientCommands(mstream& reader, DemoDataTest* validate = NULL, bool seeking = false);
	bool readPlayerSprays(mstream& reader, DemoDataTest* validate = NULL, bool seeking = false);
	
	// converts a simulated entity into a class best suited for the demo entity
	// i = index into replayEntities
	edict_t* convertEdictType(edict_t* ent, int i);

	// creates new generic demo entities until reaching count
	// returns false on fatal error
	bool createReplayEntities(int count);

	// set new interpolation start/end points for the given entity
	// i = index into replayEntities
	void setupInterpolation(edict_t* ent, int i);

	void interpolateEdicts();
	
	void updateVisibility();

	void decompressNetMessage(NetMessageData& msg);

	// update network message data to point to the proper entities
	// return 0 = do not send, 1 = send, -1 = error parsing
	int processDemoNetMessage(NetMessageData& msg, DemoDataTest* validate);

	// really like 127 different messages
	bool processTempEntityMessage(NetMessageData& msg, DemoDataTest* validate);

	void processUserCmd(int playerindex);

	// ent = replay entitiy to update pitch/gait angles
	// dt = seconds between current time and last time
	void updatePlayerModelGait(edict_t* ent, float dt);

	void updatePlayerModelPitchBlend(edict_t* ent);

	// convert from a demo file entity index to an entity index in the current game
	void convReplayEntIdx(byte* dat, int offset, int dataSz);

	// convert from a demo file model idx to a model idx in the current game
	void convReplayModelIdx(byte* dat, int offset, int dataSz);

	// convert from a demo file sound idx to a sound idx idx in the current game
	bool convReplaySoundIdx(uint16_t& soundIdx);

	void writePings();

	friend class DemoWriter;
};