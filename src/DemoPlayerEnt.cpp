#include "main.h"
#include "DemoPlayerEnt.h"
#include "DemoFile.h"

using namespace std;

#undef read
#undef write

#define READ_DELTA(reader, deltaBits, deltaFlag, field, sz) \
	if (deltaBits & deltaFlag) { \
		g_stats.plrDeltaSz[bitoffset(deltaFlag)] += sz; \
		reader.read((void*)&field, sz); \
	}

#define WRITE_DELTA(writer, deltaBits, deltaFlag, field, sz) \
	if (old.field != field) { \
		deltaBits |= deltaFlag; \
		g_stats.plrDeltaSz[bitoffset(deltaFlag)] += sz; \
		writer.write((void*)&field, sz); \
	}

#define READ_DELTA_STR(reader, deltaBits, deltaFlag, field) \
	if (deltaBits & deltaFlag) { \
		uint8_t len; \
		reader.read(&len, 1); \
		if (len >= sizeof(field)) { \
			println("Invalid " ###field " length %d", (int)len); \
			len = sizeof(field)-1; \
		} \
		reader.read(strBuffer, len); \
		strBuffer[len] = '\0'; \
		memcpy(field, strBuffer, len + 1); \
		g_stats.plrDeltaSz[bitoffset(deltaFlag)] += len+1; \
	}

#define WRITE_DELTA_STR(writer, deltaBits, deltaFlag, field) \
	if (strcmp(old.field, field)) { \
		deltaBits |= deltaFlag; \
		uint8_t len = strlen(field); \
		g_stats.plrDeltaSz[bitoffset(deltaFlag)] += len+1; \
		writer.write((void*)&len, 1); \
		writer.write((void*)field, len); \
	}

int DemoPlayerEnt::writeDeltas(mstream& writer, const DemoPlayerEnt& old) {
	uint64_t startOffset = writer.tell();

	uint32_t deltaBits = 0; // flags which fields were changed

	writer.skip(PLR_DELTA_BYTES); // write delta bits later
	uint64_t deltaStartOffset = writer.tell();

	WRITE_DELTA(writer, deltaBits, FL_DELTA_FLAGS, flags, 1);

	if (flags) {
		if (!old.flags) {
			// new player. Start from a fresh state
			memset((void*)(&old), 0, sizeof(DemoPlayerEnt));
		}

		WRITE_DELTA(writer, deltaBits, FL_DELTA_BUTTON, button, 2);
		WRITE_DELTA(writer, deltaBits, FL_DELTA_PING, ping, 2);
		WRITE_DELTA(writer, deltaBits, FL_DELTA_CLIP, clip, 2);
		if ((abs(old.punchangle[0]) < abs(punchangle[0])) || (punchangle[0] == 0 && old.punchangle[0] != 0)) {
			deltaBits |= FL_DELTA_PUNCHANGLE_X;
			g_stats.plrDeltaSz[bitoffset(FL_DELTA_PUNCHANGLE_X)] += 2;
			writer.write((void*)&punchangle[0], 2);
		}
		if ((abs(old.punchangle[1]) < abs(punchangle[1])) || (punchangle[01] == 0 && old.punchangle[1] != 0)) {
			deltaBits |= FL_DELTA_PUNCHANGLE_Y;
			g_stats.plrDeltaSz[bitoffset(FL_DELTA_PUNCHANGLE_Y)] += 2;
			writer.write((void*)&punchangle[1], 2);
		}
		if ((abs(old.punchangle[2]) < abs(punchangle[2])) || (punchangle[2] == 0 && old.punchangle[2] != 0)) {
			deltaBits |= FL_DELTA_PUNCHANGLE_Z;
			g_stats.plrDeltaSz[bitoffset(FL_DELTA_PUNCHANGLE_Z)] += 2;
			writer.write((void*)&punchangle[2], 2);
		}
		WRITE_DELTA_STR(writer, deltaBits, FL_DELTA_NAME, name);
		WRITE_DELTA_STR(writer, deltaBits, FL_DELTA_MODEL, model);
		WRITE_DELTA(writer, deltaBits, FL_DELTA_STEAMID, steamid64, 8);

		if (old.topColor != topColor || old.bottomColor != bottomColor) {
			deltaBits |= FL_DELTA_COLORS;
			writer.write((void*)&topColor, 1);
			writer.write((void*)&bottomColor, 1);
		}

		WRITE_DELTA(writer, deltaBits, FL_DELTA_VIEWMODEL, viewmodel, 2);
		WRITE_DELTA(writer, deltaBits, FL_DELTA_WEAPONMODEL, weaponmodel, 2);
		WRITE_DELTA(writer, deltaBits, FL_DELTA_WEAPONANIM, weaponanim, 1);
		WRITE_DELTA(writer, deltaBits, FL_DELTA_ARMORVALUE, armorvalue, 2);
		WRITE_DELTA(writer, deltaBits, FL_DELTA_VIEWOFS, view_ofs, 2);
		WRITE_DELTA(writer, deltaBits, FL_DELTA_FRAGS, frags, 2);
		WRITE_DELTA(writer, deltaBits, FL_DELTA_FOV, fov, 1);
		WRITE_DELTA(writer, deltaBits, FL_DELTA_CLIP2, clip2, 2);
		WRITE_DELTA(writer, deltaBits, FL_DELTA_AMMO, ammo, 2);
		WRITE_DELTA(writer, deltaBits, FL_DELTA_AMMO2, ammo2, 2);
		WRITE_DELTA(writer, deltaBits, FL_DELTA_OBSERVER, observer, 1);
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

	if ((deltaBits & 0xff) == deltaBits) {
		// delta bits can fit in a single byte. Move the deltas back a few bytes.
		int moveBytes = (currentOffset - PLR_DELTA_BYTES) - startOffset;
		memmove(writer.getBuffer() + startOffset + 1, writer.getBuffer() + startOffset + PLR_DELTA_BYTES, moveBytes);
		writer.write((void*)&deltaBits, 1);
		writer.seek(currentOffset - (PLR_DELTA_BYTES-1));
	}
	else {
		deltaBits |= FL_BIGPLRDELTA;
		writer.write((void*)&deltaBits, PLR_DELTA_BYTES);
		writer.seek(currentOffset);
	}

	return EDELTA_WRITE;
}

uint32_t DemoPlayerEnt::readDeltas(mstream& reader) {
	uint32_t deltaBits = 0;
	reader.read(&deltaBits, 1);

	if (deltaBits & FL_BIGPLRDELTA) {
		uint32_t upperBits = 0;
		reader.read(&upperBits, 2);
		deltaBits = deltaBits | (upperBits << 8);
	}

	static char strBuffer[256];

	if (deltaBits & FL_DELTA_FLAGS) {
		uint8_t oldFlags = flags;
		uint8_t newFlags = flags;

		READ_DELTA(reader, deltaBits, FL_DELTA_FLAGS, newFlags, 1);

		if (!oldFlags && newFlags) {
			// new player joined. Start from a fresh state.
			memset(this, 0, sizeof(DemoPlayerEnt));
		}
		flags = newFlags;

		if (!flags) {
			return deltaBits;
		}
	}

	READ_DELTA(reader, deltaBits, FL_DELTA_BUTTON, button, 2);
	READ_DELTA(reader, deltaBits, FL_DELTA_PING, ping, 2);
	READ_DELTA(reader, deltaBits, FL_DELTA_CLIP, clip, 2);
	READ_DELTA(reader, deltaBits, FL_DELTA_PUNCHANGLE_X, punchangle[0], 2);
	READ_DELTA(reader, deltaBits, FL_DELTA_PUNCHANGLE_Y, punchangle[1], 2);
	READ_DELTA(reader, deltaBits, FL_DELTA_PUNCHANGLE_Z, punchangle[2], 2);
	READ_DELTA_STR(reader, deltaBits, FL_DELTA_NAME, name);
	READ_DELTA_STR(reader, deltaBits, FL_DELTA_MODEL, model);
	READ_DELTA(reader, deltaBits, FL_DELTA_STEAMID, steamid64, 8);

	if (deltaBits & FL_DELTA_COLORS) {
		reader.read(&topColor, 1);
		reader.read(&bottomColor, 1);
	}
	
	READ_DELTA(reader, deltaBits, FL_DELTA_VIEWMODEL, viewmodel, 2);
	READ_DELTA(reader, deltaBits, FL_DELTA_WEAPONMODEL, weaponmodel, 2);
	READ_DELTA(reader, deltaBits, FL_DELTA_WEAPONANIM, weaponanim, 1);
	READ_DELTA(reader, deltaBits, FL_DELTA_ARMORVALUE, armorvalue, 2);
	READ_DELTA(reader, deltaBits, FL_DELTA_VIEWOFS, view_ofs, 2);
	READ_DELTA(reader, deltaBits, FL_DELTA_FRAGS, frags, 2);
	READ_DELTA(reader, deltaBits, FL_DELTA_FOV, fov, 1);
	READ_DELTA(reader, deltaBits, FL_DELTA_CLIP2, clip2, 2);
	READ_DELTA(reader, deltaBits, FL_DELTA_AMMO, ammo, 2);
	READ_DELTA(reader, deltaBits, FL_DELTA_AMMO2, ammo2, 2);
	READ_DELTA(reader, deltaBits, FL_DELTA_OBSERVER, observer, 1);

	return deltaBits;
}