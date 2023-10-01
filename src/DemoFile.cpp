#include "DemoFile.h"
#include "NetMessage.h"

void NetMessageData::send(int msg_dest, edict_t* targetEnt) {
	float forigin[3];

	if (header.hasOrigin) {
		if (header.hasLongOrigin) {
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

	const float* ori = header.hasOrigin ? forigin : NULL;

	MESSAGE_BEGIN(msg_dest, header.type, ori, targetEnt);

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

	const char* oriStr = header.hasOrigin ? UTIL_VarArgs("Vector(%f %f %f)", forigin[0], forigin[1], forigin[2]) : "NULL";
	const char* entStr = targetEnt ? STRING(targetEnt->v.netname) : "NULL";
	//println("SEND(%s, %s, %s, %d);", msgDestStr(msg_dest), msgTypeStr(header.type), 
	//	oriStr, targetEnt ? ENTINDEX(targetEnt) : 0);
}
