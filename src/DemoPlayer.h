#pragma once
#include "meta_init.h"
#include "mstream.h"
#include "DemoFile.h"
#include "NetClient.h"

// TODO:
// - shocktrooper beam
// - gonome/hgrunt gait
// - apache bullets
// - sentence mouth controller (scientist)
// - check chat color
// - some sounds have no attn?
// - say commands wrong
// - player footsteps (+swimming effects?)

class DemoPlayer {
public:
	const float demoFileFps = 60; // TODO: calculate this or smth
	bool clearMapForPlayback = false; // map may crash if entities are not cleared first
	bool useBots = true; // try to use bots for player entites

	DemoPlayer();
	~DemoPlayer();

	// validates and prepares demo file for playback
	// plr = player who opened the file
	bool openDemo(edict_t* plr, string path, float offsetSeconds, bool skipPrecache = false);

	// call this every frame to replay the demo
	void playDemo();

	// call in MapInit after loading a demo file
	void precacheDemo();

	void stopReplay();

private:
	// vars for replaying a demo file
	FILE* replayFile = NULL;
	vector<string> precacheModels;
	vector<string> precacheSounds;
	uint64_t replayStartTime = 0;
	uint32_t replayFrame = 0;
	uint32_t nextFrameOffset = 0;
	uint64_t nextFrameTime = 0;
	vector<EHandle> replayEnts;
	map<int, string> replayModelPath; // maps model index in demo file to a path
	DemoHeader demoHeader;
	DemoFrame lastReplayFrame;

	DemoPlayerEnt* fileplayerinfos = NULL; // last infos read from file
	netedict* fileedicts = NULL; // last edicts read from file

	float offsetSeconds;
	float frameProgress; // for interpolation (0-1 progress to next frame)

	bool initBots();

	void closeReplayFile();

	// returns true if more frames are needed to catch up with current playback time
	bool readDemoFrame();

	bool applyEntDeltas(DemoFrame& header);
	bool readEntDeltas(mstream& reader);
	bool readPlayerDeltas(mstream& reader);
	bool readNetworkMessages(mstream& reader);
	bool readClientCommands(mstream& reader);

	void interpolateEdicts();

	// update network message data to point to the proper entities
	// return false if message should not be sent
	bool processDemoNetMessage(NetMessageData& msg);

	// really like 127 different messages
	bool processTempEntityMessage(NetMessageData& msg);

	// clears existing map entities for playback
	void prepareDemo();

	// ent = replay entitiy to update pitch/gait angles
	// dt = seconds between current time and last time
	void updatePlayerModelGait(edict_t* ent, float dt);

	void updatePlayerModelPitchBlend(edict_t* ent);

	// convert a model index read from a demo file into a model path
	string getReplayModel(uint16_t modelIdx);

	// convert from a demo file entity index to an entity index in the current game
	void convReplayEntIdx(uint16_t& eidx);

	// convert from a demo file model idx to a model idx in the current game
	void convReplayModelIdx(uint16_t& modelIdx);

	// convert from a demo file sound idx to a sound idx idx in the current game
	void convReplaySoundIdx(uint16_t& soundIdx);

	bool validateEdicts(); // debug
};