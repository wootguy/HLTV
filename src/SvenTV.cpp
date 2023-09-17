#include "SvenTV.h"
#include "mmlib.h"
#include <iostream>
#include <ctime>

SvenTV::SvenTV(bool singleThreadMode) {
	threadShouldExit = false;
	lastTvThink = 0;
	this->singleThreadMode = singleThreadMode;

	demoWriter = new DemoWriter();

	debugEdict = new netedict[MAX_EDICTS];

	frame.netedicts = new netedict[MAX_EDICTS];
	frame.playerinfos = new DemoPlayerEnt[32];
	frame.netmessages = new NetMessageData[MAX_NETMSG_FRAME];
	frame.cmds = new CommandData[MAX_CMD_FRAME];

	memset(frame.playerinfos, 0, 32 * sizeof(DemoPlayerEnt));

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

	println("ZOMG SIZE: %d", (int)sizeof(netedict));
}

SvenTV::~SvenTV() {
	threadShouldExit = true;

	if (tv_thread) {
		tv_thread->join();
		delete tv_thread;
		delete[] edicts;
	}

	delete demoWriter;

	for (int i = 0; i < MAX_CLIENTS; i++) {
		delete[] clients[i].baselines;
	}

	delete[] deltaPacketBuffer;
	delete[] frame.netedicts;
	delete[] debugEdict;
	delete[] frame.playerinfos;
	delete[] frame.netmessages;
	delete[] frame.cmds;
}

void SvenTV::think_mainThread() {
	uint64_t startMillis = getEpochMillis();

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
	else { // multi-threaded mode. Main thread only needs to copy current edict states
		if (edictCopyState.getValue() == EDICT_COPY_REQUESTED) {
			memcpy(edicts, INDEXENT(0), sizeof(edict_t) * MAX_EDICTS);
			memcpy(frame.playerinfos, g_demoplayers, gpGlobals->maxClients*sizeof(DemoPlayerEnt));
			memcpy(frame.netmessages, g_netmessages, g_netmessage_count*sizeof(NetMessageData));
			memcpy(frame.cmds, g_cmds, g_command_count*sizeof(CommandData));
			frame.netmessage_count = g_netmessage_count;
			frame.cmds_count = g_command_count;
			frame.serverFrameCount = g_server_frame_count;

			// clear the main thread buffers
			g_netmessage_count = 0;
			g_command_count = 0;

			int copySz = (sizeof(edict_t) * MAX_EDICTS) + (gpGlobals->maxClients * sizeof(DemoPlayerEnt)) + (g_netmessage_count * sizeof(NetMessageData)) + (g_command_count * sizeof(CommandData));
			edictCopyState.setValue(EDICT_COPY_FINISHED);

			g_copyTime = getEpochMillis() - startMillis;
			//println("Copy %.1fKB in %lums", copySz / 1024.0f, copyTime);
		}
	}
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
					totalEnts--; totalEnts++; // set breakpoint here
				}
				//println("Write %d bytes for edict %d (%d total)", writeSz, i, totalBytes);
				//println("Write %d bytes for edict %s (%d total)", writeSz, STRING(edicts[i].v.classname), totalBytes);
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
	bool loadNewData = true;

	while (!threadShouldExit) {
		while (edictCopyState.getValue() != EDICT_COPY_FINISHED && !threadShouldExit) {
			if (demoWriter->isFileOpen() && !enableDemoFile) {
				demoWriter->closeDemoFile();
			}
			this_thread::sleep_for(chrono::milliseconds(1));
		}
		uint64_t startMillis = getEpochMillis();

		if (loadNewData) {
			for (int i = 0; i < MAX_EDICTS; i++) {
				frame.netedicts[i].load(edicts[i]);
			}
			for (int i = 1; i <= gpGlobals->maxClients; i++) {
				DemoPlayerEnt& dplr = frame.playerinfos[i - 1];
				edict_t* ent = INDEXENT(i);

				if (!isValidPlayer(ent)) {
					continue;
				}

				dplr.armorvalue = clamp(ent->v.armorvalue + 0.5f, 0, UINT16_MAX);
				dplr.button = ent->v.button & 0xffff;
				dplr.fov = clamp(ent->v.fov + 0.5f, 0, 255);
				dplr.frags = clamp(ent->v.frags, 0, UINT16_MAX);
				dplr.punchangle[0] = clamp(ent->v.punchangle[0] * 8, INT16_MIN, INT16_MAX);
				dplr.punchangle[1] = clamp(ent->v.punchangle[1] * 8, INT16_MIN, INT16_MAX);
				dplr.punchangle[2] = clamp(ent->v.punchangle[2] * 8, INT16_MIN, INT16_MAX);
				dplr.viewmodel = ent->v.viewmodel;
				dplr.weaponmodel = ent->v.weaponmodel;
				dplr.weaponanim = ent->v.weaponanim;
				dplr.view_ofs = clamp(ent->v.view_ofs[2] * 16, INT16_MIN, INT16_MAX);
				dplr.observer = ((uint8_t)ent->v.iuser2 << 6) | ((ent->v.iuser1 & 0x3) << 1) | (ent->v.deadflag != DEAD_NO);

				int fl = ent->v.flags;
				dplr.flags = (fl & FL_INWATER ? PLR_FL_INWATER : 0)
					| (fl & FL_NOTARGET ? PLR_FL_NOTARGET : 0)
					| (fl & (FL_ONGROUND|FL_PARTIALGROUND) ? PLR_FL_ONGROUND : 0)
					| (fl & FL_WATERJUMP ? PLR_FL_WATERJUMP : 0)
					| (fl & FL_FROZEN ? PLR_FL_FROZEN : 0)
					| (fl & FL_DUCKING ? PLR_FL_DUCKING : 0)
					| (fl & FL_NOWEAPONS ? PLR_FL_NOWEAPONS : 0);
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
	}

	demoWriter->closeDemoFile();

	delete socket;
}
