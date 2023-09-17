#include "DemoWriter.h"
#include "main.h"

using namespace std;

DemoWriter::DemoWriter() {
	fileplayerinfos = new DemoPlayerEnt[32];
	fileedicts = new netedict[MAX_EDICTS];

	memset(fileplayerinfos, 0, 32 * sizeof(DemoPlayerEnt));

	// size of full delta on every edict + byte for each index delta + 2 bytes for each delta bits on edict
	int fullDeltaMaxSize = sizeof(netedict) + 4;
	int indexBytes = 1; // 2 for full index writes but on a max size update there will be no 255+ edict gaps
	int headerBytes = 1; // 1 for first full index
	fileDeltaBufferSize = headerBytes + (fullDeltaMaxSize + indexBytes) * MAX_EDICTS;
	fileDeltaBuffer = new char[fileDeltaBufferSize];

	// size of full delta on every player info
	filePlayerInfoBufferSize = (sizeof(DemoPlayerEnt) + 2) * 32;
	filePlayerInfoBuffer = new char[filePlayerInfoBufferSize];

	netmessagesBufferSize = sizeof(NetMessageData) * MAX_NETMSG_FRAME;
	netmessagesBuffer = new char[netmessagesBufferSize];

	cmdsBufferSize = sizeof(CommandData) * MAX_CMD_FRAME;
	cmdsBuffer = new char[cmdsBufferSize];
}

DemoWriter::~DemoWriter() {
	delete[] fileDeltaBuffer;
	delete[] fileplayerinfos;
	delete[] netmessagesBuffer;
	delete[] cmdsBuffer;
	delete[] fileedicts;
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
	numFileDeltas = 0;

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

		if (dat.header.hasOrigin) {
			msgbuffer.write(dat.origin, sizeof(float) * 3);
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

	memcpy(fileedicts, frame.netedicts, MAX_EDICTS * sizeof(netedict));
	memcpy(fileplayerinfos, frame.playerinfos, 32 * sizeof(DemoPlayerEnt));

	int entDeltaSz = entbuffer.tell() + (entbuffer.tell() ? 2 : 0);
	int plrDeltaSz = plrbuffer.tell() + (plrbuffer.tell() ? 4 : 0);
	int msgSz = msgbuffer.tell() + (msgbuffer.tell() ? 2 : 0);
	int cmdSz = cmdbuffer.tell() + (cmdbuffer.tell() ? 2 : 0);
	int totalSz = entDeltaSz + plrDeltaSz + msgSz + cmdSz + sizeof(DemoFrame);
	numFileDeltas++;
	deltaWriteSz += (uint64_t)totalSz;

	uint8_t frameCountDelta = clamp(frame.serverFrameCount - lastServerFrameCount, 0, 255);
	lastServerFrameCount = frame.serverFrameCount;

	DemoFrame header;
	header.deltaFrames = frameCountDelta;
	header.demoTime = now - demoStartTime;
	header.hasEntityDeltas = numEntDeltas > 0;
	header.hasNetworkMessages = frame.netmessage_count > 0;
	header.hasPlayerDeltas = plrDeltaBits > 0;
	header.hasCommands = frame.cmds_count > 0;
	header.isKeyFrame = isKeyframe;
	header.frameSize = totalSz;

	fwrite(&header, sizeof(DemoFrame), 1, demoFile);

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
	if (header.hasCommands) {
		fwrite(&frame.cmds_count, sizeof(uint16_t), 1, demoFile);
		fwrite(cmdbuffer.getBuffer(), cmdbuffer.tell(), 1, demoFile);
	}

	bool showstats = false;
	if (showstats) {
		println("%4de %2dp %4dm %4dc    |  %4d + %4d + %4d + %4d = %4d B  |  %.1f MB file  |  %dms copy, %dms think",
			numEntDeltas, numPlayerDeltas, frame.netmessage_count, frame.cmds_count,
			entDeltaSz, plrDeltaSz, msgSz, cmdSz, totalSz,
			(float)((double)deltaWriteSz / (1024.0 * 1024.0)),
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