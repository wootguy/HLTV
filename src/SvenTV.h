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

enum edict_copy_states {
	EDICT_COPY_REQUESTED,
	EDICT_COPY_FINISHED,
};

enum delta_results {
	EDELTA_NONE, // there were no differences between the edicts
	EDELTA_WRITE, // there were differences between the edicts
	EDELTA_OVERFLOW, // there were differences but there is no room to write them
};

class SvenTV {
public:
	edict_t* edicts;
	netedict* netedicts;
	char* packetEntsBuffer;
	char* deltaPacketBuffer;
	int deltaPacketBufferSz;

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
	Socket* socket;
	bool abortEverything = false;
	netedict* debugEdict = NULL;

	void think_tvThread();

	void handleClientPackets();
	void handleDeltaAck(mstream& reader, NetClient& client);

	void broadcastEntityStates();
	int writeEdictDelta(mstream& writer, const netedict& old, const netedict& now);
};