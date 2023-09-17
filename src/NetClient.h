#pragma once
#include "Packet.h"
#include "IPV4.h"
#include "main.h"
#include "mstream.h"

// Convert floats to/from signed fixed-point integers.
// Use this when the number of bits needed in the fixed-point representation is less than a standard type.
// For example, if you want 24 bits instead of 32 (part of int32), or 12 instead of 16 (part of int16)
// Simpler conversion logic can be used otherwise.
// TODO: clamp values bigger than can fit in the fixed int
#define FLOAT_TO_FIXED(v, whole_bits, frac_bits) \
	((uint32_t)(fabs(v) * (1 << frac_bits)) | (v < 0 ? (1 << (whole_bits-1)) : 0))
#define FIXED_TO_FLOAT(v, whole_bits, frac_bits) \
	((v & (1 << (whole_bits-1)) ? -1.0f : 1.0f) * ( (v & (~(1 << (whole_bits-1)))) / (float)(1 << frac_bits)))

// flags for indicating which edict fields were updated
#define FL_DELTA_EDTYPE			(1 << 0)
#define FL_DELTA_ORIGIN_X		(1 << 1)
#define FL_DELTA_ORIGIN_Y		(1 << 2)
#define FL_DELTA_ORIGIN_Z		(1 << 3)
#define FL_DELTA_ANGLES_X		(1 << 4)
#define FL_DELTA_ANGLES_Y		(1 << 5)
#define FL_DELTA_ANGLES_Z		(1 << 6)
#define FL_DELTA_MODELINDEX		(1 << 7)
#define FL_DELTA_SKIN			(1 << 8)
#define FL_DELTA_BODY			(1 << 9)
#define FL_DELTA_EFFECTS		(1 << 10)
#define FL_DELTA_SEQUENCE		(1 << 11)
#define FL_DELTA_GAITSEQUENCE	(1 << 12)
#define FL_DELTA_FRAME			(1 << 13)
#define FL_DELTA_ANIMTIME		(1 << 14)
#define FL_DELTA_FRAMERATE		(1 << 15)
#define FL_DELTA_CONTROLLER_0	(1 << 16)
#define FL_DELTA_CONTROLLER_1	(1 << 17)
#define FL_DELTA_CONTROLLER_2	(1 << 18)
#define FL_DELTA_CONTROLLER_3	(1 << 19)
#define FL_DELTA_BLENDING		(1 << 20)
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
#define FL_DELTA_CLASSIFYGOD	(1 << 31)

enum netedict_types {
	NETED_INVALID, // don't render or update this entity
	NETED_MODEL,   // generic entity that uses a model (BSP/mdl/spr)
	NETED_MONSTER, // should display health/name
	NETED_PLAYER,  // special model loading and rendering
	NETED_BEAM,    // lasers and stuff
};

enum delta_results {
	EDELTA_NONE, // there were no differences between the edicts
	EDELTA_WRITE, // there were differences between the edicts
	EDELTA_OVERFLOW, // there were differences but there is no room to write them
};

// edict with only the data needed for rendering, and only the bits needed
struct netedict {
	uint8_t		edtype;			// netedict_types
	uint32_t	origin[3];		// 19.5 fixed point
	uint32_t	health;
	uint16_t	angles[3];		// Model/view angles (0-360 scaled to 0-65535)
	uint16_t	modelindex;

	uint8_t		skin;
	uint8_t		body;			// sub-model selection for studiomodels
	uint16_t 	effects;
	uint8_t		colormap;

	uint8_t		sequence;		// animation sequence
	uint8_t		gaitsequence;	// movement animation sequence for player (0 for none)
	uint8_t		frame;			// % playback position in animation sequences (0..255)
	uint8_t		animtime;		// world time when frame was set
	int8_t		framerate;		// animation playback rate (-8x to 8x) (4.4 fixed point)
	uint8_t		controller[4];	// bone controller setting (0..255)
	uint8_t		blending[2];	// blending amount between sub-sequences (0..255)

	uint16_t	scale;			// rendering scale (0..255) (8.8 fixed point)

	uint8_t		rendermode;
	uint8_t		renderamt;
	uint8_t		rendercolor[3];
	uint8_t		renderfx;

	uint16_t	aiment;		// entity pointer when MOVETYPE_FOLLOW, 0 if movetype is not MOVETYPE_FOLLOW
	uint8_t		classifyGod; // class_ovverride (7 bits), GODMODE/DAMAGE_NO (LSB)

	netedict();
	void load(const edict_t& ed);
	void apply(edict_t* ed, vector<EHandle>& simEnts);
	bool matches(netedict& other);

	// returns false if entity was deleted (no deltas)
	bool readDeltas(mstream& reader);

	// write deltas between this edict and the "old" edict
	// resets writer position on EDELTA_OVERFLOW 
	int writeDeltas(mstream& writer, netedict& old);
};

struct DeltaUpdate {
	uint16_t updateId; // client will use this to ack

	// 8192 edict deltas can't always fit in a single packet
	vector<Packet> packets;
};

struct NetUsageDatapoint {
	int bytes;
	uint64_t time;
};

class NetClient {
public:
	IPV4 addr;
	bool isFree = true; // true if slot this client occupies is empty

	// baselines are entity states which delta packets are created from
	// if an entity doesn't change, then no data needs to be sent for that
	// entity. Baselines are updated whenever a client acknowledges that
	// it received a delta packet.
	netedict* baselines = NULL;

	// delta packets that have been sent but not acknowledged by the client
	vector<DeltaUpdate> sentDeltas;

	deque<NetUsageDatapoint> sentBytesHistory;

	// how often to send entity updates
	float updateRateFps = 60;

	uint64_t nextUpdateTime = 0;

	// max bytes that can be sent per second.
	// If an entity update requires more bytes, the update will be throttled and sent in order
	// until the update is complete. This slows the update rate down substantially.
	uint64_t maxBytesPerSecond = 50 * 1000;

	// used to ID which edict state a delta is for.
	// 0 = no previous state exists, so use a null baseline for every edict
	// When a client sends an ack for a delta update, it sends the update ID and includes
	// a list of packets that were received/lost in the update. The server then uses this
	// ID as the baseline for future delta packets. This allows both the servera and client to apply
	// the changes from the packets which were acked, and discard older edict states/deltas.
	// Not all packets in an update are required to establish a new baseline. Each individual packet
	// helps reduce delta size in future updates. Even with 90% packet loss, deltas should eventually
	// reach a reasonable size (1-4 packets).
	// (delta update = deltas for every entity, which could be up to ~900 packets for 8192 edicts)
	uint16_t baselineId = 0;

	uint64_t lastPacketTime = 0; // used to disconnected unresponsive clients

	NetClient();

	void init(IPV4 addr);
	int getBytesSentPerSecond();
	
	// returns edict number on error or -1 for success
	int applyDeltaToBaseline(Packet& packet, bool debugMode);

private:

};