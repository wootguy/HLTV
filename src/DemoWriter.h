#pragma once
#include "DemoFile.h"
#include "netedict.h"

struct DemoDataTest;

class DemoWriter {
public:
	int showStats = 0;
	std::string fpath;

	DemoWriter();
	~DemoWriter();

	void initDemoFile();

	// return true if more data requested
	bool writeDemoFile(FrameData& frame);

	bool shouldWriteDemoFrame();

	void closeDemoFile();

	bool isFileOpen();

	void compressDemo(std::string inPath, std::string outPath);

	// get millis since recording started
	uint32_t getRecordingTime();

private:
	std::thread* compress_thread = NULL;
	void compressNetMessage(FrameData& frame, NetMessageData& msg);

	mstream writeEntDeltas(FrameData& frame, uint16_t& numEntDeltas, DemoDataTest* testData);
	mstream writeMsgDeltas(FrameData& frame, DemoDataTest* testData);
	mstream writeCmdDeltas(FrameData& frame);
	mstream writeEvtDeltas(FrameData& frame);
	mstream writeUserCmdDeltas(FrameData& frame);

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