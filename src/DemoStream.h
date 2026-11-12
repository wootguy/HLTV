#pragma once
#include <stdint.h>
#include <stddef.h>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// Live demo streaming over TCP. Frames are encoded once by DemoWriter and handed to this class,
// which keeps the most recent ones in a ring that every client reads from. Connected clients
// only cost a socket send, no matter how many there are.
//
// Wire format:
//   "HLTVLIVE" + uint32 version (sent once, on connect)
//   the client then sends uint32 size + password, even if the password is empty. Instead of the
//   password, the client can send the session token from logging in to the website (see
//   StreamTokenCheck), which the hlcoop_webserver plugin validates.
//   then packets of [uint8 type][uint32 payload size][payload]
//     STREAM_PKT_HEADER: uint64 backlog bytes that follow + demo header, string pool, model and sound lists
//     STREAM_PKT_FRAME:  one demo frame, same bytes as in a demo file
//     STREAM_PKT_POOL:   uint16 offset + bytes appended to the string pool
//     STREAM_PKT_AUTH_FAILED: no payload. The password was wrong and the server will disconnect.
//     STREAM_PKT_VIEWERS: who is watching (see StreamViewers.h). uint16 count, then for each viewer
//       who logged in with a website token: uint16 camera entity (0 = none), uint8 top color,
//       uint8 bottom color, uint8 VIEWER_FL_* flags, uint32 seconds behind live (rounded, 0 = live),
//       uint8 name length, name. Viewers who used the stream password aren't listed.
//       After the last viewer, the same viewers again in the same order, with what they're watching:
//       uint8 VIEW_INFO_* flags, uint8 player being spectated (entity index, 0 = none), uint8 VIEW_CAM_*
//       camera mode, uint8 VIEW_DIRECTOR_* mode, float chasecam distance. Older clients stop reading
//       after the names, so this was added without changing the version.
//       Sent when it changes, and after STREAM_PKT_HEADER.
//     STREAM_PKT_REPLACED: no payload. A newer connection replaced this one: the same Steam ID logged
//       in again, or too many connections from this IP used the stream password. The server will
//       disconnect, and the client shouldn't reconnect.
// After logging in, clients can send packets of the same form:
//     CLIENT_PKT_VIEW: float origin[3] + float view angles[3] (pitch, yaw, roll) + uint32 millis
//       behind live (0 = live). Where the viewer is looking from. The server makes a camera entity
//       there for viewers with a token login. Newer clients add uint8 player being spectated
//       (0 = none), uint8 VIEW_CAM_* camera mode, uint8 VIEW_DIRECTOR_* mode and float chasecam
//       distance, which the server uses to keep the camera on the player and passes on to others.
//       Then uint8 VIEW_LOAD_* state, which the website shows next to the viewer.
// Older clients and servers skip packet types they don't know, so new types don't change the version.
// The server closes the connection when the session ends (map change, new recording).

#define HLTV_STREAM_MAGIC "HLTVLIVE"
#define HLTV_STREAM_VERSION 2

enum stream_packet_types {
	STREAM_PKT_HEADER = 1,
	STREAM_PKT_FRAME = 2,
	STREAM_PKT_POOL = 3,
	STREAM_PKT_AUTH_FAILED = 4,
	STREAM_PKT_VIEWERS = 5,
	STREAM_PKT_REPLACED = 6,
};

enum stream_client_packet_types {
	CLIENT_PKT_VIEW = 1,
};

#define VIEWER_FL_YOU 1 // the viewer this packet was sent to

#define VIEW_INFO_KNOWN 1 // the client sent what it's watching. Older clients don't.

// camera modes of the demo player (camera_modes in bspguy's DemoViewer.h)
enum view_camera_modes {
	VIEW_CAM_FREE, // flying around
	VIEW_CAM_FIRST_PERSON, // through the player's eyes
	VIEW_CAM_CHASE, // orbiting the player
	VIEW_CAM_CHASE_LOCKED, // orbiting the player, turning with them
	VIEW_CAM_COUNT
};

enum view_director_modes {
	VIEW_DIRECTOR_OFF,
	VIEW_DIRECTOR_ACTION, // picks the player seeing the most action
	VIEW_DIRECTOR_CROWD, // picks the player who can see the most players
	VIEW_DIRECTOR_COUNT
};

#define MAX_CHASE_DIST 4096.0f

// what the viewer has loaded (CLIENT_PKT_VIEW, VIEW_LOAD_* in bspguy's DemoFile.h). Clients that
// don't send it count as ready once they send a view, and as loading until then.
enum view_load_states {
	VIEW_LOAD_READY, // everything it needs right now is loaded
	VIEW_LOAD_MAP, // missing the map or player model, so it can't spectate properly yet
	VIEW_LOAD_BACKGROUND, // can spectate, but still loading other models, sounds or textures
	VIEW_LOAD_COUNT
};

// what a viewer is watching, from CLIENT_PKT_VIEW
struct StreamViewInfo {
	bool known = false; // the client sent it
	uint8_t target = 0; // player entity index, 0 = none
	uint8_t camMode = VIEW_CAM_FREE;
	uint8_t director = VIEW_DIRECTOR_OFF;
	float chaseDist = 0;

	bool operator==(const StreamViewInfo& o) const {
		return known == o.known && target == o.target && camMode == o.camMode
			&& director == o.director && chaseDist == o.chaseDist;
	}
};

struct StreamClientInfo {
	uint64_t id; // unique for each connection
	std::string address;
	const char* state;
	bool authed; // gave the password or a valid token, so watching or waiting for the next session
	uint32_t seconds; // time connected
	uint64_t bytesSent;
	uint64_t behind; // packets waiting to be sent
	uint64_t steamid64; // logged in with a website session token. 0 if they used the password.
	bool joined; // receiving the current session
	bool hasCamera; // the client sent where it's looking from
	float camOrigin[3];
	float camAngles[3]; // view angles
	uint32_t delayMs; // how far behind live the client is watching
	StreamViewInfo view;
	uint8_t loadState; // VIEW_LOAD_*. VIEW_LOAD_MAP until the client has joined and sent a view.
};

// a viewer who logged in with a website token
struct StreamViewer {
	uint64_t clientId;
	uint16_t entindex; // camera entity, or 0
	uint8_t topColor;
	uint8_t bottomColor;
	uint32_t delaySec; // rounded, so this doesn't change constantly
	std::string name;
	StreamViewInfo view;

	bool operator==(const StreamViewer& o) const {
		return clientId == o.clientId && entindex == o.entindex && topColor == o.topColor
			&& bottomColor == o.bottomColor && delaySec == o.delaySec && name == o.name && view == o.view;
	}
};

// A password that didn't match the stream password, which might be a website session token. Only
// the main thread may call other plugins, so it takes these with takeTokenChecks(), looks the
// tokens up and returns them through finishTokenChecks().
struct StreamTokenCheck {
	uint64_t clientId;
	std::string token;
	uint64_t steamid64; // set by the main thread. 0 if the token isn't valid.
};

class DemoStream {
public:
	int maxFrames = 7200; // minimum number of recent frames to keep for clients that join late
	int maxClients = 128; // new connections are refused at this many. Lowering it doesn't drop anyone.

	DemoStream();
	~DemoStream();

	bool listen(uint16_t port);
	void stopListening();
	bool isListening();

	// headerBlock is everything in a demo file before the first frame
	void beginSession(const std::vector<uint8_t>& headerBlock, size_t stringPoolOffset);
	void endSession(); // disconnects clients, so they can pick up the next session
	bool isSessionActive() { return sessionActive; }

	void pushFrame(const uint8_t* data, size_t sz, bool keyframe);
	void pushStringPool(uint16_t offset, const char* data, size_t sz);

	// accepts clients and sends pending data. Never blocks.
	void think();

	// these are safe to call from any thread
	void setPassword(const char* newPassword); // empty allows anyone to connect
	void setTokenLogins(bool enable); // accept website session tokens in place of the password
	std::vector<StreamClientInfo> getClients();
	std::vector<StreamTokenCheck> takeTokenChecks();
	void finishTokenChecks(const std::vector<StreamTokenCheck>& results);
	void setViewers(const std::vector<StreamViewer>& viewers); // sent to clients when they change

private:
	typedef std::shared_ptr<std::vector<uint8_t>> Segment;

	struct Client {
		uint64_t id = 0;
		intptr_t sock = -1;
		std::vector<uint8_t> prefix; // hello and header, not shared with other clients
		size_t prefixSent = 0;
		bool joined = false;
		uint64_t nextSeq = 0; // next segment to send
		size_t segmentSent = 0; // bytes of that segment already sent

		std::string address;
		std::string ip; // address without the port
		uint64_t connectTime = 0;
		uint64_t bytesSent = 0;
		std::vector<uint8_t> authBuf; // password received so far
		uint64_t authDeadline = 0; // drop if not authed by now. For a rejected client, when to reply.
		bool authed = false;
		bool checkingToken = false; // waiting for the main thread to validate a session token
		uint64_t steamid64 = 0; // logged in with a session token
		bool rejected = false; // wrong password, waiting out the delay
		bool closing = false; // disconnect once the prefix is sent
		uint64_t closeDeadline = 0; // when closing, disconnect by now even if the prefix wasn't sent

		std::vector<uint8_t> inBuf; // client packets received after logging in
		bool hasCamera = false;
		float camOrigin[3] = { 0, 0, 0 };
		float camAngles[3] = { 0, 0, 0 };
		uint32_t delayMs = 0;
		StreamViewInfo view;
		uint8_t loadState = VIEW_LOAD_READY; // sent by newer clients
		uint64_t viewersSent = 0; // version of the viewer list this client was sent
	};

	struct KeyframeMark {
		uint64_t seq;
		uint64_t frameNum; // frames pushed before this one
	};

	intptr_t listenSock = -1;
	uint64_t nextListenAttempt = 0;
	std::vector<Client> clients;
	uint64_t nextClientId = 1;

	std::mutex infoMutex; // guards password, token logins and clientInfo
	std::string password;
	bool tokenLogins = false;
	std::vector<StreamTokenCheck> tokenChecks; // waiting for the main thread
	std::vector<StreamTokenCheck> tokenResults; // checked by the main thread
	std::vector<StreamClientInfo> clientInfo; // copy of the client states for other threads
	std::vector<StreamViewer> viewers;
	uint64_t viewersVersion = 1;

	bool sessionActive = false;
	std::vector<uint8_t> header; // kept up to date with the string pool for late joiners
	size_t poolOffset = 0;

	std::deque<Segment> ring;
	std::deque<KeyframeMark> keyframes;
	uint64_t firstSeq = 0; // sequence number of ring[0]
	uint64_t framesPushed = 0;

	Segment makeSegment(uint8_t type, const void* prefix, size_t prefixSz, const void* data, size_t sz);
	void trimRing();
	void joinSession(Client& client);
	void closeClient(Client& client);
	bool flushClient(Client& client); // false if the client should be dropped
	bool readAuth(Client& client); // false if the client should be dropped
	bool readClientPackets(Client& client); // false if the client should be dropped
	void sendViewers(Client& client);
	void acceptClient(Client& client);
	void rejectClient(Client& client, uint64_t now);
	void replaceClient(Client& client, uint64_t now, const char* reason);
	void limitPasswordConnections(const Client& client, uint64_t now);
	void applyTokenResults();
	void updateClientInfo();
};
