#include "SvenTV.h"
#include "meta_utils.h"
#include "mstream.h"

SvenTV::SvenTV(bool singleThreadMode) {
	threadShouldExit = false;
	lastTvThink = 0;
	this->singleThreadMode = singleThreadMode;

	netedicts = new netedict[MAX_EDICTS];
	debugEdict = new netedict[MAX_EDICTS];
	
	// size of full delta on every edict + byte for each index delta + 2 bytes for each delta bits on edict
	// + final byte for packet type
	//deltaPacketBufferSz = 2048;

	deltaPacketBufferSz = 508; // max UDP payload before possible fragmentation

	deltaPacketBuffer = new char[deltaPacketBufferSz];

	if (!singleThreadMode) {
		edicts = new edict_t[MAX_EDICTS];
		edictCopyState.setValue(EDICT_COPY_REQUESTED);
		tv_thread = new thread(&SvenTV::think_tvThread, this);
	}
	else {
		socket = new Socket(SOCKET_UDP | SOCKET_NONBLOCKING, SVENTV_PORT);
		edicts = INDEXENT(0);
	}
}

SvenTV::~SvenTV() {
	threadShouldExit = true;

	if (tv_thread) {
		tv_thread->join();
		delete tv_thread;
		delete[] edicts;
	}

	for (int i = 0; i < MAX_CLIENTS; i++) {
		delete[] clients[i].baselines;
	}

	delete[] deltaPacketBuffer;
	delete[] netedicts;
	delete[] debugEdict;
}

void SvenTV::think_mainThread() {
	int copySz = sizeof(edict_t) * MAX_EDICTS;

	uint64_t startMillis = getEpochMillis();

	if (singleThreadMode) {
		uint64_t now = getEpochMillis();
		if (TimeDifference(lastTvThink, now) >= 0.05f) {
			lastTvThink = now;

			for (int i = 0; i < MAX_EDICTS; i++) {
				netedicts[i].load(edicts[i]);
			}

			//handleClientPackets();
			broadcastEntityStates();
			updateId++;
		}
	}
	else {
		if (edictCopyState.getValue() == EDICT_COPY_REQUESTED) {
			memcpy(edicts, INDEXENT(0), copySz);

			uint32_t diff = getEpochMillis() - startMillis;
			//println("Copy time %lums", diff);
			edictCopyState.setValue(EDICT_COPY_FINISHED);
		}
	}
}

int SvenTV::writeEdictDelta(mstream& writer, const netedict& old, const netedict& now) {
	uint64_t startOffset = writer.tell();

	uint32_t deltaBits = 0; // flags which fields were changed
	

	if (old.isValid != now.isValid) {
		if (!now.isValid) {
			// 0 deltas indicates edict was deleted
			writer.write(&deltaBits, 4);

			if (writer.eom()) {
				writer.seek(startOffset);
				return EDELTA_OVERFLOW;
			}

			return EDELTA_WRITE;
		}
	}

	writer.skip(4); // write delta bits later

	if (old.origin[0] != now.origin[0]) {
		deltaBits |= FL_DELTA_ORIGIN_X;
		writer.write((void*)&now.origin[0], 4);
	}
	if (old.origin[1] != now.origin[1]) {
		deltaBits |= FL_DELTA_ORIGIN_Y;
		writer.write((void*)&now.origin[1], 4);
	}
	if (old.origin[2] != now.origin[2]) {
		deltaBits |= FL_DELTA_ORIGIN_Z;
		writer.write((void*)&now.origin[2], 4);
	}
	if (old.angles[0] != now.angles[0]) {
		deltaBits |= FL_DELTA_ANGLES_X;
		writer.write((void*)&now.angles[0], 2);
	}
	if (old.angles[1] != now.angles[1]) {
		deltaBits |= FL_DELTA_ANGLES_Y;
		writer.write((void*)&now.angles[1], 2);
	}
	if (old.angles[2] != now.angles[2]) {
		deltaBits |= FL_DELTA_ANGLES_Z;
		writer.write((void*)&now.angles[2], 2);
	}
	if (old.modelindex != now.modelindex) {
		deltaBits |= FL_DELTA_MODELINDEX;
		writer.write((void*)&now.modelindex, 2);
	}
	if (old.skin != now.skin) {
		deltaBits |= FL_DELTA_SKIN;
		writer.write((void*)&now.skin, 1);
	}
	if (old.body != now.body) {
		deltaBits |= FL_DELTA_BODY;
		writer.write((void*)&now.body, 1);
	}
	if (old.effects != now.effects) {
		deltaBits |= FL_DELTA_EFFECTS;
		writer.write((void*)&now.effects, 1);
	}
	if (old.sequence != now.sequence) {
		deltaBits |= FL_DELTA_SEQUENCE;
		writer.write((void*)&now.sequence, 1);
	}
	if (old.gaitsequence != now.gaitsequence) {
		deltaBits |= FL_DELTA_GAITSEQUENCE;
		writer.write((void*)&now.gaitsequence, 1);
	}
	if (old.frame != now.frame) {
		deltaBits |= FL_DELTA_FRAME;
		writer.write((void*)&now.frame, 1);
	}
	if (old.animtime != now.animtime) {
		deltaBits |= FL_DELTA_ANIMTIME;
		writer.write((void*)&now.animtime, 1);
	}
	if (old.framerate != now.framerate) {
		deltaBits |= FL_DELTA_FRAMERATE;
		writer.write((void*)&now.framerate, 1);
	}
	if (old.controller[0] != now.controller[0]) {
		deltaBits |= FL_DELTA_CONTROLLER_0;
		writer.write((void*)&now.controller[0], 1);
	}
	if (old.controller[1] != now.controller[1]) {
		deltaBits |= FL_DELTA_CONTROLLER_1;
		writer.write((void*)&now.controller[1], 1);
	}
	if (old.controller[2] != now.controller[2]) {
		deltaBits |= FL_DELTA_CONTROLLER_2;
		writer.write((void*)&now.controller[2], 1);
	}
	if (old.controller[3] != now.controller[3]) {
		deltaBits |= FL_DELTA_CONTROLLER_3;
		writer.write((void*)&now.controller[3], 1);
	}
	if (old.blending[0] != now.blending[0]) {
		deltaBits |= FL_DELTA_BLENDING_0;
		writer.write((void*)&now.blending[0], 1);
	}
	if (old.blending[1] != now.blending[1]) {
		deltaBits |= FL_DELTA_BLENDING_1;
		writer.write((void*)&now.blending[1], 1);
	}
	if (old.scale != now.scale) {
		deltaBits |= FL_DELTA_SCALE;
		writer.write((void*)&now.scale, 1);
	}
	if (old.rendermode != now.rendermode) {
		deltaBits |= FL_DELTA_RENDERMODE;
		writer.write((void*)&now.rendermode, 1);
	}
	if (old.renderamt != now.renderamt) {
		deltaBits |= FL_DELTA_RENDERAMT;
		writer.write((void*)&now.renderamt, 1);
	}
	if (old.rendercolor[0] != now.rendercolor[0]) {
		deltaBits |= FL_DELTA_RENDERCOLOR_0;
		writer.write((void*)&now.rendercolor[0], 1);
	}
	if (old.rendercolor[1] != now.rendercolor[1]) {
		deltaBits |= FL_DELTA_RENDERCOLOR_1;
		writer.write((void*)&now.rendercolor[1], 1);
	}
	if (old.rendercolor[2] != now.rendercolor[2]) {
		deltaBits |= FL_DELTA_RENDERCOLOR_2;
		writer.write((void*)&now.rendercolor[2], 1);
	}
	if (old.renderfx != now.renderfx) {
		deltaBits |= FL_DELTA_RENDERFX;
		writer.write((void*)&now.renderfx, 1);
	}
	if (old.aiment != now.aiment) {
		deltaBits |= FL_DELTA_AIMENT;
		writer.write((void*)&now.aiment, 1);
	}

	if (writer.eom()) {
		writer.seek(startOffset);
		return EDELTA_OVERFLOW;
	}

	uint64_t currentOffset = writer.tell();
	writer.seek(startOffset);

	if (deltaBits == 0) {
		return EDELTA_NONE;
	}

	writer.write((void*)&deltaBits, 4);
	writer.seek(currentOffset);

	return EDELTA_WRITE;
}

void SvenTV::handleDeltaAck(mstream& reader, NetClient& client) {
	uint16_t updateId;

	reader.read(&updateId, 2);

	int deltaIdx = -1;
	for (int i = 0; i < client.sentDeltas.size(); i++) {
		if (client.sentDeltas[i].updateId == updateId) {
			deltaIdx = i;
			break;
		}
	}

	if (deltaIdx == -1) {
		println("Got ack for too old update %d", (int)updateId);
		return;
	}

	DeltaUpdate& update = client.sentDeltas[deltaIdx];

	if (update.packets.empty()) {
		println("Ignoring duplicate ack");
		return;
	}

	int packetIdx = 0;
	for (int i = 0; i < update.packets.size(); i++) {
		int bit = reader.readBit();
		if (bit == -1) {
			println("Failed to read ack, invalid number of bits sent");
			return;
		}

		if (bit == 1) {
			client.applyDeltaToBaseline(update.packets[i], false);
		}
	}
	client.baselineId = updateId;

	// don't need any history older than the current baseline
	client.sentDeltas.erase(client.sentDeltas.begin(), client.sentDeltas.begin() + deltaIdx);
	
	//update.packets.clear();
	//println("OKIE DID ACK %d", updateId);
}

void SvenTV::handleClientPackets() {
	Packet packet;
	while (!threadShouldExit) {
		if (!socket->recv(packet)) {
			return;
		}

		if (packet.sz == 0) {
			println("Ignore 0 size packet");
			continue;
		}

		uint8_t packetType;
		mstream reader(packet.data, packet.sz);
		reader.read(&packetType, 1);

		int clientIdx = -1;
		for (int i = 0; i < MAX_CLIENTS; i++) {
			if (!clients[i].isFree && clients[i].addr == packet.addr) {
				clientIdx = i;
				break;
			}
		}

		if (packetType == CLC_CONNECT) {
			if (clientIdx != -1) {
				println("Ignore connect packet for existing client");
				continue;
			}
			println("New client connected from %s", packet.addr.getString().c_str());

			bool foundFree = false;
			for (int i = 0; i < MAX_CLIENTS; i++) {
				if (clients[i].isFree) {
					clients[i].init(packet.addr);
					foundFree = true;
					break;
				}
			}

			if (!foundFree) {
				const char* fullStr = "sventv_full";
				socket->send(Packet(packet.addr, fullStr, strlen(fullStr)));
			}
			else {
				const char* mapName = STRING(gpGlobals->mapname);
				int welcomDatSz = 1 + strlen(mapName);
				static char welcomeDat[64];
				welcomeDat[0] = SVC_WELCOME;
				memcpy(welcomeDat + 1, mapName, welcomDatSz);

				socket->send(Packet(packet.addr, welcomeDat, welcomDatSz));
			}

			continue;
		}

		if (clientIdx == -1) {
			println("Ignore packet from unknown client");
			continue;
		}

		NetClient& client = clients[clientIdx];
		client.lastPacketTime = getEpochMillis();

		if (packetType == CLC_DELTA_RESET) {
			println("Client reset delta state to null");
			client.baselineId = 0;
			client.sentDeltas.clear();
			memset(client.baselines, 0, MAX_EDICTS * sizeof(netedict));
		}
		if (packetType == CLC_DELTA_ACK) {
			handleDeltaAck(reader, client);
		}
	}
	
}

void SvenTV::broadcastEntityStates() {
	int totalEnts = 0;

	int clientCount = 0;

	const int maxPacketSz = 508; // 508 = max size before fragmentation is possible

	mstream buffer(deltaPacketBuffer, deltaPacketBufferSz);

	uint64_t now = getEpochMillis();

	if (abortEverything) {
		return;
	}

	bool debugMode = false;
	int debugFrag = -1;
	int debugEnt = -1;

	for (int k = 0; k < MAX_CLIENTS; k++) {
		if (clients[k].isFree && (k != 0 || !debugMode)) {
			continue;
		}

		if (clients[k].nextUpdateTime > now && debugFrag == -1) {
			continue;
		}

		uint64_t updateDelay = (1.0f / clients[k].updateRateFps) * 1000;
		clients[k].nextUpdateTime = now + updateDelay;

		const uint8_t packetType = SVC_DELTAPACKETENTITIES;

		// in case the update is split into multiple packets, the client needs to be able
		// to tell the server which packets it received.
		uint16_t fragmentId = 0;

		buffer.seek(0);
		buffer.write((void*)&packetType, 1);
		buffer.write((void*)&updateId, 2);
		buffer.write((void*)&clients[k].baselineId, 2);
		buffer.write((void*)&fragmentId, 2);
		if (debugMode) {
			println("Write delta %d frag %d", (int)updateId, (int)fragmentId);
		}
		fragmentId++;

		uint8_t offset = 0; // always write full index for first entity delta

		vector<Packet> deltaPackets;

		for (uint16_t i = 0; i < MAX_EDICTS; i++) {
			netedict& now = netedicts[i];

			uint64_t startOffset = buffer.tell();
			uint8_t initialOffset = offset;

			buffer.write(&offset, 1); // entity index the delta is for (offset from previous)
			if (offset == 0) {
				// last edict was 256+ slots away. Write full index.
				buffer.write(&i, 2);
			}

			uint64_t datOffset = buffer.tell();

			int ret = writeEdictDelta(buffer, clients[k].baselines[i], now);

			if (ret == EDELTA_OVERFLOW) {
				Packet delta = Packet(clients[k].addr, deltaPacketBuffer, startOffset);
				deltaPackets.push_back(delta);
				
				// set up new packet
				buffer.seek(0);
				buffer.write((void*)&packetType, 1);
				buffer.write((void*)&updateId, 2);
				buffer.write((void*)&clients[k].baselineId, 2);
				buffer.write((void*)&fragmentId, 2);
				if (debugMode) {
					println("Write delta %d frag %d", (int)updateId, (int)fragmentId);
				}
				fragmentId++;
				offset = 0; // always write full index for first entity delta
				
				// redo this entity delta in the new packet
				i--;
				continue;
			}
			else if (ret == EDELTA_NONE) {
				// no differences
				buffer.seek(startOffset); // undo index write
				if (offset != 0) {
					offset += 1;
				}
			}
			else {
				// delta written
				int writeSz = (int)(buffer.tell() - startOffset);
				offset = 1;
				totalEnts++;
				if (debugMode) {
					uint32_t bits = *((uint32_t*)(buffer.getBuffer() + datOffset));
					println("Wrote ent idx %d (%d bytes) offset %d, bits %lu", (int)i, (int)(buffer.tell() - datOffset), (int)initialOffset, bits);
				}
				if (fragmentId - 1 == debugFrag && debugFrag != -1) {
					totalEnts--;
					totalEnts++;
				}
				//println("Write %d bytes for edict %d (%d total)", writeSz, i, totalBytes);
				//println("Write %d bytes for edict %s (%d total)", writeSz, STRING(edicts[i].v.classname), totalBytes);
			}
		}

		if (buffer.tell() > 7) { // more than just the header written
			deltaPackets.push_back(Packet(clients[k].addr, deltaPacketBuffer, buffer.tell()));
		}

		// assume client acked
		
		if (debugMode) {
			bool redoWrite = false;

			memcpy(debugEdict, clients[k].baselines, MAX_EDICTS * sizeof(netedict));

			for (int i = 0; i < deltaPackets.size(); i++) {
				Packet& deltaPacket = deltaPackets[i];
				debugEnt = clients[k].applyDeltaToBaseline(deltaPacket, debugMode);
				if (debugEnt != -1) {
					abortEverything = true;
					
					if (debugFrag != -1) {
						return;
					}

					redoWrite = true;
					debugFrag = i;
					break;
				}
			}

			if (redoWrite) {
				memcpy(clients[k].baselines, debugEdict, MAX_EDICTS * sizeof(netedict));
				println("OK DO IT AGAIN");
				k--;
				continue;
			}

			/*
			for (int i = 0; i < MAX_EDICTS; i++) {
				if (!clients[k].baselines[i].matches(netedicts[i])) {
					println("ZOMG BAD");
				}
			}
			*/
			
			//memcpy(clients[k].baselines, netedicts, MAX_EDICTS * sizeof(netedict));
		}

		
		int totalSz = 0;
		int numPackets = deltaPackets.size();
		for (int i = 0; i < deltaPackets.size(); i++) {
			Packet& deltaPacket = deltaPackets[i];
			if (!debugMode)
				socket->send(deltaPacket);		
			totalSz += deltaPacket.sz;
		}

		DeltaUpdate update;
		update.packets = deltaPackets;
		update.updateId = updateId;
		clients[k].sentDeltas.push_back(update);

		NetUsageDatapoint datapoint;
		datapoint.bytes = totalSz;
		datapoint.time = getEpochMillis();
		clients[k].sentBytesHistory.push_back(datapoint);
		float kbsec = clients[k].getBytesSentPerSecond() / 1024.0f;
		println("Send %s: %4d B, %d packets, update %d, baseline %d, %.1f KB/s", 
			clients[k].addr.getString().c_str(), totalSz, numPackets, (int)updateId, clients[k].baselineId, kbsec);

		if (clients[k].sentDeltas.size() > 100) {
			if (!debugMode)
				println("Client %d not acking!", k);
			clients[k].sentDeltas.erase(clients[k].sentDeltas.begin());
		}

		if (TimeDifference(clients[k].lastPacketTime, now) > timeoutSeconds && !debugMode) {
			println("Disconnecting unresponsive client");
			clients[k].isFree = true;
			clients[k].sentDeltas.clear();
			clients[k].sentBytesHistory.clear();
		}

		clientCount++;
	}

	//println("Send %d ents to %d clients (%d bytes each)", totalEnts, clientCount, sizeof(netedict));
}

void SvenTV::think_tvThread() {
	socket = new Socket(SOCKET_UDP | SOCKET_NONBLOCKING, SVENTV_PORT);

	while (!threadShouldExit) {
		while (edictCopyState.getValue() != EDICT_COPY_FINISHED && !threadShouldExit) {
			this_thread::sleep_for(chrono::milliseconds(1));
		}
		for (int i = 0; i < MAX_EDICTS; i++) {
			netedicts[i].load(edicts[i]);
		}
		edictCopyState.setValue(EDICT_COPY_REQUESTED);

		handleClientPackets();
		broadcastEntityStates();
		updateId++;
	}

	delete socket;
}
