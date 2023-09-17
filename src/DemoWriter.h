#pragma once
#include "mmlib.h"
#include "DemoFile.h"
#include "netedict.h"

class DemoWriter {
public:
	DemoWriter();
	~DemoWriter();

	void initDemoFile();

	// return true if more data requested
	bool writeDemoFile(FrameData& frame);

	void closeDemoFile();

	bool isFileOpen();

	bool validateEdicts(); // debug

private:
	// vars for writing a demo file
	uint64_t nextDemoUpdate = 0;
	float demoFileFps = 60;
	FILE* demoFile = NULL;
	int numFileDeltas = 0;
	uint64_t deltaWriteSz = 0;
	uint64_t netMsgWriteSz = 0;
	uint64_t nextDemoKeyframe = 0;
	uint32_t lastServerFrameCount = 0;
	uint64_t demoStartTime = 0;
	uint64_t lastDemoFrameTime = 0;

	DemoPlayerEnt* fileplayerinfos = NULL;
	char* filePlayerInfoBuffer = NULL;
	int filePlayerInfoBufferSize = -1;

	netedict* fileedicts = NULL; // last edicts written to file
	char* fileDeltaBuffer = NULL;
	int fileDeltaBufferSize = -1;

	char* netmessagesBuffer = NULL;
	int netmessagesBufferSize = -1;

	char* cmdsBuffer = NULL;
	int cmdsBufferSize = -1;
};