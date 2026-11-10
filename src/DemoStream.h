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
// The server closes the connection when the session ends (map change, new recording).

#define HLTV_STREAM_MAGIC "HLTVLIVE"
#define HLTV_STREAM_VERSION 2

enum stream_packet_types {
	STREAM_PKT_HEADER = 1,
	STREAM_PKT_FRAME = 2,
	STREAM_PKT_POOL = 3,
	STREAM_PKT_AUTH_FAILED = 4,
};

struct StreamClientInfo {
	std::string address;
	const char* state;
	bool authed; // gave the password or a valid token, so watching or waiting for the next session
	uint32_t seconds; // time connected
	uint64_t bytesSent;
	uint64_t behind; // packets waiting to be sent
	uint64_t steamid64; // logged in with a website session token. 0 if they used the password.
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
		uint64_t connectTime = 0;
		uint64_t bytesSent = 0;
		std::vector<uint8_t> authBuf; // password received so far
		uint64_t authDeadline = 0; // drop if not authed by now. For a rejected client, when to reply.
		bool authed = false;
		bool checkingToken = false; // waiting for the main thread to validate a session token
		uint64_t steamid64 = 0; // logged in with a session token
		bool rejected = false; // wrong password, waiting out the delay
		bool closing = false; // disconnect once the prefix is sent
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
	void acceptClient(Client& client);
	void rejectClient(Client& client, uint64_t now);
	void applyTokenResults();
	void updateClientInfo();
};
