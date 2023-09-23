#include "DemoWriter.h"
#include "main.h"

using namespace std;

DemoWriter::DemoWriter() {
	fileplayerinfos = new DemoPlayerEnt[32];
	fileedicts = new netedict[MAX_EDICTS];

	memset(fileedicts, 0, MAX_EDICTS * sizeof(netedict));
	memset(fileplayerinfos, 0, 32 * sizeof(DemoPlayerEnt));

	// size of full delta on every edict + byte for each index delta + 2 bytes for each delta bits on edict
	int fullDeltaMaxSize = sizeof(netedict) + 4;
	int indexBytes = 1; // 2 for full index writes but on a max size update there will be no 255+ edict gaps
	int headerBytes = 1; // 1 for first full index
	fileDeltaBufferSize = headerBytes + (fullDeltaMaxSize + indexBytes) * MAX_EDICTS;
	fileDeltaBuffer = new char[fileDeltaBufferSize];

	// size of full delta on every player info
	filePlayerInfoBufferSize = sizeof(DemoPlayerEnt) * 32 + 2;
	filePlayerInfoBuffer = new char[filePlayerInfoBufferSize];

	netmessagesBufferSize = sizeof(NetMessageData) * MAX_NETMSG_FRAME + 2;
	netmessagesBuffer = new char[netmessagesBufferSize];

	cmdsBufferSize = sizeof(CommandData) * MAX_CMD_FRAME + 2;
	cmdsBuffer = new char[cmdsBufferSize];

	eventsBufferSize = sizeof(DemoEventData) * MAX_EVENT_FRAME + 2;
	eventsBuffer = new char[eventsBufferSize];
}

DemoWriter::~DemoWriter() {
	delete[] fileedicts;
	delete[] fileDeltaBuffer;
	delete[] fileplayerinfos;
	delete[] netmessagesBuffer;
	delete[] cmdsBuffer;
	delete[] eventsBuffer;
}

void DemoWriter::initDemoFile() {
	time_t rawtime;
	struct tm* timeinfo;
	char buffer[80];

	time(&rawtime);
	timeinfo = localtime(&rawtime);

	strftime(buffer, sizeof(buffer), "%Y-%m-%d_%H-%M-%S", timeinfo);
	std::string fname(buffer);
	//fname = fname + "_" + STRING(gpGlobals->mapname) + ".demo";
	fname = string(STRING(gpGlobals->mapname)) + ".demo";
	string fpath = g_demo_file_path->string + fname;
	println("Open demo file: %s", fpath.c_str());

	demoFile = fopen(fpath.c_str(), "wb");
	memset(fileedicts, 0, MAX_EDICTS * sizeof(netedict));
	memset(fileplayerinfos, 0, 32 * sizeof(DemoPlayerEnt));

	fileDeltaBuffer = new char[fileDeltaBufferSize];

	uint64_t now = getEpochMillis();

	DemoHeader header;
	strncpy(header.mapname, STRING(gpGlobals->mapname), 64);
	header.maxPlayers = gpGlobals->maxClients;
	header.startTime = now;
	header.endTime = 0; // will indicate server crash if stays 0
	header.version = DEMO_VERSION;

	nextDemoKeyframe = 0;
	demoStartTime = now;
	lastDemoFrameTime = now;

	string modelData;
	int maxModelIdx = 0;
	int minModelIdx = 0;
	for (pair<int, string> item : g_indexToModel) {
		if (item.first > maxModelIdx) {
			maxModelIdx = item.first;
		}
	}
	for (int i = 0; i <= maxModelIdx; i++) {
		if (g_indexToModel[i].empty() || g_indexToModel[i].size() > 1 && g_indexToModel[i][0] == '*') {
			minModelIdx = i + 1;
			continue;
		}
		modelData += g_indexToModel[i] + "\n";
	}

	string soundData;
	for (int i = 0; i < g_SoundCacheFiles.size(); i++) {
		soundData += g_SoundCacheFiles[i] + "\n";
	}

	header.modelIdxStart = minModelIdx;
	header.modelLen = modelData.size();
	header.soundLen = soundData.size();

	memset(&g_stats, 0, sizeof(DemoStats));
	g_stats.totalWriteSz = sizeof(DemoHeader) + header.modelLen + header.soundLen;

	fwrite(&header, sizeof(DemoHeader), 1, demoFile);

	if (modelData.size())
		fwrite(modelData.c_str(), modelData.size(), 1, demoFile);
	if (soundData.size())
		fwrite(soundData.c_str(), soundData.size(), 1, demoFile);
}

bool DemoWriter::writeDemoFile(FrameData& frame) {
	if (!demoFile) {
		initDemoFile();
	}

	uint64_t now = getEpochMillis();
	if (now < nextDemoUpdate) {
		return false;
	}
	uint64_t updateDelay = (1.0f / demoFileFps) * 1000;
	nextDemoUpdate = now + updateDelay;
	uint32_t frameTimeDelta = now - lastDemoFrameTime;
	lastDemoFrameTime = now;

	bool isKeyframe = now >= nextDemoKeyframe;

	if (isKeyframe) {
		nextDemoKeyframe = now + (1000ULL * KEYFRAME_INTERVAL);
		memset(fileedicts, 0, MAX_EDICTS * sizeof(netedict));
		memset(fileplayerinfos, 0, 32 * sizeof(DemoPlayerEnt));
	}

	mstream entbuffer(fileDeltaBuffer, fileDeltaBufferSize);

	uint8_t offset = 0; // always write full index for first entity delta

	uint16_t numEntDeltas = 0;
	uint32_t indexWriteSz = 0;
	for (uint16_t i = 0; i < MAX_EDICTS; i++) {
		netedict& now = frame.netedicts[i];

		uint64_t startOffset = entbuffer.tell();

		entbuffer.write(&offset, 1); // entity index the delta is for (offset from previous)

		if (offset == 0) {
			// last edict was 256+ slots away. Write full index.
			entbuffer.write(&i, 2);
		}

		int ret = now.writeDeltas(entbuffer, fileedicts[i]);

		if (ret == EDELTA_OVERFLOW) {
			println("ERROR: Demo file entity delta buffer overflowed. Use a bigger buffer! The demo file is now broken");
			break;
		}
		else if (ret == EDELTA_NONE) {
			// no differences
			entbuffer.seek(startOffset); // undo index write
			if (offset != 0) {
				offset += 1;
			}
		}
		else {
			// delta written
			//println("Write edict %d offset %d bytes %d", i, (int)offset, (int)(entbuffer.tell() - startOffset));
			indexWriteSz += offset == 0 ? 3 : 1;
			offset = 1;
			numEntDeltas++;
		}
	}

	int numPlayerDeltas = 0;
	uint32_t plrDeltaBits = 0;
	mstream plrbuffer(filePlayerInfoBuffer, filePlayerInfoBufferSize);
	for (int i = 0; i < gpGlobals->maxClients; i++) {
		int ret = frame.playerinfos[i].writeDeltas(plrbuffer, fileplayerinfos[i]);

		if (ret == EDELTA_OVERFLOW) {
			println("ERROR: Demo file player delta buffer overflowed. Use a bigger buffer! The demo file is now broken");
			break;
		}
		else if (ret == EDELTA_NONE) {
			// no differences
		}
		else {
			// delta written
			offset = 1;
			plrDeltaBits |= 1 << i;
			numPlayerDeltas++;
		}
	}

	mstream msgbuffer(netmessagesBuffer, netmessagesBufferSize);
	for (int i = 0; i < frame.netmessage_count; i++) {
		NetMessageData& dat = frame.netmessages[i];
		msgbuffer.write(&dat.header, sizeof(DemoNetMessage));

		g_stats.msgSz[dat.header.type] += dat.header.sz;
		if (dat.header.type == SVC_TEMPENTITY) {
			g_stats.msgSz[256 + dat.data[0]] += dat.header.sz;
		}

		if (dat.header.hasOrigin) {
			msgbuffer.write(&dat.origin[0], 3);
			msgbuffer.write(&dat.origin[1], 3);
			msgbuffer.write(&dat.origin[2], 3);
		}
		if (dat.header.hasEdict) {
			msgbuffer.write(&dat.eidx, sizeof(uint16_t));
		}
		msgbuffer.write(dat.data, dat.header.sz);
	}
	if (msgbuffer.eom()) {
		println("ERROR: Demo file network message buffer overflowed (%d > %d). Use a bigger buffer!", frame.netmessage_count, MAX_NETMSG_FRAME);
	}

	mstream cmdbuffer(cmdsBuffer, cmdsBufferSize);
	for (int i = 0; i < frame.cmds_count; i++) {
		CommandData& dat = frame.cmds[i];
		DemoCommand cmd;

		cmd.idx = dat.idx;
		cmd.len = dat.len;
		cmdbuffer.write(&cmd, sizeof(DemoCommand));
		cmdbuffer.write(dat.data, dat.len);
	}
	if (cmdbuffer.eom()) {
		println("ERROR: Demo file command buffer overflowed. Use a bigger buffer!");
	}

	mstream evbuffer(eventsBuffer, eventsBufferSize);
	for (int i = 0; i < frame.event_count; i++) {
		DemoEventData& dat = frame.events[i];
		evbuffer.write(&dat.header, sizeof(DemoEvent));

		if (dat.header.hasOrigin) {
			evbuffer.write(&dat.origin[0], 3);
			evbuffer.write(&dat.origin[1], 3);
			evbuffer.write(&dat.origin[2], 3);
		}
		if (dat.header.hasAngles) {
			evbuffer.write(&dat.angles[0], 2);
			evbuffer.write(&dat.angles[1], 2);
			evbuffer.write(&dat.angles[2], 2);
		}
		if (dat.header.hasFparam1) {
			evbuffer.write(&dat.fparam1, 3);
		}
		if (dat.header.hasFparam2) {
			evbuffer.write(&dat.fparam2, 3);
		}
		if (dat.header.hasIparam1) {
			evbuffer.write(&dat.iparam1, 2);
		}
		if (dat.header.hasIparam2) {
			evbuffer.write(&dat.iparam2, 2);
		}
	}
	if (evbuffer.eom()) {
		println("ERROR: Demo file event buffer overflowed (%d > %d). Use a bigger buffer!", frame.event_count, MAX_EVENT_FRAME);
	}

	memcpy(fileedicts, frame.netedicts, MAX_EDICTS * sizeof(netedict));
	memcpy(fileplayerinfos, frame.playerinfos, 32 * sizeof(DemoPlayerEnt));

	g_stats.entDeltaCurrentSz = entbuffer.tell() + (entbuffer.tell() ? 2 : 0);
	g_stats.plrDeltaCurrentSz = plrbuffer.tell() + (plrbuffer.tell() ? 4 : 0);
	g_stats.msgCurrentSz = msgbuffer.tell() + (msgbuffer.tell() ? 2 : 0);
	g_stats.cmdCurrentSz = cmdbuffer.tell() + (cmdbuffer.tell() ? 1 : 0);
	g_stats.eventCurrentSz = evbuffer.tell() + (evbuffer.tell() ? 1 : 0);
	g_stats.msgCount += frame.netmessage_count;
	g_stats.cmdCount += frame.cmds_count;
	g_stats.eventCount += frame.event_count;
	g_stats.entIndexTotalSz += indexWriteSz;
	g_stats.calcFrameSize();

	uint8_t frameCountDelta = clamp(frame.serverFrameCount - lastServerFrameCount, 0, 255);
	lastServerFrameCount = frame.serverFrameCount;

	DemoFrame header;
	header.deltaFrames = frameCountDelta;
	header.hasEntityDeltas = numEntDeltas > 0;
	header.hasNetworkMessages = frame.netmessage_count > 0;
	header.hasEvents = frame.event_count > 0;
	header.hasPlayerDeltas = plrDeltaBits > 0;
	header.hasCommands = frame.cmds_count > 0;
	header.isKeyFrame = isKeyframe;
	header.isBigFrame = frameTimeDelta > 255 || g_stats.currentWriteSz > (65535-3) || isKeyframe;

	fwrite(&header, sizeof(DemoFrame), 1, demoFile);

	if (header.isBigFrame) {
		g_stats.currentWriteSz += sizeof(uint32_t) * 2;
		uint32_t demoTime = now - demoStartTime;
		uint32_t frameSize = g_stats.currentWriteSz;
		fwrite(&demoTime, sizeof(uint32_t), 1, demoFile);
		fwrite(&frameSize, sizeof(uint32_t), 1, demoFile);
		g_stats.bigFrameCount++;
	}
	else {
		g_stats.currentWriteSz += sizeof(uint16_t) + sizeof(uint8_t);
		uint8_t demoTimeDelta = frameTimeDelta;
		uint16_t frameSize = g_stats.currentWriteSz;
		fwrite(&demoTimeDelta, sizeof(uint8_t), 1, demoFile);
		fwrite(&frameSize, sizeof(uint16_t), 1, demoFile);
	}

	g_stats.incTotals();

	if (header.hasEntityDeltas) {
		fwrite(&numEntDeltas, sizeof(uint16_t), 1, demoFile);
		fwrite(entbuffer.getBuffer(), entbuffer.tell(), 1, demoFile);
	}
	if (header.hasPlayerDeltas) {
		fwrite(&plrDeltaBits, sizeof(uint32_t), 1, demoFile);
		fwrite(plrbuffer.getBuffer(), plrbuffer.tell(), 1, demoFile);
	}
	if (header.hasNetworkMessages) {
		fwrite(&frame.netmessage_count, sizeof(uint16_t), 1, demoFile);
		fwrite(msgbuffer.getBuffer(), msgbuffer.tell(), 1, demoFile);
	}
	if (header.hasEvents) {
		fwrite(&frame.event_count, sizeof(uint8_t), 1, demoFile);
		fwrite(evbuffer.getBuffer(), evbuffer.tell(), 1, demoFile);
	}
	if (header.hasCommands) {
		fwrite(&frame.cmds_count, sizeof(uint8_t), 1, demoFile);
		fwrite(cmdbuffer.getBuffer(), cmdbuffer.tell(), 1, demoFile);
	}

	bool showstats = false;
	if (showstats) {
		int actualSz = ftell(demoFile);
		if (actualSz != g_stats.totalWriteSz) {
			println("%d != %d", (int)g_stats.totalWriteSz, actualSz);
		}
		println("%4de %2dp %4dm %4dc    |  %4d + %4d + %4d + %4d = %4d B  |  %.1f MB file  |  %dms copy, %dms think",
			numEntDeltas, numPlayerDeltas, frame.netmessage_count, frame.cmds_count,
			g_stats.entDeltaCurrentSz, g_stats.plrDeltaCurrentSz, g_stats.msgCurrentSz, g_stats.cmdCurrentSz, g_stats.currentWriteSz,
			(float)((double)g_stats.totalWriteSz / (1024.0 * 1024.0)),
			g_copyTime, g_thinkTime);
	}

	return true;
}

void DemoWriter::closeDemoFile() {
	if (!demoFile) {
		return;
	}
	println("Close demo file");

	uint64_t endTime = getEpochMillis();
	fseek(demoFile, offsetof(DemoHeader, endTime), SEEK_SET);
	fwrite(&lastDemoFrameTime, sizeof(uint64_t), 1, demoFile);

	fclose(demoFile);
	demoFile = NULL;
}

bool DemoWriter::isFileOpen() {
	return demoFile != NULL;
}

bool DemoWriter::validateEdicts() {
	for (int i = 0; i < MAX_EDICTS; i++) {
		if (fileedicts[i].edflags && !(fileedicts[i].edflags & EDFLAG_BEAM) && fileedicts[i].aiment > 8192) {
			println("Invalid edict %d has %d", i, (int)fileedicts[i].aiment);
			return false;
		}
	}
	return true;
}