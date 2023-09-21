#pragma once
#include <stdint.h>
#include "DemoPlayerEnt.h"
#include "netedict.h"

// Convert floats to/from signed fixed-point integers.
// Use this when the number of bits needed in the fixed-point representation is less than a standard type.
// For example, if you want 24 bits instead of 32 (part of int32), or 12 instead of 16 (part of int16)
// Simpler conversion logic can be used otherwise.
// TODO: clamp values bigger than can fit in the fixed int
#define FLOAT_TO_FIXED(v, whole_bits, frac_bits) \
	((uint32_t)(fabs(v) * (1 << frac_bits)) | (v < 0 ? (1 << (whole_bits-1)) : 0))
#define FIXED_TO_FLOAT(v, whole_bits, frac_bits) \
	((v & (1 << (whole_bits-1)) ? -1.0f : 1.0f) * ( (v & (~(1 << (whole_bits-1)))) / (float)(1 << frac_bits)))

#define INT24_MIN -8388608
#define INT24_MAX 8388607

#define MAX_CMD_FRAME 1024 // max client commands per frame
#define MAX_NETMSG_FRAME 1024 // max network messages per frame
#define MAX_NETMSG_DATA 512 // max bytes before overflow message from game
#define MAX_CMD_LENGTH 128
#define KEYFRAME_INTERVAL 60ULL // seconds between keyframes in demo files

#define DEMO_VERSION 1 // version number written to demo files for compatibility check (0-65535)

enum delta_results {
	EDELTA_NONE, // there were no differences between the edicts
	EDELTA_WRITE, // there were differences between the edicts
	EDELTA_OVERFLOW, // there were differences but there is no room to write them
};

#pragma pack(push, 1)

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

struct CommandData {
	uint8_t idx;
	uint8_t len;
	uint8_t data[128];
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

struct NetMessageData {
	DemoNetMessage header;
	float origin[3];
	uint16_t eidx;
	uint8_t data[512];

	int getSize() { 
		return sizeof(DemoNetMessage) + (header.hasOrigin*3*sizeof(float)) 
			+ (header.hasEdict*sizeof(uint16_t)) + header.sz;
	}
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

// data needed to simulate a server frame
struct FrameData {
	netedict* netedicts;
	DemoPlayerEnt* playerinfos;
	NetMessageData* netmessages;
	CommandData* cmds;

	uint16_t netmessage_count = 0;
	uint16_t cmds_count = 0;
	uint32_t serverFrameCount = 0;
};