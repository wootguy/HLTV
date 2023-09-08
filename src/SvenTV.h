#pragma once
#include "meta_init.h"
#include "ThreadSafeInt.h"
#include <thread>
#include <mutex>
#include "Socket.h"
#include "NetClient.h"
#include "mstream.h"

#define SVENTV_PORT 28015
#define MAX_CLIENTS 64
#define MAX_CMD_FRAME 1024 // max client commands per frame
#define MAX_NETMSG_FRAME 1024 // max network messages per frame
#define MAX_NETMSG_DATA 512 // max bytes before overflow message from game
#define MAX_CMD_LENGTH 128
#define KEYFRAME_INTERVAL 60ULL // seconds between keyframes in demo files

#define SVC_PACKETENTITIES 40
#define SVC_DELTAPACKETENTITIES 41
#define SVC_WELCOME 255 // new client connect ack

// client packet types
#define CLC_CONNECT 1 
#define CLC_DELTA_ACK 2
#define CLC_DELTA_RESET 3

// flags for indicating which edict fields were updated
#define FL_DELTA_ORIGIN_X		(1 << 0)
#define FL_DELTA_ORIGIN_Y		(1 << 1)
#define FL_DELTA_ORIGIN_Z		(1 << 2)
#define FL_DELTA_ANGLES_X		(1 << 3)
#define FL_DELTA_ANGLES_Y		(1 << 4)
#define FL_DELTA_ANGLES_Z		(1 << 5)
#define FL_DELTA_MODELINDEX		(1 << 6)
#define FL_DELTA_SKIN			(1 << 7)
#define FL_DELTA_BODY			(1 << 8)
#define FL_DELTA_EFFECTS		(1 << 9)
#define FL_DELTA_SEQUENCE		(1 << 10)
#define FL_DELTA_GAITSEQUENCE	(1 << 11)
#define FL_DELTA_FRAME			(1 << 12)
#define FL_DELTA_ANIMTIME		(1 << 13)
#define FL_DELTA_FRAMERATE		(1 << 14)
#define FL_DELTA_CONTROLLER_0	(1 << 15)
#define FL_DELTA_CONTROLLER_1	(1 << 16)
#define FL_DELTA_CONTROLLER_2	(1 << 17)
#define FL_DELTA_CONTROLLER_3	(1 << 18)
#define FL_DELTA_BLENDING_0		(1 << 19)
#define FL_DELTA_BLENDING_1		(1 << 20)
#define FL_DELTA_SCALE			(1 << 21)
#define FL_DELTA_RENDERMODE		(1 << 22)
#define FL_DELTA_RENDERAMT		(1 << 23)
#define FL_DELTA_RENDERCOLOR_0	(1 << 24)
#define FL_DELTA_RENDERCOLOR_1	(1 << 25)
#define FL_DELTA_RENDERCOLOR_2	(1 << 26)
#define FL_DELTA_RENDERFX		(1 << 27)
#define FL_DELTA_AIMENT			(1 << 28)
#define FL_DELTA_HEALTH			(1 << 29)
#define FL_DELTA_COLORMAP		(1 << 30)

enum edict_copy_states {
	EDICT_COPY_REQUESTED,
	EDICT_COPY_FINISHED,
};

enum delta_results {
	EDELTA_NONE, // there were no differences between the edicts
	EDELTA_WRITE, // there were differences between the edicts
	EDELTA_OVERFLOW, // there were differences but there is no room to write them
};

#pragma pack(push, 1)

#define DEMO_VERSION 1

struct DemoHeader {
	uint16_t version; // demo file version
	uint64_t startTime; // epoch time when demo recording started
	uint64_t endTime; // epoch time when demo recording stopped (0 = server crashed before finishing)
	char mapname[64];
	uint8_t maxPlayers;
	uint32_t modelIdxStart; // idx of the first non-bsp model
	uint32_t modelLen; // size of model string data (model idx = string idx + modelIdxStart)
	uint32_t soundLen; // size of sound string data (sound idx = string idx)
	// modelLen bytes of model strings delimtted by \n
	// soundLen bytes of sound strings delimtted by \n
};

// extra info for player entities (combined with netedict data)
struct DemoPlayer {
	bool isConnected;
	char name[32];
	char model[23];
	uint64_t steamid64;
	uint8_t topColor;
	uint8_t bottomColor;
	uint16_t ping;
	uint16_t pmMoveCounter; // detect client FPS and lag spikes

	// player-specific entvars for 1st/3rd person views and scoreboard
	uint16_t	viewmodel;		// 1st-person weapon model
	uint16_t	weaponmodel;	// 3rd-person weapon model
	uint16_t	armorvalue;
	uint16_t	button;
	uint16_t	frags;
	int16_t		view_ofs;	// eye position (Z) (12.4 fixed point)
	uint8_t		fov;
	uint8_t		weaponanim;
	uint8_t		iuser1 : 2; // observer mode
	uint8_t		iuser2 : 6; // observer target

	// weapon info
	uint16_t	clip;
	uint16_t	clip2;
	uint16_t	ammo;
	uint16_t	ammo2;
};

struct DemoPlayerDelta {
	uint8_t isConnectedChanged : 1;
	uint8_t nameChanged : 1;
	uint8_t modelChanged : 1;
	uint8_t steamIdChanged : 1;
	uint8_t colorsChanged : 1;
	uint8_t pingChanged : 1;
	uint8_t pmMoveChanged : 1;
	uint8_t viewmodelChanged : 1;

	uint8_t weaponmodelChanged : 1;
	uint8_t weaponanimChanged : 1;
	uint8_t armorvalueChanged : 1;
	uint8_t buttonChanged : 1;
	uint8_t view_ofsChanged : 1;
	uint8_t fragsChanged : 1;
	uint8_t fovChanged : 1;

	uint8_t clipChanged : 1;
	uint8_t clip2Changed : 1;
	uint8_t ammoChanged : 1;
	uint8_t ammo2Changed : 1;
	uint8_t observerChanged : 1;

	// if isConnectedChanged:
	//     uint8 = is connected
	// if name changed:
	//     uint8 = name length
	//     char[] = name bytes (max 31)
	// if model changed:
	//     uint8 = model length
	//     char[] = model bytes (max 22)
	// if steamIdChanged:
	//     uint64_t = steamid64 bytes
	// if colorsChanged:
	//     byte = top color
	//     byte = bottom color
	// if ping changed:
	//		uint16 = ping
	// if pmMoveDeltaChanged:
	//      uint8 = movement commands since last delta
	
	// max bytes = 2 + 33 + 22 + 8 + 2 + 2 + 1 = 70
};

struct DemoCommand {
	uint8_t idx; //  entity index. 0 = server.
	uint8_t len; // length of command
	// byte[] = command bytes
};

struct DemoFrame {
	uint32_t demoTime; // milliseconds since recording started
	uint32_t frameSize; // total size of this frame, including this header

	uint8_t deltaFrames; // server frames since last demoFrame (for server fps)

	uint8_t isKeyFrame : 1; // if true, zero out entity and player info for a full update
	uint8_t hasEntityDeltas : 1;
	uint8_t hasNetworkMessages : 1;
	uint8_t hasPlayerDeltas : 1;
	uint8_t hasCommands : 1;
};

struct NetMessageData {
	uint16_t sz;
	uint8_t type;
	uint8_t dest : 4;
	uint8_t hasOrigin : 1;
	uint8_t hasEdict : 1;
	float origin[3];
	uint16_t eidx;
	byte data[512];
};

struct CommandData {
	uint8_t idx;
	uint8_t len;
	byte data[128];
};

struct DemoNetMessage {
	uint16_t sz; // network message data bytes
	uint8_t type;
	uint8_t dest : 4;
	uint8_t hasOrigin : 1;
	uint8_t hasEdict : 1;
	// if hasOrigin:
	//     float[3] = origin
	// if hasEdict:
	//     uint16_t = edict index
	// byte[] = message bytes
};

// File layout:
// DemoHeader
// DemoFrame[]

// DemoFrame layout:
// DemoFrame = header
// if hasEntityDeltas:
//     uint16 = count of entity deltas
//     byte[] = deltas
// if hasPlayerDeltas:
//     uint32 = bitfield of included deltas (count of bits = count of deltas)
//     DemoPlayerDelta[]
// if hasNetworkMessages:
//     uint16 = count of DemoNetMessage[]
//     DemoNetMessage[]
// if hasCommands:
//    uint16 = count of DemoCommand[]
//    DemoCommand[]

#pragma pack(pop)

class SvenTV {
public:
	edict_t* edicts = NULL;
	netedict* netedicts = NULL;
	char* packetEntsBuffer = NULL;
	char* deltaPacketBuffer = NULL;
	int deltaPacketBufferSz = -1;

	volatile bool enableServer = false;
	volatile bool enableDemoFile = false;

	SvenTV(bool singleThreadMode);
	~SvenTV();

	// validates and prepares demo file for playback
	// plr = player who opened the file
	bool openDemo(edict_t* plr, string path, float offsetSeconds, bool skipPrecache=false);

	// call this every frame to replay the demo
	void playDemo();

	// call in MapInit after loading a demo file
	void precacheDemo();

	void stopReplay();

	// called from main thread to copy data to SvenTV thread
	void think_mainThread();

private:
	// main thread vars
	bool singleThreadMode;
	std::thread* tv_thread = NULL;

	// multi-thread vars
	volatile bool threadShouldExit; // only write from main thread
	ThreadSafeInt edictCopyState;

	// tvThread vars
	uint64_t lastTvThink; // for single-threaded mode
	NetClient clients[MAX_CLIENTS]; // lock mutex before using!
	float timeoutSeconds = 0.5; // how long to wait for a client to respond before disconnecting them
	uint16_t updateId = 0; // incremented on each edict refresh
	Socket* socket = NULL;
	bool abortEverything = false;
	netedict* debugEdict = NULL;
	
	// demo file writing
	uint64_t nextDemoUpdate = 0;
	float demoFileFps = 60;

	FILE* replayFile = NULL;
	vector<string> precacheModels;
	vector<string> precacheSounds;
	uint64_t replayStartTime = 0;
	uint32_t replayFrame = 0;
	uint32_t nextFrameOffset = 0;
	uint64_t nextFrameTime = 0;
	vector<EHandle> replayEnts;
	map<int, string> replayModelPath; // maps model index in demo file to a path

	FILE* demoFile = NULL;
	int numFileDeltas = 0;
	uint64_t deltaWriteSz = 0;
	uint64_t netMsgWriteSz = 0;
	uint64_t nextDemoKeyframe = 0;
	uint32_t lastServerFrameCount = 0;
	uint32_t serverFrameCount = 0;
	uint64_t demoStartTime = 0;
	uint64_t lastDemoFrameTime = 0;

	DemoPlayer* playerinfos = NULL;
	DemoPlayer* fileplayerinfos = NULL;
	char* filePlayerInfoBuffer = NULL;
	int filePlayerInfoBufferSize = -1;

	netedict* fileedicts = NULL; // last edicts written to file
	char* fileDeltaBuffer = NULL;
	int fileDeltaBufferSize = -1;
	
	NetMessageData* netmessages = NULL;
	char* netmessagesBuffer = NULL;
	int netmessagesBufferSize = -1;
	uint16_t netmessage_count = 0;

	CommandData* cmds = NULL;
	char* cmdsBuffer = NULL;
	int cmdsBufferSize = -1;
	uint16_t cmds_count = 0;

	uint32_t copyTime = 0;
	uint32_t thinkTime = 0;

	void think_tvThread();

	void handleClientPackets();
	void handleDeltaAck(mstream& reader, NetClient& client);

	void broadcastEntityStates();
	int writeEdictDelta(mstream& writer, const netedict& old, const netedict& now);

	// playerIdx = entity index - 1
	int writePlayerDelta(mstream& writer, uint8_t playerIdx, const DemoPlayer& old, const DemoPlayer& now);

	void initDemoFile();
	
	// return true if more data requested
	bool writeDemoFile();

	void closeDemoFile();

	void closeReplayFile();

	// returns true if more frames are needed to catch up with current playback time
	bool readDemoFrame();
	bool readEntDeltas(mstream& reader);
	// clears existing map entities for playback
	void prepareDemo(float offsetSeconds);
};


extern SvenTV* g_sventv;

// data written to by main thread
// and copied to svenTV thread when needed
extern DemoPlayer* g_demoplayers;
extern NetMessageData* g_netmessages;
extern int g_netmessage_count;
extern CommandData* g_cmds;
extern int g_command_count;
extern uint32_t g_server_frame_count;