#include "rehlds.h"
#include "DemoFile.h"

void NetMessageData::send(int msg_dest, edict_t* targetEnt) {
	float forigin[3];

	if ((msg_dest == MSG_ONE || msg_dest == MSG_ONE_UNRELIABLE) && !targetEnt) {
		ALERT(at_console, "Dropped message %s mode %d (no target entity given)\n",
			msgTypeStr(type), msg_dest);
		return;
	}

	if (type == SVC_SPAWNSTATICSOUND && sz != 14) {
		// TODO: why is this happening
		ALERT(at_console, "Dropped static sound message with bad size %d\n", (int)sz);
		DEBUG_MSG(at_console, "Payload: ");
		for (int i = 0; i < sz; i++) {
			DEBUG_MSG(at_console, "%02x ", (int)data[i]);
		}
		DEBUG_MSG(at_console, "\n");
		return;
	}

	int expectedSz = 0;
	if (GetUserMsgInfo(type, &expectedSz) && expectedSz != -1 && sz != expectedSz) {
		ALERT(at_console, "Dropped %s with bad size (%d != %d)\n", msgTypeStr(type), (int)sz, expectedSz);
		return;
	}

	if (sz >= 512 - 3 ) {
		if (g_RehldsApi) {
			rehlds_SendBigMessage(msg_dest, type, data, sz, targetEnt ? ENTINDEX(targetEnt) : 0);
		}
		else {
			ALERT(at_console, "Dropped %d byte %s message (rehlds API not available)\n",
				sz, msgTypeStr(type));
		}

		return;
	}

	//ALERT(at_console, "SEND NET MSG %s sz %d\n", msgTypeStr(type), sz);

	if (hasOrigin) {
		if (hasLongOrigin) {
			forigin[0] = FIXED_TO_FLOAT(origin[0], 19, 5);
			forigin[1] = FIXED_TO_FLOAT(origin[1], 19, 5);
			forigin[2] = FIXED_TO_FLOAT(origin[2], 19, 5);
		}
		else {
			forigin[0] = (int16_t)origin[0];
			forigin[1] = (int16_t)origin[1];
			forigin[2] = (int16_t)origin[2];
		}
	}

	const float* ori = hasOrigin ? forigin : NULL;

	MESSAGE_BEGIN(msg_dest, type, ori, targetEnt);

	int numLongs = sz / 4;
	int numBytes = sz % 4;

	for (int i = 0; i < numLongs; i++) {
		WRITE_LONG(((uint32_t*)data)[i]);
	}

	int byteOffset = numLongs * 4;
	for (int i = byteOffset; i < byteOffset + numBytes; i++) {
		WRITE_BYTE(data[i]);
	}

	MESSAGE_END();

	//const char* oriStr = header.hasOrigin ? UTIL_VarArgs("Vector(%f %f %f)", forigin[0], forigin[1], forigin[2]) : "NULL";
	//const char* entStr = targetEnt ? STRING(targetEnt->v.netname) : "NULL";
	//ALERT(at_console, "SEND(%s, %s, %s, %d);", msgDestStr(msg_dest), msgTypeStr(header.type), 
	//	oriStr, targetEnt ? ENTINDEX(targetEnt) : 0);
}

void NetMessageData::compressCoords(int offset, int count, bool keepFrac) {
	if (count > 12) {
		ALERT(at_console, "Can't compress 13+ coords!\n", 0);
		return;
	}
	int32_t* oldorigin = (int32_t*)(data + offset);
	static int16_t neworigin[12];

	for (int i = 0; i < count; i++) {
		neworigin[i] = keepFrac ? oldorigin[i] : oldorigin[i] / 8;
	}

	int newOriginSz = sizeof(int16_t) * count;
	int oldOriginSz = sizeof(int32_t) * count;
	int moveSz = sz - (offset + oldOriginSz);
	byte* originPtr = (byte*)oldorigin;
	memcpy(originPtr, neworigin, newOriginSz);
	memmove(originPtr + newOriginSz, originPtr + oldOriginSz, moveSz);

	sz -= oldOriginSz - newOriginSz;
}

void NetMessageData::decompressCoords(int offset, int count, bool keepFrac) {
	if (count > 12) {
		ALERT(at_console, "Can't decompress 13+ coords!\n", 0);
		return;
	}
	int16_t* oldorigin = (int16_t*)(data + offset);
	static int32_t neworigin[12];
	for (int i = 0; i < count; i++) {
		neworigin[i] = keepFrac ? (int32_t)oldorigin[i] : (int32_t)oldorigin[i] * 8;
	}

	int newOriginSz = sizeof(int32_t) * count;
	int oldOriginSz = sizeof(int16_t) * count;
	int moveSz = sz - (offset + oldOriginSz);
	byte* originPtr = (byte*)oldorigin;
	memmove(originPtr + newOriginSz, originPtr + oldOriginSz, moveSz);
	memcpy(originPtr, neworigin, newOriginSz);

	sz += newOriginSz - oldOriginSz;
}