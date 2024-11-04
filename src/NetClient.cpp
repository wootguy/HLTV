#include "NetClient.h"
#include "mstream.h"
#include "SvenTV.h"

NetClient::NetClient() {
	isFree = true;
	baselines = new netedict[MAX_EDICTS];
}

void NetClient::init(IPV4 addr) {
	this->addr = addr;
	isFree = false;
	nextUpdateTime = 0;
	lastPacketTime = getEpochMillis();
	baselineId = 0;
	sentDeltas.clear();
	sentBytesHistory.clear();
	memset(baselines, 0, sizeof(netedict) * MAX_EDICTS);
}

int NetClient::getBytesSentPerSecond() {
	uint64_t now = getEpochMillis();

	// delete data points for packets sent longer than a second ago
	while (!sentBytesHistory.empty() && TimeDifference(sentBytesHistory.front().time, now) > 1.0f) {
		sentBytesHistory.pop_front();
	}

	int totalBytes = 0;
	for (auto& itr : sentBytesHistory) {
		totalBytes += itr.bytes;
	}

	return totalBytes;
}

int NetClient::applyDeltaToBaseline(Packet& packet, bool debugMode) {
	mstream reader(packet.data, packet.sz);

	uint8_t packetType;
	uint8_t offset;
	uint16_t fullIndex = 0;
	uint16_t updateId; // ID of the global update which this packet belongs
	uint16_t baselineId; // ID of the update we should be delta-ing from
	uint16_t fragmentId; // ID of this packet within the update, unique only to this update

	reader.read(&packetType, 1);
	reader.read(&updateId, 2);
	reader.read(&baselineId, 2);
	reader.read(&fragmentId, 2);

	if (debugMode)
		ALERT(at_console, "Apply delta %d frag %d\n", updateId, fragmentId);

	int loop = -1;
	int lastSuccessEdict = 0;
	while (1) {
		loop++;
		reader.read(&offset, 1);

		if (reader.eom()) {
			break;
		}

		if (offset == 0) {
			reader.read(&fullIndex, 2);
		}
		else {
			fullIndex += offset;
		}

		if (fullIndex >= MAX_EDICTS) {
			ALERT(at_console, "ERROR: Invalid delta packet wants to update edict %d at %d\n", (int)fullIndex, loop);
			ALERT(at_console, "TODO: rollback changes made so far, because now client and server are desynced\n");
			return lastSuccessEdict;
		}

		uint64_t startPos = reader.tell();

		netedict& ed = baselines[fullIndex];
		if (!ed.readDeltas(reader)) {
			//ALERT(at_console, "Skip free %d\n", fullIndex);
			continue;
		}

		if (debugMode)
			ALERT(at_console, "Read index %d (%d bytes)\n", (int)fullIndex, (int)(reader.tell() - startPos));

		if (reader.eom()) {
			ALERT(at_console, "ERROR: Invalid delta hit unexpected eom at %d\n", loop);
			ALERT(at_console, "TODO: rollback changes made so far, because now client and server are desynced\n");
			return lastSuccessEdict;
		}

		lastSuccessEdict = fullIndex;
	}

	return -1;
}
