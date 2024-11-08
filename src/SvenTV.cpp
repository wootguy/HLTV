#include "SvenTV.h"
#include <iostream>
#include <ctime>

#ifdef HLCOOP_BUILD
#define FL_NOWEAPONS 134217728
#endif

SvenTV::SvenTV(bool singleThreadMode) {
	threadShouldExit = false;
	lastTvThink = 0;
	this->singleThreadMode = singleThreadMode;

	demoWriter = new DemoWriter();

	debugEdict = new netedict[MAX_EDICTS];

	frame.netedicts = new netedict[MAX_EDICTS];
	frame.netmessages = new NetMessageData[MAX_NETMSG_FRAME];
	frame.cmds = new CommandData[MAX_CMD_FRAME];
	frame.events = new DemoEventData[MAX_EVENT_FRAME];

	deltaPacketBufferSz = 508; // max UDP payload before possible fragmentation
	deltaPacketBuffer = new char[deltaPacketBufferSz];

	if (!singleThreadMode || true) {
		edicts = new edict_t[MAX_EDICTS];
		edictCopyState.setValue(EDICT_COPY_REQUESTED);

		if (!singleThreadMode)
			tv_thread = new thread(&SvenTV::think_tvThread, this);
	}
	else {
		socket = new Socket(SOCKET_UDP | SOCKET_NONBLOCKING, SVENTV_PORT);
		edicts = INDEXENT(0);
	}
}

SvenTV::~SvenTV() {
	threadShouldExit = true;

	delete demoWriter;

	for (int i = 0; i < MAX_CLIENTS; i++) {
		delete[] clients[i].baselines;
	}

	delete[] deltaPacketBuffer;
	delete[] frame.netedicts;
	delete[] debugEdict;

	if (!singleThreadMode) {
		tv_thread->join();
		delete tv_thread;
		delete[] edicts;
		delete[] frame.netmessages;
		delete[] frame.cmds;
	}
}

void SvenTV::think_mainThread() {
	uint64_t startMillis = getEpochMillis();
	/*
	if (singleThreadMode) {
		uint64_t now = getEpochMillis();
		if (TimeDifference(lastTvThink, now) >= 0.05f) {
			lastTvThink = now;

			for (int i = 0; i < MAX_EDICTS; i++) {
				frame.netedicts[i].load(edicts[i]);
			}

			//handleClientPackets();
			broadcastEntityStates();
			updateId++;
		}
	}
	else 
	*/
	{ // multi-threaded mode. Main thread only needs to copy current edict states

		// single thread mode must use new data immediately to avoid missing messages while waiting to write
		bool shouldCopy = (demoWriter->shouldWriteDemoFrame() || !singleThreadMode);
		
		if (edictCopyState.getValue() == EDICT_COPY_REQUESTED && shouldCopy) {
			if (singleThreadMode) {
				edicts = INDEXENT(0);
				frame.netmessages = g_netmessages;
				frame.cmds = g_cmds;
				frame.events = g_events;
			}
			else {
				// need to duplicate because the server thread will be running in parallel to the
				// tv thread which is working on these ents
				memcpy(edicts, INDEXENT(0), sizeof(edict_t) * MAX_EDICTS);
				memcpy(frame.netmessages, g_netmessages, g_netmessage_count * sizeof(NetMessageData));
				memcpy(frame.cmds, g_cmds, g_command_count * sizeof(CommandData));
				memcpy(frame.events, g_events, g_event_count * sizeof(DemoEventData));
			}

			frame.netmessage_count = g_netmessage_count;
			frame.cmds_count = g_command_count;
			frame.serverFrameCount = g_server_frame_count;
			frame.event_count = g_event_count;

			// clear the main thread buffers
			g_netmessage_count = 0;
			g_command_count = 0;
			g_event_count = 0;

			//int copySz = (sizeof(edict_t) * MAX_EDICTS) + (gpGlobals->maxClients * sizeof(DemoPlayerEnt)) + (g_netmessage_count * sizeof(NetMessageData)) + (g_command_count * sizeof(CommandData));
			edictCopyState.setValue(EDICT_COPY_FINISHED);

			g_copyTime = getEpochMillis() - startMillis;
			//ALERT(at_console, "Copy %.1fKB in %lums\n", copySz / 1024.0f, copyTime);

			if (singleThreadMode) {
				think_tvThread();
			}
		} else if (singleThreadMode) {
			think_tvThread();
		}
	}
}

void SvenTV::handleDeltaAck(mstream& reader, NetClient& client) {
	uint16_t updateId;

	reader.read(&updateId, 2);

	int deltaIdx = -1;
	for (int i = 0; i < (int)client.sentDeltas.size(); i++) {
		if (client.sentDeltas[i].updateId == updateId) {
			deltaIdx = i;
			break;
		}
	}

	if (deltaIdx == -1) {
		ALERT(at_console, "Got ack for too old update %d\n", (int)updateId);
		return;
	}

	DeltaUpdate& update = client.sentDeltas[deltaIdx];

	if (update.packets.empty()) {
		ALERT(at_console, "Ignoring duplicate ack\n");
		return;
	}

	for (int i = 0; i < (int)update.packets.size(); i++) {
		int bit = reader.readBit();
		if (bit == -1) {
			ALERT(at_console, "Failed to read ack, invalid number of bits sent\n");
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
	//ALERT(at_console, "OKIE DID ACK %d\n", updateId);
}

void SvenTV::handleClientPackets() {
	Packet packet;
	while (!threadShouldExit) {
		if (!socket->recv(packet)) {
			return;
		}

		if (packet.sz == 0) {
			ALERT(at_console, "Ignore 0 size packet\n");
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
				ALERT(at_console, "Ignore connect packet for existing client\n");
				continue;
			}
			ALERT(at_console, "New client connected from %s\n", packet.addr.getString().c_str());

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
				welcomeDat[0] = (char)SVC_WELCOME;
				memcpy(welcomeDat + 1, mapName, welcomDatSz);

				socket->send(Packet(packet.addr, welcomeDat, welcomDatSz));
			}

			continue;
		}

		if (clientIdx == -1) {
			ALERT(at_console, "Ignore packet from unknown client\n");
			continue;
		}

		NetClient& client = clients[clientIdx];
		client.lastPacketTime = getEpochMillis();

		if (packetType == CLC_DELTA_RESET) {
			ALERT(at_console, "Client reset delta state to null\n");
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

	//const int maxPacketSz = 508; // 508 = max size before fragmentation is possible

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
			ALERT(at_console, "Write delta %d frag %d\n", (int)updateId, (int)fragmentId);
		}
		fragmentId++;

		uint8_t offset = 0; // always write full index for first entity delta

		vector<Packet> deltaPackets;

		for (uint16_t i = 0; i < MAX_EDICTS; i++) {
			netedict& now = frame.netedicts[i];

			uint64_t startOffset = buffer.tell();
			uint8_t initialOffset = offset;

			buffer.write(&offset, 1); // entity index the delta is for (offset from previous)
			if (offset == 0) {
				// last edict was 256+ slots away. Write full index.
				buffer.write(&i, 2);
			}

			uint64_t datOffset = buffer.tell();

			int ret = now.writeDeltas(buffer, clients[k].baselines[i]);

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
					ALERT(at_console, "Write delta %d frag %d\n", (int)updateId, (int)fragmentId);
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
				//int writeSz = (int)(buffer.tell() - startOffset);
				offset = 1;
				totalEnts++;
				if (debugMode) {
					uint32_t bits = *((uint32_t*)(buffer.getBuffer() + datOffset));
					ALERT(at_console, "Wrote ent idx %d (%d bytes) offset %d, bits %lu\n", (int)i, (int)(buffer.tell() - datOffset), (int)initialOffset, bits);
				}
				if (fragmentId - 1 == debugFrag && debugFrag != -1) {
					totalEnts--; totalEnts++; // set breakpoint here
				}
				//ALERT(at_console, "Write %d bytes for edict %d (%d total)\n", writeSz, i, totalBytes);
				//ALERT(at_console, "Write %d bytes for edict %s (%d total)\n", writeSz, STRING(edicts[i].v.classname), totalBytes);
			}
		}

		if (buffer.tell() > 7) { // more than just the header written
			deltaPackets.push_back(Packet(clients[k].addr, deltaPacketBuffer, buffer.tell()));
		}
		else {
			// TODO: Don't send empty updates (only here because clients get disconnected for lack of acks)
			deltaPackets.push_back(Packet(clients[k].addr, deltaPacketBuffer, buffer.tell()));
		}

		// assume client acked
		
		if (debugMode) {
			bool redoWrite = false;

			memcpy(debugEdict, clients[k].baselines, MAX_EDICTS * sizeof(netedict));

			for (int i = 0; i < (int)deltaPackets.size(); i++) {
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
				ALERT(at_console, "OK DO IT AGAIN\n");
				k--;
				continue;
			}

			/*
			for (int i = 0; i < MAX_EDICTS; i++) {
				if (!clients[k].baselines[i].matches(netedicts[i])) {
					ALERT(at_console, "ZOMG BAD\n");
				}
			}
			*/
			
			//memcpy(clients[k].baselines, netedicts, MAX_EDICTS * sizeof(netedict));
		}

		
		int totalSz = 0;
		int numPackets = deltaPackets.size();
		for (int i = 0; i < (int)deltaPackets.size(); i++) {
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
		ALERT(at_console, "Send %s: %4d B, %d packets, update %d, baseline %d, %.1f KB/s\n", 
			clients[k].addr.getString().c_str(), totalSz, numPackets, (int)updateId, clients[k].baselineId, kbsec);

		if (clients[k].sentDeltas.size() > 100) {
			if (!debugMode)
				ALERT(at_console, "Client %d not acking!\n", k);
			clients[k].sentDeltas.erase(clients[k].sentDeltas.begin());
		}

		if (TimeDifference(clients[k].lastPacketTime, now) > timeoutSeconds && !debugMode) {
			ALERT(at_console, "Disconnecting unresponsive client\n");
			clients[k].isFree = true;
			clients[k].sentDeltas.clear();
			clients[k].sentBytesHistory.clear();
		}

		clientCount++;
	}

	//ALERT(at_console, "Send %d ents to %d clients (%d bytes each)\n", totalEnts, clientCount, sizeof(netedict));
}

void SvenTV::think_tvThread() {
	bool loadNewData = true;

	while (!threadShouldExit) {
		while (!singleThreadMode && edictCopyState.getValue() != EDICT_COPY_FINISHED && !threadShouldExit) {
			if (demoWriter->isFileOpen() && !enableDemoFile) {
				demoWriter->closeDemoFile();
			}
			this_thread::sleep_for(chrono::milliseconds(1));
		}
		uint64_t startMillis = getEpochMillis();

		if (loadNewData) {
			for (int i = 0; i < gpGlobals->maxEntities; i++) {
				if (i > 0 && i <= MAX_PLAYERS && (edicts[i].v.flags & FL_CLIENT) == 0) {
					frame.netedicts[i].reset();
					continue;
				}
				frame.netedicts[i].load(edicts[i]);
			}
			loadNewData = false;
		}

		bool wantMoreData = false;

		if (enableDemoFile) {
			wantMoreData = wantMoreData || demoWriter->writeDemoFile(frame);
		}
		else if (demoWriter->isFileOpen()) {
			demoWriter->closeDemoFile();
		}

		if (enableServer && !socket) {
			socket = new Socket(SOCKET_UDP | SOCKET_NONBLOCKING, SVENTV_PORT);
		}
		if (socket) {
			wantMoreData = true;
			handleClientPackets();
			broadcastEntityStates();
			updateId++;
		}

		if (wantMoreData) {
			edictCopyState.setValue(EDICT_COPY_REQUESTED);
			loadNewData = true;
			g_thinkTime = getEpochMillis() - startMillis;
		}

		if (singleThreadMode) {
			break;
		}
	}

	if (threadShouldExit) {
		demoWriter->closeDemoFile();
		delete socket;
	}
}
