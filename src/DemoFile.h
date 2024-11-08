#pragma once
#include "main.h"
#include <stdint.h>
#include "DemoPlayerEnt.h"
#include "netedict.h"

// Convert floats to/from signed fixed-point integers.
// Use this when the number of bits needed in the fixed-point representation is less than a standard type.
// For example, if you want 24 bits instead of 32 (part of int32), or 12 instead of 16 (part of int16).
// Simpler conversion logic can be used otherwise.
// result will be sign-extended to fit an int32_t.
// values that don't fit in the specified number of bits will be clamped.
inline int32_t FLOAT_TO_FIXED(float x, int whole_bits, int frac_bits) {
	int32_t maxVal = ((1 << (whole_bits + frac_bits - 1)) - 1);
	int32_t minVal = -(maxVal+1);
	int32_t r = clampf(x, minVal, maxVal) * (1 << frac_bits);
	return r;
}
// x should not be sign extended to fit the int32_t it was stored in. That will be done in this function
inline float FIXED_TO_FLOAT(int x, int whole_bits, int frac_bits) {
	// https://graphics.stanford.edu/~seander/bithacks.html#VariableSignExtend
	uint32_t b = whole_bits + frac_bits;
	int m = 1U << (b - 1); // sign bit mask

	x = x & ((1U << b) - 1); // remove sign bit
	int r = (x ^ m) - m; // sign extended version of x

	return r / (float)(1 << frac_bits);
}

// compares two fixed point integers for equality, where each may or may not be sign extended
inline bool FIXED_EQUALS(int x, int y, int totalBits) {
	int m = (1 << totalBits) - 1; // mask of value without extended sign
	return (x & m) == (y & m);
}

#define MAX_EVENT_FRAME 256 // max events per frame
#define MAX_CMD_FRAME 256 // max client commands per frame
#define MAX_NETMSG_FRAME 4096 // max network messages per frame
#define MAX_NETMSG_DATA 512 // max bytes before overflow message from game
#define MAX_CMD_LENGTH 128
#define KEYFRAME_INTERVAL 60ULL // seconds between keyframes in demo files

#define DEMO_VERSION 1 // version number written to demo files for compatibility check (0-65535)

#define FL_ENTIDX_LONG 1 // ent index is further than 127 from the last idx

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
	uint16_t stringPoolSize; // size of the string pool
	uint16_t modelCount;
	uint16_t soundCount;
	uint32_t modelLen; // size of model string data
	uint32_t soundLen; // size of sound string data
	// stringPoolSize of characters
	// modelCount of 2-byte model indexes which map to a string below
	// modelLen bytes of model strings delimtted by \n
	// soundCount of 2-byte sound indexes which map to a string below
	// soundLen bytes of sound strings delimtted by \n
};

struct DemoCommand {
	uint8_t idx; //  entity index. 0 = server.
	uint8_t len; // length of command
	// byte[] = command bytes
};


struct DemoFrame {
	uint8_t isKeyFrame : 1; // if true, zero out entity and player info for a full update
	uint8_t isGiantFrame : 1; // time delta or frame size does not fit in a BigFrame
	uint8_t isBigFrame : 1; // frame size does not fit in a byte
	uint8_t hasEntityDeltas : 1;
	uint8_t hasNetworkMessages : 1;
	uint8_t hasEvents : 1;
	uint8_t hasPlayerDeltas : 1;
	uint8_t hasCommands : 1;
	uint8_t deltaFrames; // server frames since last demoFrame (for server fps)
	// if isGiantFrame:
	//     uint32_t demoTime = milliseconds since recording started
	//     uint32_t frameSize = total size of this frame, including this header
	// else if isBigFrame:
	//     uint8_t demoTimeDelta = milliseconds since the last frame
	//     uint16_t frameSize = total size of this frame, including this header
	// else:
	//     uint8_t demoTimeDelta = milliseconds since the last frame
	//     uint8_t frameSize = total size of this frame, including this header
};

struct CommandData {
	uint8_t idx;
	uint8_t len;
	uint8_t data[128];
};

struct DemoNetMessage {
	uint8_t sz; // network message data bytes
	uint8_t type;
	uint8_t dest : 4;
	uint8_t hasEdict : 1;
	uint8_t hasOrigin : 1;
	uint8_t hasLongOrigin : 1;
	uint8_t szHighBit : 1; // high bit of the "sz" field
	// if hasLongOrigin:
	//     uint24[3] = origin (stored as 19.5 fixed point)
	// if hasOrigin:
	//     int16[3] = origin (stored as integers)
	// if hasEdict:
	//     uint16_t = edict index
	// byte[] = message bytes
};

struct NetMessageData {
	DemoNetMessage header;
	uint16_t sz; // header.sz + szHighBit, for ease of use
	uint32_t origin[3]; // 19.5 fixed point, or 16bit integers, depending on hasLongOrigin
	uint16_t eidx;
	uint8_t data[512];

	int getSize() { 
		return sizeof(DemoNetMessage) + (header.hasOrigin*3*sizeof(float)) 
			+ (header.hasEdict*sizeof(uint16_t)) + header.sz;
	}

	void send(int msg_dst, edict_t* targetEnt);

	// compresses 4-byte coords into 2-bytes by removing the fractional parts
	// coords will become integers clamped to +/-32768 (max map size before stuff breaks anyway)
	// if keepFrac is set, range will be +/-4096 with a 3-bit fractional part
	void compressCoords(int offset, int count, bool keepFrac=false);
	void decompressCoords(int offset, int count, bool keepFrac=false);
};

struct DemoEvent {
	uint8_t eventindex;
	uint16_t entindex : 12;
	uint16_t flags : 4; // FEV_HOSTONLY/SERVER/CLIENT flags never set for server events(?)

	uint8_t hasOrigin : 1;
	uint8_t hasAngles : 1;
	uint8_t hasFparam1 : 1;
	uint8_t hasFparam2 : 1;
	uint8_t hasIparam1 : 1;
	uint8_t hasIparam2 : 1;
	uint8_t bparam1 : 1;
	uint8_t bparam2 : 1;
	// if hasOrigin:
	//    int32[3] = origin
	// ...
};

struct DemoEventData {
	DemoEvent header;
	int32_t origin[3];
	int16_t angles[3];
	int32_t fparam1;
	int32_t fparam2;
	int16_t iparam1;
	int16_t iparam2;
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
// if hasEvents:
//     uint8 = count of DemoEvent[]
//     DemoEvent[]
// if hasCommands:
//     uint8 = count of DemoCommand[]
//     DemoCommand[]

#pragma pack(pop)

// data needed to simulate a server frame
struct FrameData {
	netedict* netedicts;
	DemoPlayerEnt* playerinfos;
	NetMessageData* netmessages;
	CommandData* cmds;
	DemoEventData* events;

	uint32_t netmessage_count = 0;
	uint32_t cmds_count = 0;
	uint32_t event_count = 0;
	uint32_t serverFrameCount = 0;
};
