#include "DemoPlayerEnt.h"
#include "mmlib.h"
#include "DemoFile.h"

using namespace std;

#undef read
#undef write

int DemoPlayerEnt::writeDeltas(mstream& writer, const DemoPlayerEnt& old) {
	uint64_t startOffset = writer.tell();

	DemoPlayerDelta deltaBits; // flags which fields were changed
	memset(&deltaBits, 0, sizeof(DemoPlayerDelta));

	writer.skip(sizeof(DemoPlayerDelta)); // write delta bits later
	uint64_t deltaStartOffset = writer.tell();

	if (old.isConnected != isConnected) {
		deltaBits.isConnectedChanged = 1;
		writer.write((void*)&isConnected, 1);
	}
	if (isConnected) {
		if (!old.isConnected) {
			// new player. Start from a fresh state
			memset(this, 0, sizeof(DemoPlayerEnt));
		}

		if (strcmp(old.name, name) != 0) {
			deltaBits.nameChanged = 1;
			uint8_t len = strlen(name);
			writer.write((void*)&len, 1);
			writer.write((void*)&name, len);
		}
		if (strcmp(old.model, model) != 0) {
			deltaBits.modelChanged = 1;
			uint8_t len = strlen(model);
			writer.write((void*)&len, 1);
			writer.write((void*)&model, len);
		}
		if (old.steamid64 != steamid64) {
			deltaBits.steamIdChanged = 1;
			writer.write((void*)&steamid64, 8);
		}
		if (old.topColor != topColor || old.bottomColor != bottomColor) {
			deltaBits.colorsChanged = 1;
			writer.write((void*)&topColor, 1);
			writer.write((void*)&bottomColor, 1);
		}
		if (old.ping != ping) {
			deltaBits.pingChanged = 1;
			writer.write((void*)&ping, 2);
		}
		if (old.pmMoveCounter != pmMoveCounter) {
			deltaBits.pmMoveChanged = 1;
			uint8_t delta = clamp(pmMoveCounter - old.pmMoveCounter, 0, 255);
			writer.write((void*)&delta, 1);
		}
		if (old.flags != flags) {
			deltaBits.flagsChanged = 1;
			writer.write((void*)&flags, 1);
		}
		if (old.punchangle[0] != punchangle[0]) {
			deltaBits.punchAngleXChanged = 1;
			writer.write((void*)&punchangle[0], 2);
		}
		if (old.punchangle[1] != punchangle[1]) {
			deltaBits.punchAngleYChanged = 1;
			writer.write((void*)&punchangle[1], 2);
		}
		if (old.punchangle[2] != punchangle[2]) {
			deltaBits.punchAngleZChanged = 1;
			writer.write((void*)&punchangle[2], 2);
		}
		if (old.viewmodel != viewmodel) {
			deltaBits.viewmodelChanged = 1;
			writer.write((void*)&viewmodel, 2);
		}
		if (old.weaponmodel != weaponmodel) {
			deltaBits.weaponmodelChanged = 1;
			writer.write((void*)&weaponmodel, 2);
		}
		if (old.weaponanim != weaponanim) {
			deltaBits.weaponanimChanged = 1;
			writer.write((void*)&weaponanim, 2);
		}
		if (old.armorvalue != armorvalue) {
			deltaBits.armorvalueChanged = 1;
			writer.write((void*)&armorvalue, 2);
		}
		if (old.button != button) {
			deltaBits.buttonChanged = 1;
			writer.write((void*)&button, 2);
		}
		if (old.view_ofs != view_ofs) {
			deltaBits.view_ofsChanged = 1;
			writer.write((void*)&view_ofs, 2);
		}
		if (old.frags != frags) {
			deltaBits.fragsChanged = 1;
			writer.write((void*)&frags, 2);
		}
		if (old.fov != fov) {
			deltaBits.fovChanged = 1;
			writer.write((void*)&fov, 1);
		}
		if (old.clip != clip) {
			deltaBits.clipChanged = 1;
			writer.write((void*)&clip, 2);
		}
		if (old.clip2 != clip2) {
			deltaBits.clip2Changed = 1;
			writer.write((void*)&clip2, 2);
		}
		if (old.ammo != ammo) {
			deltaBits.ammoChanged = 1;
			writer.write((void*)&ammo, 2);
		}
		if (old.ammo2 != ammo2) {
			deltaBits.ammo2Changed = 1;
			writer.write((void*)&ammo2, 2);
		}
		if (old.observer != observer) {
			deltaBits.observerChanged = 1;
			writer.write((void*)&observer, 1);
		}
	}

	if (writer.eom()) {
		writer.seek(startOffset);
		return EDELTA_OVERFLOW;
	}

	uint64_t currentOffset = writer.tell();
	writer.seek(startOffset);

	if (currentOffset == deltaStartOffset) {
		return EDELTA_NONE;
	}

	writer.write((void*)&deltaBits, sizeof(DemoPlayerDelta));
	writer.seek(currentOffset);

	return EDELTA_WRITE;
}

int DemoPlayerEnt::readDeltas(mstream& reader) {
	DemoPlayerDelta deltaBits;
	reader.read(&deltaBits, sizeof(DemoPlayerDelta));
	static char strBuffer[256];

	bool oldConnected = isConnected;
	bool newConnected = isConnected;

	if (deltaBits.isConnectedChanged) {
		reader.read(&newConnected, 1);
	}

	if (!oldConnected && newConnected) {
		// new player joined. Start from a fresh state.
		memset(this, 0, sizeof(DemoPlayerEnt));
	}
	isConnected = newConnected;

	if (!isConnected) {
		return 0;
	}

	if (deltaBits.nameChanged) {
		uint8_t len;
		reader.read(&len, 1);
		if (len > 31) {
			println("Invalid name length %d", (int)len);
			len = 31;
		}
		reader.read(strBuffer, len);
		strBuffer[len] = '\0';
		memcpy(name, strBuffer, len + 1);
	}
	if (deltaBits.modelChanged) {
		uint8_t len;
		reader.read(&len, 1);
		if (len > 22) {
			println("Invalid name length %d", (int)len);
			len = 22;
		}
		reader.read(strBuffer, len);
		strBuffer[len] = '\0';
		memcpy(model, strBuffer, len + 1);
	}
	if (deltaBits.steamIdChanged) {
		reader.read(&steamid64, 8);
	}
	if (deltaBits.colorsChanged) {
		reader.read(&topColor, 1);
		reader.read(&bottomColor, 1);
	}
	if (deltaBits.pingChanged) {
		reader.read(&ping, 2);
	}
	if (deltaBits.pmMoveChanged) {
		reader.read(&pmMoveCounter, 1);
	}
	if (deltaBits.flagsChanged) {
		reader.read(&flags, 1);
	}
	if (deltaBits.punchAngleXChanged) {
		reader.read(&punchangle[0], 2);
	}
	if (deltaBits.punchAngleYChanged) {
		reader.read(&punchangle[1], 2);
	}
	if (deltaBits.punchAngleZChanged) {
		reader.read(&punchangle[2], 2);
	}
	if (deltaBits.viewmodelChanged) {
		reader.read(&viewmodel, 2);
	}
	if (deltaBits.weaponmodelChanged) {
		reader.read(&weaponmodel, 2);
	}
	if (deltaBits.weaponanimChanged) {
		reader.read(&weaponanim, 2);
	}
	if (deltaBits.armorvalueChanged) {
		reader.read(&armorvalue, 2);
	}
	if (deltaBits.buttonChanged) {
		reader.read(&button, 2);
	}
	if (deltaBits.view_ofsChanged) {
		reader.read(&view_ofs, 2);
	}
	if (deltaBits.fragsChanged) {
		reader.read(&frags, 2);
	}
	if (deltaBits.fovChanged) {
		reader.read(&fov, 1);
	}
	if (deltaBits.clipChanged) {
		reader.read(&clip, 2);
	}
	if (deltaBits.clip2Changed) {
		reader.read(&clip2, 2);
	}
	if (deltaBits.ammoChanged) {
		reader.read(&ammo, 2);
	}
	if (deltaBits.ammo2Changed) {
		reader.read(&ammo2, 2);
	}
	if (deltaBits.observerChanged) {
		reader.read(&observer, 1);
	}

	return 0;
}