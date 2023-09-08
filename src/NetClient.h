#pragma once
#include "Packet.h"
#include "IPV4.h"
#include "main.h"

// convert floats to/from fixed-point integers with the given amount of fractional bits
// TODO: does not protect against overflow/underflow
#define FLOAT_TO_FIXED(v, fractional_bits) (v * (1 << fractional_bits))
#define FIXED_TO_FLOAT(v, fractional_bits) ((float)v / (float)(1 << fractional_bits))

// edict with only the data needed for rendering, and only the bits needed
struct netedict {
	bool		isValid;		// true if edict is rendered and sent to clients
	float		origin[3];
	uint32_t	health;
	uint16_t	angles[3];		// Model angles (0-360 scaled to 0-65535)
	uint16_t	modelindex;

	uint8_t		skin;
	uint8_t		body;			// sub-model selection for studiomodels
	uint8_t 	effects;
	uint8_t		colormap;

	uint8_t		sequence;		// animation sequence
	uint8_t		gaitsequence;	// movement animation sequence for player (0 for none)
	uint8_t		frame;			// % playback position in animation sequences (0..255)
	uint8_t		animtime;		// world time when frame was set
	uint8_t		framerate;		// animation playback rate (-8x to 8x)
	uint8_t		controller[4];	// bone controller setting (0..255)
	uint8_t		blending[2];	// blending amount between sub-sequences (0..255)

	uint8_t		scale;			// sprite rendering scale (0..255)

	uint8_t		rendermode;
	uint8_t		renderamt;
	uint8_t		rendercolor[3];
	uint8_t		renderfx;

	uint16_t	aiment;		// entity pointer when MOVETYPE_FOLLOW, 0 if movetype is not MOVETYPE_FOLLOW

	netedict();
	void load(const edict_t& ed);
	void apply(edict_t* ed, vector<EHandle>& simEnts);
	bool matches(netedict& other);
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