#pragma once
#include "ThreadSafeInt.h"
#include <thread>
#include <mutex>
#include "Socket.h"
#include "NetClient.h"
#include "mstream.h"
#include "DemoPlayer.h"
#include "DemoFile.h"
#include "DemoWriter.h"
#include "mstream.h"

#define SVENTV_PORT 28015
#define MAX_CLIENTS 64

#define SVC_PACKETENTITIES 40
#define SVC_DELTAPACKETENTITIES 41
#define SVC_WELCOME 255 // new client connect ack

// client packet types
#define CLC_CONNECT 1 
#define CLC_DELTA_ACK 2
#define CLC_DELTA_RESET 3

enum edict_copy_states {
	EDICT_COPY_REQUESTED,
	EDICT_COPY_FINISHED,
};

class SvenTV {
public:
	edict_t* edicts = NULL;
	char* packetEntsBuffer = NULL;
	char* deltaPacketBuffer = NULL;
	int deltaPacketBufferSz = -1;

	volatile bool enableServer = false;
	volatile bool enableDemoFile = false;

	SvenTV(bool singleThreadMode);
	~SvenTV();

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
	DemoWriter* demoWriter;
	FrameData frame; // latest frame data from the server

	void think_tvThread();

	void handleClientPackets();

	void handleDeltaAck(mstream& reader, NetClient& client);

	void broadcastEntityStates();

	bool validateEdicts(); // debug
};


// data written to by main thread
// and copied to svenTV thread when needed
extern DemoPlayerEnt* g_demoplayers;
extern NetMessageData* g_netmessages;
extern int g_netmessage_count;
extern CommandData* g_cmds;
extern int g_command_count;
extern DemoEventData* g_events;
extern int g_event_count;
extern uint32_t g_server_frame_count;