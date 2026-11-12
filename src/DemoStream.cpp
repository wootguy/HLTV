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
#include <math.h>
#include <algorithm>
#include <string>

#define MAX_SEND_PER_THINK (1024 * 256) // per client, to keep a backlog from stalling the server frame
#define MAX_PASSWORD_LEN 256 // website session tokens are 172 characters
#define AUTH_TIMEOUT_MS 5000
#define AUTH_FAIL_DELAY_MS 2000 // with the client limit, this caps how fast passwords can be guessed
#define CLOSE_TIMEOUT_MS 5000 // how long a closing client gets to receive its last packet
#define MAX_PASSWORD_CLIENTS_PER_IP 3 // connections that logged in with the stream password, not a token
#define MAX_CLIENT_PACKET 1024
#define MAX_COORD 131072.0f
#define MAX_VIEW_TARGET 32 // highest player entity index

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
		if (client.authed && !client.closing) {
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

	client.viewersSent = 0; // send the viewer list again after the header
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

void DemoStream::setViewers(const std::vector<StreamViewer>& newViewers) {
	std::lock_guard<std::mutex> guard(infoMutex);
	viewers = newViewers;
	viewersVersion++;
}

void DemoStream::sendViewers(Client& client) {
	std::vector<uint8_t> payload(2);
	uint16_t count = 0;
	{
		std::lock_guard<std::mutex> guard(infoMutex);
		if (client.viewersSent == viewersVersion) {
			return;
		}
		client.viewersSent = viewersVersion;

		for (const StreamViewer& viewer : viewers) {
			uint8_t nameLen = (uint8_t)(std::min)(viewer.name.size(), (size_t)255);
			uint8_t entry[10];
			memcpy(entry, &viewer.entindex, 2);
			entry[2] = viewer.topColor;
			entry[3] = viewer.bottomColor;
			entry[4] = viewer.clientId == client.id ? VIEWER_FL_YOU : 0;
			memcpy(entry + 5, &viewer.delaySec, 4);
			entry[9] = nameLen;
			payload.insert(payload.end(), entry, entry + 10);
			payload.insert(payload.end(), viewer.name.begin(), viewer.name.begin() + nameLen);
			count++;
		}

		// what each viewer is watching, after the list so older clients don't read it
		for (const StreamViewer& viewer : viewers) {
			uint8_t entry[8];
			entry[0] = viewer.view.known ? VIEW_INFO_KNOWN : 0;
			entry[1] = viewer.view.target;
			entry[2] = viewer.view.camMode;
			entry[3] = viewer.view.director;
			memcpy(entry + 4, &viewer.view.chaseDist, 4);
			payload.insert(payload.end(), entry, entry + 8);
		}
	}
	memcpy(payload.data(), &count, 2);

	Segment seg = makeSegment(STREAM_PKT_VIEWERS, NULL, 0, payload.data(), payload.size());
	client.prefix.insert(client.prefix.end(), seg->begin(), seg->end());
}

bool DemoStream::readClientPackets(Client& client) {
	char buf[1024];
	while (client.inBuf.size() < MAX_CLIENT_PACKET * 4) {
		int got = recv(client.sock, buf, sizeof(buf), 0);
		if (got == 0 || (got == SOCKET_ERROR && !SOCK_WOULD_BLOCK)) {
			return false; // disconnected
		}
		if (got < 0) {
			break; // nothing more to read
		}
		client.inBuf.insert(client.inBuf.end(), buf, buf + got);
	}

	size_t pos = 0;
	while (client.inBuf.size() - pos >= 5) {
		uint8_t type = client.inBuf[pos];
		uint32_t sz = 0;
		memcpy(&sz, client.inBuf.data() + pos + 1, 4);
		if (sz > MAX_CLIENT_PACKET) {
			return false;
		}
		if (client.inBuf.size() - pos - 5 < sz) {
			break;
		}
		const uint8_t* payload = client.inBuf.data() + pos + 5;

		if (type == CLIENT_PKT_VIEW && sz >= 28) {
			float vals[6];
			memcpy(vals, payload, 24);
			bool valid = true;
			for (int i = 0; i < 6; i++) {
				valid = valid && vals[i] == vals[i] && fabsf(vals[i]) <= MAX_COORD; // not NaN or huge
			}
			if (valid) {
				memcpy(client.camOrigin, vals, 12);
				memcpy(client.camAngles, vals + 3, 12);
				memcpy(&client.delayMs, payload + 24, 4);
				client.hasCamera = true;
			}

			// what it's watching, from newer clients
			if (valid && sz >= 35) {
				StreamViewInfo view;
				float chaseDist;
				memcpy(&chaseDist, payload + 31, 4);
				view.known = true;
				view.target = payload[28] <= MAX_VIEW_TARGET ? payload[28] : 0;
				view.camMode = payload[29] < VIEW_CAM_COUNT ? payload[29] : VIEW_CAM_FREE;
				view.director = payload[30] < VIEW_DIRECTOR_COUNT ? payload[30] : VIEW_DIRECTOR_OFF;
				view.chaseDist = chaseDist == chaseDist ? (std::max)(0.0f, (std::min)(chaseDist, MAX_CHASE_DIST)) : 0;
				if (!view.target) {
					view.camMode = VIEW_CAM_FREE;
				}
				client.view = view;
			}
			if (valid && sz >= 36) {
				client.loadState = payload[35] < VIEW_LOAD_COUNT ? payload[35] : VIEW_LOAD_READY;
			}
		}

		pos += 5 + sz;
	}
	client.inBuf.erase(client.inBuf.begin(), client.inBuf.begin() + pos);
	return true;
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

				// one connection per Steam ID. The newest one wins, because an older one may be a
				// page that was reloaded or a connection that died without closing.
				for (Client& other : clients) {
					if (&other != &client && other.steamid64 == result.steamid64 && !other.closing) {
						replaceClient(other, now, "a newer connection from the same steam ID");
					}
				}

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

void DemoStream::replaceClient(Client& client, uint64_t now, const char* reason) {
	Segment seg = makeSegment(STREAM_PKT_REPLACED, NULL, 0, "", 0);
	client.prefix.insert(client.prefix.end(), seg->begin(), seg->end());
	client.closing = true;
	client.closeDeadline = now + CLOSE_TIMEOUT_MS;
	ALERT(at_console, "HLTV stream: %s was replaced by %s\n", client.address.c_str(), reason);
}

// Called before a client that gave the stream password is accepted. Closes the oldest password
// connections from its IP so there are at most MAX_PASSWORD_CLIENTS_PER_IP, counting the new one.
// Like a Steam ID login, the newest wins, because the old ones may be reloaded pages.
void DemoStream::limitPasswordConnections(const Client& client, uint64_t now) {
	int count = 1;
	for (const Client& other : clients) {
		if (&other != &client && other.ip == client.ip && other.authed && !other.steamid64 && !other.closing) {
			count++;
		}
	}

	for (Client& other : clients) { // oldest first
		if (count <= MAX_PASSWORD_CLIENTS_PER_IP) {
			break;
		}
		if (&other != &client && other.ip == client.ip && other.authed && !other.steamid64 && !other.closing) {
			replaceClient(other, now, "a newer password connection from the same IP");
			count--;
		}
	}
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
		info.id = client.id;
		info.address = client.address;
		info.state = client.closing ? "disconnecting"
			: (client.joined ? "watching" : (client.authed ? "waiting" : "logging in"));
		info.steamid64 = client.steamid64;
		info.authed = client.authed && !client.closing;
		info.seconds = (uint32_t)((now - client.connectTime) / 1000);
		info.bytesSent = client.bytesSent;
		info.behind = client.joined ? (firstSeq + ring.size()) - client.nextSeq : 0;
		info.joined = client.joined && !client.closing;
		info.hasCamera = client.hasCamera;
		memcpy(info.camOrigin, client.camOrigin, sizeof(info.camOrigin));
		memcpy(info.camAngles, client.camAngles, sizeof(info.camAngles));
		info.delayMs = client.delayMs;
		info.view = client.view;
		info.loadState = client.joined && client.hasCamera ? client.loadState : VIEW_LOAD_MAP;
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
			client.closeDeadline = now + CLOSE_TIMEOUT_MS;
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
		limitPasswordConnections(client, now);
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
	if (client.closing && getEpochMillis() >= client.closeDeadline) {
		return false; // not reading what's sent to it
	}

	if (!client.authed) {
		if (!readAuth(client)) {
			return false;
		}
	}
	else if (!readClientPackets(client)) {
		return false;
	}

	if (client.joined && !client.closing) {
		sendViewers(client);
	}

	if (client.joined && client.nextSeq < firstSeq) {
		ALERT(at_console, "HLTV stream: dropping a client that fell too far behind\n");
		return false;
	}

	int budget = MAX_SEND_PER_THINK;

	while (budget > 0) {
		const uint8_t* data;
		size_t remaining;
		// the prefix can grow while a frame is half sent, so it waits for the frame to finish
		bool isPrefix = client.prefixSent < client.prefix.size() && client.segmentSent == 0;

		if (isPrefix) {
			data = client.prefix.data() + client.prefixSent;
			remaining = client.prefix.size() - client.prefixSent;
		}
		else if (client.joined && client.nextSeq < firstSeq + ring.size()
			&& (!client.closing || client.segmentSent > 0)) { // a closing client only finishes its frame
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

		if (clients.size() >= (size_t)maxClients) {
			closesock(sock);
			continue;
		}

		setNonBlocking(sock);
		int noDelay = 1;
		setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&noDelay, sizeof(noDelay));

		Client client;
		client.id = nextClientId++;
		client.sock = sock;
		client.ip = inet_ntoa(addr.sin_addr);
		client.address = client.ip + ":" + std::to_string(ntohs(addr.sin_port));
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
