#pragma once
#include "DemoFile.h"
#include "netedict.h"
#include "DemoStream.h"

struct DemoDataTest;

class DemoWriter {
public:
	int showStats = 0;
	std::string fpath;

	DemoWriter();
	~DemoWriter();

	DemoStream stream;

	// opens the demo file (if wanted) and starts a new sequence of frames
	void beginSession(bool wantFile, bool wantStream);

	// encodes one frame and sends the same bytes to each wanted output.
	// return true if more data requested
	bool writeDemoFile(FrameData& frame, bool wantFile, bool wantStream);

	bool shouldWriteDemoFrame();

	void closeDemoFile();

	bool isFileOpen();

	// true if frames are going to a file or the live stream
	bool isActive();

	void compressDemo(std::string inPath, std::string outPath, int sleepMillis);

	// get millis since recording started
	uint32_t getRecordingTime();

private:
	void compressNetMessage(FrameData& frame, NetMessageData& msg);

	void openDemoFile();

	mstream writeEntDeltas(FrameData& frame, uint16_t& numEntDeltas, DemoDataTest* testData);
	mstream writeMsgDeltas(FrameData& frame, DemoDataTest* testData);
	mstream writeCmdDeltas(FrameData& frame);
	mstream writeEvtDeltas(FrameData& frame);
	mstream writeUserCmdDeltas(FrameData& frame);

	// Each player's latest ScoreInfo and TeamInfo, repeated in keyframes. Stream viewers start at a
	// keyframe, and they're only sent when something changes, so without this anyone who joined
	// later wouldn't know the players' team colors.
	void rememberTeamMessages(FrameData& frame);
	void repeatTeamMessages(FrameData& frame);
	std::vector<NetMessageData> lastTeamMsgs; // [player * 2 + (0 = ScoreInfo, 1 = TeamInfo)]
	bool hasTeamMsg[33 * 2] = { false };

	// vars for writing a demo file
	uint64_t nextDemoUpdate = 0;
	float demoFileFps = 60;
	FILE* demoFile = NULL;
	uint64_t nextDemoKeyframe = 0;
	uint32_t lastServerFrameCount = 0;
	uint64_t demoStartTime = 0;
	uint64_t lastDemoFrameTime = 0;
	uint32_t stringPoolOffset = 0; // location of the string pool in the file
	uint32_t lastStringPoolIdx = 0; // for detecting if the string pool needs updating in the file

	netedict* fileedicts = NULL; // last edicts written to file
	DemoUserCmdData usrstates[32];

	std::vector<uint8_t> frameOut; // the current frame, encoded
	bool warnedStreamDeferred = false;

	char* fileDeltaBuffer = NULL;
	int fileDeltaBufferSize = -1;

	char* netmessagesBuffer = NULL;
	int netmessagesBufferSize = -1;

	char* cmdsBuffer = NULL;
	int cmdsBufferSize = -1;

	char* eventsBuffer = NULL;
	int eventsBufferSize = -1;

	char* usercmdBuffer = NULL;
	int usercmdBufferSize = -1;
};