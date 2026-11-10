#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#define SOCK_WOULD_BLOCK (WSAGetLastError() == WSAEWOULDBLOCK)
#define closesock closesocket
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#define SOCK_WOULD_BLOCK (errno == EAGAIN || errno == EWOULDBLOCK)
#define closesock close
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#endif

#include "main.h"
#include "DemoStream.h"
#include <string.h>
#include <algorithm>
#include <string>

#define MAX_STREAM_CLIENTS 16
#define MAX_SEND_PER_THINK (1024 * 256) // per client, to keep a backlog from stalling the server frame
#define MAX_PASSWORD_LEN 256 // website session tokens are 172 characters
#define AUTH_TIMEOUT_MS 5000
#define AUTH_FAIL_DELAY_MS 2000 // with the client limit, this caps how fast passwords can be guessed

static void setNonBlocking(intptr_t sock) {
#ifdef WIN32
	u_long mode = 1;
	ioctlsocket((SOCKET)sock, FIONBIO, &mode);
#else
	fcntl((int)sock, F_SETFL, fcntl((int)sock, F_GETFL, 0) | O_NONBLOCK);
#endif
}

DemoStream::DemoStream() {
#ifdef WIN32
	WSADATA wsa;
	WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
}

DemoStream::~DemoStream() {
	stopListening();
#ifdef WIN32
	WSACleanup();
#endif
}

bool DemoStream::isListening() {
	return listenSock != -1;
}

bool DemoStream::listen(uint16_t port) {
	if (listenSock != -1) {
		return true;
	}

	uint64_t now = getEpochMillis();
	if (now < nextListenAttempt) {
		return false;
	}
	nextListenAttempt = now + 5000;

	intptr_t sock = (intptr_t)socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock == (intptr_t)INVALID_SOCKET) {
		ALERT(at_error, "HLTV stream: failed to create socket\n");
		return false;
	}

#ifndef WIN32
	int reuse = 1; // don't fail to bind while old connections are in TIME_WAIT
	setsockopt((int)sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif

	sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(port);

	if (::bind(sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR || ::listen(sock, 8) == SOCKET_ERROR) {
		ALERT(at_error, "HLTV stream: failed to listen on TCP port %d\n", (int)port);
		closesock(sock);
		return false;
	}

	setNonBlocking(sock);
	listenSock = sock;
	g_engfuncs.pfnServerPrint(UTIL_VarArgs("HLTV stream: listening on TCP port %d\n", (int)port));
	return true;
}

void DemoStream::stopListening() {
	endSession();
	for (Client& client : clients) {
		closeClient(client);
	}
	clients.clear();
	if (listenSock != -1) {
		closesock(listenSock);
		listenSock = -1;
	}
}

void DemoStream::beginSession(const std::vector<uint8_t>& headerBlock, size_t stringPoolOffset) {
	endSession();

	header = headerBlock;
	poolOffset = stringPoolOffset;
	sessionActive = true;

	for (Client& client : clients) {
		if (client.authed) {
			joinSession(client);
		}
	}
}

void DemoStream::endSession() {
	if (!sessionActive) {
		return;
	}
	sessionActive = false;

	// clients that are waiting stay connected for the next session. The others will reconnect.
	clients.erase(std::remove_if(clients.begin(), clients.end(), [this](Client& client) {
		if (client.joined) {
			closeClient(client);
			return true;
		}
		return false;
	}), clients.end());

	ring.clear();
	keyframes.clear();
	firstSeq = 0;
	framesPushed = 0;
}

DemoStream::Segment DemoStream::makeSegment(uint8_t type, const void* prefix, size_t prefixSz, const void* data, size_t sz) {
	Segment seg = std::make_shared<std::vector<uint8_t>>(5 + prefixSz + sz);
	uint32_t payloadSz = prefixSz + sz;
	uint8_t* out = seg->data();
	out[0] = type;
	memcpy(out + 1, &payloadSz, 4);
	if (prefixSz)
		memcpy(out + 5, prefix, prefixSz);
	memcpy(out + 5 + prefixSz, data, sz);
	return seg;
}

void DemoStream::pushFrame(const uint8_t* data, size_t sz, bool keyframe) {
	if (!sessionActive) {
		return;
	}

	if (keyframe) {
		keyframes.push_back({ firstSeq + ring.size(), framesPushed });
	}
	ring.push_back(makeSegment(STREAM_PKT_FRAME, NULL, 0, data, sz));
	framesPushed++;

	trimRing();
}

void DemoStream::pushStringPool(uint16_t offset, const char* data, size_t sz) {
	if (!sessionActive || poolOffset + offset + sz > header.size()) {
		return;
	}

	memcpy(header.data() + poolOffset + offset, data, sz);
	ring.push_back(makeSegment(STREAM_PKT_POOL, &offset, 2, data, sz));
}

// Clients need a keyframe to start from, so the ring always begins with one. Frames are
// dropped a keyframe interval at a time, as long as at least maxFrames would remain.
void DemoStream::trimRing() {
	while (keyframes.size() >= 2 && framesPushed - keyframes[1].frameNum >= (uint64_t)maxFrames) {
		while (firstSeq < keyframes[1].seq) {
			ring.pop_front();
			firstSeq++;
		}
		keyframes.pop_front();
	}
}

void DemoStream::joinSession(Client& client) {
	client.joined = true;
	// nothing is trimmed until a keyframe exists, so without one the ring starts at the session start
	client.nextSeq = keyframes.empty() ? firstSeq : keyframes.front().seq;
	client.segmentSent = 0;

	uint64_t backlog = 0;
	for (uint64_t seq = client.nextSeq; seq < firstSeq + ring.size(); seq++) {
		backlog += ring[seq - firstSeq]->size();
	}

	Segment seg = makeSegment(STREAM_PKT_HEADER, &backlog, sizeof(backlog), header.data(), header.size());
	client.prefix.insert(client.prefix.end(), seg->begin(), seg->end());
}

void DemoStream::closeClient(Client& client) {
	closesock(client.sock);
	client.sock = -1;
}

void DemoStream::setPassword(const char* newPassword) {
	std::lock_guard<std::mutex> guard(infoMutex);
	password = newPassword;
}

void DemoStream::setTokenLogins(bool enable) {
	std::lock_guard<std::mutex> guard(infoMutex);
	tokenLogins = enable;
}

std::vector<StreamTokenCheck> DemoStream::takeTokenChecks() {
	std::lock_guard<std::mutex> guard(infoMutex);
	std::vector<StreamTokenCheck> checks;
	checks.swap(tokenChecks);
	return checks;
}

void DemoStream::finishTokenChecks(const std::vector<StreamTokenCheck>& results) {
	std::lock_guard<std::mutex> guard(infoMutex);
	tokenResults.insert(tokenResults.end(), results.begin(), results.end());
}

void DemoStream::applyTokenResults() {
	std::vector<StreamTokenCheck> results;
	{
		std::lock_guard<std::mutex> guard(infoMutex);
		results.swap(tokenResults);
	}

	uint64_t now = getEpochMillis();
	for (const StreamTokenCheck& result : results) {
		for (Client& client : clients) {
			if (client.id != result.clientId || !client.checkingToken) {
				continue;
			}

			client.checkingToken = false;
			if (result.steamid64) {
				client.steamid64 = result.steamid64;
				ALERT(at_console, "HLTV stream: %s logged in with a website token (steam ID %llu)\n",
					client.address.c_str(), (unsigned long long)result.steamid64);
				acceptClient(client);
			}
			else {
				rejectClient(client, now);
			}
		}
	}
}

void DemoStream::acceptClient(Client& client) {
	client.authed = true;
	if (sessionActive) {
		joinSession(client);
	}
}

void DemoStream::rejectClient(Client& client, uint64_t now) {
	client.rejected = true;
	client.authDeadline = now + AUTH_FAIL_DELAY_MS;
	ALERT(at_console, "HLTV stream: wrong password from %s\n", client.address.c_str());
}

std::vector<StreamClientInfo> DemoStream::getClients() {
	std::lock_guard<std::mutex> guard(infoMutex);
	return clientInfo;
}

void DemoStream::updateClientInfo() {
	uint64_t now = getEpochMillis();

	std::lock_guard<std::mutex> guard(infoMutex);
	clientInfo.clear();
	for (const Client& client : clients) {
		StreamClientInfo info;
		info.address = client.address;
		info.state = client.joined ? "watching" : (client.authed ? "waiting" : "logging in");
		info.steamid64 = client.steamid64;
		info.authed = client.authed;
		info.seconds = (uint32_t)((now - client.connectTime) / 1000);
		info.bytesSent = client.bytesSent;
		info.behind = client.joined ? (firstSeq + ring.size()) - client.nextSeq : 0;
		clientInfo.push_back(info);
	}
}

bool DemoStream::readAuth(Client& client) {
	uint64_t now = getEpochMillis();

	if (client.rejected) {
		if (now >= client.authDeadline && !client.closing) {
			Segment seg = makeSegment(STREAM_PKT_AUTH_FAILED, NULL, 0, "", 0);
			client.prefix.insert(client.prefix.end(), seg->begin(), seg->end());
			client.closing = true;
		}
		return true;
	}

	if (client.checkingToken) {
		if (now >= client.authDeadline) {
			client.checkingToken = false; // the main thread never answered
			rejectClient(client, now);
		}
		return true;
	}

	if (now >= client.authDeadline) {
		return false;
	}

	char buf[4 + MAX_PASSWORD_LEN];
	int want = (int)sizeof(buf) - (int)client.authBuf.size();
	int got = recv(client.sock, buf, want, 0);
	if (got == 0 || (got == SOCKET_ERROR && !SOCK_WOULD_BLOCK)) {
		return false; // disconnected
	}
	if (got > 0) {
		client.authBuf.insert(client.authBuf.end(), buf, buf + got);
	}

	if (client.authBuf.size() < 4) {
		return true;
	}

	uint32_t len = 0;
	memcpy(&len, client.authBuf.data(), 4);
	if (len > MAX_PASSWORD_LEN) {
		return false;
	}
	if (client.authBuf.size() < 4 + len) {
		return true;
	}

	std::string attempt((const char*)client.authBuf.data() + 4, len);
	client.authBuf.clear();

	bool correct, checkToken;
	{
		std::lock_guard<std::mutex> guard(infoMutex);
		correct = attempt == password;
		checkToken = !correct && tokenLogins && !attempt.empty();
		if (checkToken) {
			tokenChecks.push_back({ client.id, attempt, 0 });
		}
	}

	if (correct) {
		acceptClient(client);
	}
	else if (checkToken) {
		client.checkingToken = true;
		client.authDeadline = now + AUTH_TIMEOUT_MS;
	}
	else {
		rejectClient(client, now);
	}

	return true;
}

bool DemoStream::flushClient(Client& client) {
	if (!client.authed) {
		if (!readAuth(client)) {
			return false;
		}
	}
	else {
		char discard[256];
		int got = recv(client.sock, discard, sizeof(discard), 0);
		if (got == 0 || (got == SOCKET_ERROR && !SOCK_WOULD_BLOCK)) {
			return false; // disconnected
		}
	}

	if (client.joined && client.nextSeq < firstSeq) {
		ALERT(at_console, "HLTV stream: dropping a client that fell too far behind\n");
		return false;
	}

	int budget = MAX_SEND_PER_THINK;

	while (budget > 0) {
		const uint8_t* data;
		size_t remaining;
		bool isPrefix = client.prefixSent < client.prefix.size();

		if (isPrefix) {
			data = client.prefix.data() + client.prefixSent;
			remaining = client.prefix.size() - client.prefixSent;
		}
		else if (client.joined && client.nextSeq < firstSeq + ring.size()) {
			const std::vector<uint8_t>& seg = *ring[client.nextSeq - firstSeq];
			data = seg.data() + client.segmentSent;
			remaining = seg.size() - client.segmentSent;
		}
		else {
			break;
		}

		int sendSz = (int)(std::min)(remaining, (size_t)budget);
#ifdef WIN32
		int sent = send(client.sock, (const char*)data, sendSz, 0);
#else
		int sent = send(client.sock, (const char*)data, sendSz, MSG_NOSIGNAL);
#endif
		if (sent == SOCKET_ERROR) {
			return SOCK_WOULD_BLOCK;
		}

		budget -= sent;
		client.bytesSent += sent;

		if (isPrefix) {
			client.prefixSent += sent;
			if (client.prefixSent >= client.prefix.size()) {
				client.prefix.clear();
				client.prefixSent = 0;
			}
		}
		else {
			client.segmentSent += sent;
			if ((size_t)sent == remaining) {
				client.nextSeq++;
				client.segmentSent = 0;
			}
		}
	}

	return !(client.closing && client.prefix.empty());
}

void DemoStream::think() {
	if (listenSock == -1) {
		updateClientInfo();
		return;
	}

	while (true) {
		sockaddr_in addr;
		socklen_t addrLen = sizeof(addr);
		intptr_t sock = (intptr_t)accept(listenSock, (sockaddr*)&addr, &addrLen);
		if (sock == (intptr_t)INVALID_SOCKET) {
			break;
		}

		if (clients.size() >= MAX_STREAM_CLIENTS) {
			closesock(sock);
			continue;
		}

		setNonBlocking(sock);
		int noDelay = 1;
		setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&noDelay, sizeof(noDelay));

		Client client;
		client.id = nextClientId++;
		client.sock = sock;
		client.address = std::string(inet_ntoa(addr.sin_addr)) + ":" + std::to_string(ntohs(addr.sin_port));
		client.connectTime = getEpochMillis();
		client.authDeadline = client.connectTime + AUTH_TIMEOUT_MS;

		uint32_t version = HLTV_STREAM_VERSION;
		client.prefix.resize(12);
		memcpy(client.prefix.data(), HLTV_STREAM_MAGIC, 8);
		memcpy(client.prefix.data() + 8, &version, 4);

		clients.push_back(client);
		ALERT(at_console, "HLTV stream: %s connected (%d total)\n", client.address.c_str(), (int)clients.size());
	}

	applyTokenResults();

	clients.erase(std::remove_if(clients.begin(), clients.end(), [this](Client& client) {
		if (!flushClient(client)) {
			closeClient(client);
			return true;
		}
		return false;
	}), clients.end());

	updateClientInfo();
}
