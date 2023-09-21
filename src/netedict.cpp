#include "netedict.h"
#include "DemoFile.h"
#include "main.h"

using namespace std;

#undef read
#undef write

#define READ_DELTA(reader, deltaBits, deltaFlag, field, sz) \
	if (deltaBits & deltaFlag) { \
		g_stats.entDeltaSz[bitoffset(deltaFlag)] += sz; \
		reader.read((void*)&field, sz); \
	}

#define WRITE_DELTA(writer, deltaBits, deltaFlag, field, sz) \
	if (old.field != field) { \
		deltaBits |= deltaFlag; \
		g_stats.entDeltaSz[bitoffset(deltaFlag)] += sz; \
		writer.write((void*)&field, sz); \
	}

netedict::netedict() {
	memset(&edflags, 0, sizeof(netedict));
}

bool netedict::matches(netedict& other) {
	if (edflags != other.edflags) {
		println("Mismatch isValid");
		return false;
	}
	if (origin[0] != other.origin[0]) {
		println("Mismatch origin[0]");
		return false;
	}
	if (origin[1] != other.origin[1]) {
		println("Mismatch isValid");
		return false;
	}
	if (origin[1] != other.origin[1]) {
		println("Mismatch origin[1]");
		return false;
	}
	if (angles[0] != other.angles[0]) {
		println("Mismatch angles[0]");
		return false;
	}
	if (other.angles[1] != other.angles[1]) {
		println("Mismatch angles[1]");
		return false;
	}
	if (angles[2] != other.angles[2]) {
		println("Mismatch angles[2]");
		return false;
	}
	if (modelindex != other.modelindex) {
		println("Mismatch modelindex");
		return false;
	}
	if (skin != other.skin) {
		println("Mismatch skin");
		return false;
	}
	if (body != other.body) {
		println("Mismatch body");
		return false;
	}
	if (effects != other.effects) {
		println("Mismatch effects");
		return false;
	}
	if (sequence != other.sequence) {
		println("Mismatch sequence");
		return false;
	}
	if (gaitsequence != other.gaitsequence) {
		println("Mismatch gaitsequence");
		return false;
	}
	if (frame != other.frame) {
		println("Mismatch frame");
		return false;
	}
	if (framerate != other.framerate) {
		println("Mismatch framerate");
		return false;
	}
	if (controller[0] != other.controller[0]) {
		println("Mismatch controller[0]");
		return false;
	}
	if (controller[1] != other.controller[1]) {
		println("Mismatch controller[1]");
		return false;
	}
	if (controller[2] != other.controller[2]) {
		println("Mismatch controller[2]");
		return false;
	}
	if (scale != other.scale) {
		println("Mismatch scale");
		return false;
	}
	if (rendermode != other.rendermode) {
		println("Mismatch rendermode");
		return false;
	}
	if (renderamt != other.renderamt) {
		println("Mismatch renderamt");
		return false;
	}
	if (rendercolor[0] != other.rendercolor[0]) {
		println("Mismatch rendercolor[0]");
		return false;
	}
	if (rendercolor[1] != other.rendercolor[1]) {
		println("Mismatch rendercolor[1]");
		return false;
	}
	if (rendercolor[2] != other.rendercolor[2]) {
		println("Mismatch rendercolor[2]");
		return false;
	}
	if (renderfx != other.renderfx) {
		println("Mismatch renderfx");
		return false;
	}
	if (aiment != other.aiment) {
		println("Mismatch aiment");
		return false;
	}
	if (health != other.health) {
		println("Mismatch health");
		return false;
	}
	if (colormap != other.colormap) {
		println("Mismatch colormap");
		return false;
	}
	if (health != other.health) {
		println("Mismatch health");
		return false;
	}
	if (classifyGod != other.classifyGod) {
		println("Mismatch classifyGod");
		return false;
	}
	return true;
}

void netedict::load(const edict_t& ed) {
	entvars_t vars = ed.v;

	bool isValid = !ed.free && ed.pvPrivateData && (vars.effects & EF_NODRAW) == 0 && vars.modelindex;

	if (!isValid) {
		memset(&edflags, 0, sizeof(netedict));
		return; // no need to update other values. Only the isFree var will be sent from now on
	}

	origin[0] = clamp(FLOAT_TO_FIXED(ed.v.origin[0], 19, 5), INT24_MIN, INT24_MAX);
	origin[1] = clamp(FLOAT_TO_FIXED(ed.v.origin[1], 19, 5), INT24_MIN, INT24_MAX);
	origin[2] = clamp(FLOAT_TO_FIXED(ed.v.origin[2], 19, 5), INT24_MIN, INT24_MAX);

	modelindex = vars.modelindex;
	skin = vars.skin;
	body = vars.body;
	effects = vars.effects;
	sequence = vars.sequence;
	gaitsequence = vars.gaitsequence;
	frame = vars.frame;
	framerate = clamp(vars.framerate * 16.0f, INT8_MIN, INT8_MAX);
	memcpy(controller, vars.controller, 4);
	scale = clamp(vars.scale * 256.0f, 0, UINT16_MAX);
	rendermode = vars.rendermode;
	renderamt = vars.renderamt;
	rendercolor[0] = vars.rendercolor[0];
	rendercolor[1] = vars.rendercolor[1];
	rendercolor[2] = vars.rendercolor[2];
	renderfx = vars.renderfx;
	aiment = vars.aiment ? ENTINDEX(vars.aiment) : 0;
	colormap = vars.colormap;
	health = vars.health > 0 ? Min(vars.health, UINT32_MAX) : 0;

	edflags |= EDFLAG_VALID;

	if (ed.v.flags & FL_CLIENT) {
		angles[0] = (uint16_t)(normalizeRangef(vars.v_angle.x, 0, 360) * (65535.0f / 360.0f));
		angles[1] = (uint16_t)(normalizeRangef(vars.v_angle.y, 0, 360) * (65535.0f / 360.0f));
		angles[2] = (uint16_t)(normalizeRangef(vars.v_angle.z, 0, 360) * (65535.0f / 360.0f));
		edflags |= EDFLAG_PLAYER;
	}
	else if (ed.v.flags & FL_CUSTOMENTITY) {
		edflags |= EDFLAG_BEAM;
		angles[0] = clamp(FLOAT_TO_FIXED(ed.v.angles[0], 21, 3), INT24_MIN, INT24_MAX);
		angles[1] = clamp(FLOAT_TO_FIXED(ed.v.angles[1], 21, 3), INT24_MIN, INT24_MAX);
		angles[2] = clamp(FLOAT_TO_FIXED(ed.v.angles[2], 21, 3), INT24_MIN, INT24_MAX);
		// not enough bits in sequence/skin so using aiment/owner instead
		// these vars set the start/end entity and attachment points
		aiment = vars.sequence;
		((uint16_t*)controller)[0] = vars.skin;
		sequence = 0;
		skin = 0;
	}
	else {
		angles[0] = (uint16_t)(normalizeRangef(vars.angles.x, 0, 360) * (65535.0f / 360.0f));
		angles[1] = (uint16_t)(normalizeRangef(vars.angles.y, 0, 360) * (65535.0f / 360.0f));
		angles[2] = (uint16_t)(normalizeRangef(vars.angles.z, 0, 360) * (65535.0f / 360.0f));

		if (ed.v.flags & FL_MONSTER) {
			edflags |= EDFLAG_MONSTER;
		}
	}

	if (ed.v.flags & (FL_CLIENT | FL_MONSTER)) {
		uint8_t godbit = (vars.flags & FL_GODMODE) || vars.takedamage == DAMAGE_NO;
		CBaseEntity* bent = (CBaseEntity*)GET_PRIVATE((&ed));
		uint8_t classifyBits = bent && bent->m_fOverrideClass ? bent->m_iClassSelection : 0;
		classifyGod = (classifyBits << 1) | godbit;
	}
}

void netedict::apply(edict_t* ed, vector<EHandle>& simEnts) {
	entvars_t& vars = ed->v;

	if (!edflags) {
		return; // no need to update other values. Only the isFree var will be sent from now on
	}

	Vector oldorigin;
	memcpy(oldorigin, vars.origin, sizeof(Vector));

	vars.modelindex = modelindex;
	vars.skin = skin;
	vars.body = body;
	vars.effects = effects;
	vars.sequence = sequence;
	vars.gaitsequence = gaitsequence;
	vars.frame = frame;
	vars.framerate = framerate / 16.0f;
	memcpy(vars.controller, controller, 4);
	vars.scale = scale / 256.0f;
	vars.rendermode = rendermode;
	vars.renderamt = renderamt;
	vars.rendercolor[0] = rendercolor[0];
	vars.rendercolor[1] = rendercolor[1];
	vars.rendercolor[2] = rendercolor[2];
	vars.renderfx = renderfx;
	vars.colormap = colormap;
	vars.health = health;
	vars.playerclass = classifyGod >> 1;
	vars.flags |= (classifyGod & 1) ? FL_GODMODE : 0;
	vars.aiment = NULL;

	//vars.movetype = vars.aiment ? MOVETYPE_FOLLOW : MOVETYPE_NONE;

	if (edflags & EDFLAG_BEAM) {
		uint16_t startIdx = aiment & 0xfff;
		uint16_t endIdx = ((uint16_t*)controller)[0] & 0xfff;

		if (startIdx) {
			if (startIdx < simEnts.size()) {
				edict_t* copyent = simEnts[startIdx];
				vars.sequence = (aiment & 0xf000) | ENTINDEX(copyent);
				vars.origin = copyent->v.origin; // must be set even if not used
			}
			else {
				println("Invalid beam start entity %d / %d", startIdx, (int)simEnts.size());
			}
		}
		else {
			vars.origin[0] = FIXED_TO_FLOAT(origin[0], 19, 5);
			vars.origin[1] = FIXED_TO_FLOAT(origin[1], 19, 5);
			vars.origin[2] = FIXED_TO_FLOAT(origin[2], 19, 5);
		}
		if (endIdx) {
			if (endIdx < simEnts.size()) {
				edict_t* copyent = simEnts[endIdx];
				vars.skin = (((uint16_t*)controller)[0] & 0xf000) | ENTINDEX(simEnts[endIdx]);
			}
			else {
				println("Invalid beam end entity %d / %d", endIdx, (int)simEnts.size());
			}
		}
		else {
			vars.angles[0] = FIXED_TO_FLOAT(angles[0], 21, 3);
			vars.angles[1] = FIXED_TO_FLOAT(angles[1], 21, 3);
			vars.angles[2] = FIXED_TO_FLOAT(angles[2], 21, 3);
		}
	}
	else {
		if (aiment) {
			if (aiment >= simEnts.size()) {
				println("Invalid aiment %d / %d", aiment, (int)simEnts.size());
				vars.movetype = MOVETYPE_NONE;
				return;
			}
			else {
				//vars.aiment = simEnts[aiment];
				// aiment causing hard-to-troubleshoot crashes, so just set origin

				edict_t* copyent = simEnts[aiment];

				if ((edflags & (EDFLAG_MONSTER|EDFLAG_PLAYER)) == 0 && vars.skin && vars.body) {
					GET_ATTACHMENT(copyent, vars.body-1, vars.origin, vars.angles);
					vars.skin = 0;
					vars.body = 0;
				}
				else {
					vars.origin = copyent->v.origin;
				}
			}
		}
		else {
			vars.origin[0] = FIXED_TO_FLOAT(origin[0], 19, 5);
			vars.origin[1] = FIXED_TO_FLOAT(origin[1], 19, 5);
			vars.origin[2] = FIXED_TO_FLOAT(origin[2], 19, 5);
		}

		const float angleConvert = (360.0f / 65535.0f);
		vars.angles = Vector((float)angles[0] * angleConvert, (float)angles[1] * angleConvert, (float)angles[2] * angleConvert);
	}

	// calculate instantaneous velocity for gait calculations
	if (edflags == EDFLAG_PLAYER)
		vars.velocity = (vars.origin - oldorigin);
}

bool netedict::readDeltas(mstream& reader) {
	uint32_t deltaBits;
	reader.read(&deltaBits, 4);

	if (deltaBits == 0) {
		edflags = 0;
		return false;
	}

	uint8_t oldedflags = edflags;
	uint8_t newedflags = edflags;

	READ_DELTA(reader, deltaBits, FL_DELTA_EDFLAGS, newedflags, 1);

	if (!oldedflags && newedflags) {
		// new entity created. Start deltas from a fresh state.
		memset(this, 0, sizeof(netedict));
	}

	edflags = newedflags;

	int angleSz = edflags & EDFLAG_BEAM ? 3 : 2;

	READ_DELTA(reader, deltaBits, FL_DELTA_ORIGIN_X, origin[0], 3);
	READ_DELTA(reader, deltaBits, FL_DELTA_ORIGIN_Y, origin[1], 3);
	READ_DELTA(reader, deltaBits, FL_DELTA_ORIGIN_Z, origin[2], 3);
	READ_DELTA(reader, deltaBits, FL_DELTA_ANGLES_X, angles[0], angleSz);
	READ_DELTA(reader, deltaBits, FL_DELTA_ANGLES_Y, angles[1], angleSz);
	READ_DELTA(reader, deltaBits, FL_DELTA_ANGLES_Z, angles[2], angleSz);
	READ_DELTA(reader, deltaBits, FL_DELTA_MODELINDEX, modelindex, 2);
	READ_DELTA(reader, deltaBits, FL_DELTA_SKIN, skin, 1);
	READ_DELTA(reader, deltaBits, FL_DELTA_BODY, body, 1);
	READ_DELTA(reader, deltaBits, FL_DELTA_EFFECTS, effects, 2);
	READ_DELTA(reader, deltaBits, FL_DELTA_SEQUENCE, sequence, 1);
	READ_DELTA(reader, deltaBits, FL_DELTA_GAITSEQUENCE, gaitsequence, 1);
	READ_DELTA(reader, deltaBits, FL_DELTA_FRAME, frame, 1);
	READ_DELTA(reader, deltaBits, FL_DELTA_FRAMERATE, framerate, 1);
	READ_DELTA(reader, deltaBits, FL_DELTA_CONTROLLER_0, controller[0], 1);
	READ_DELTA(reader, deltaBits, FL_DELTA_CONTROLLER_1, controller[1], 1);
	READ_DELTA(reader, deltaBits, FL_DELTA_CONTROLLER_HI, controller[2], 2);
	READ_DELTA(reader, deltaBits, FL_DELTA_SCALE, scale, 2);
	READ_DELTA(reader, deltaBits, FL_DELTA_RENDERMODE, rendermode, 1);
	READ_DELTA(reader, deltaBits, FL_DELTA_RENDERAMT, renderamt, 1);
	READ_DELTA(reader, deltaBits, FL_DELTA_RENDERCOLOR_0, rendercolor[0], 1);
	READ_DELTA(reader, deltaBits, FL_DELTA_RENDERCOLOR_1, rendercolor[1], 1);
	READ_DELTA(reader, deltaBits, FL_DELTA_RENDERCOLOR_2, rendercolor[2], 1);
	READ_DELTA(reader, deltaBits, FL_DELTA_RENDERFX, renderfx, 1);
	READ_DELTA(reader, deltaBits, FL_DELTA_AIMENT, aiment, 2);
	READ_DELTA(reader, deltaBits, FL_DELTA_HEALTH, health, 4);
	READ_DELTA(reader, deltaBits, FL_DELTA_COLORMAP, colormap, 1);
	READ_DELTA(reader, deltaBits, FL_DELTA_CLASSIFYGOD, classifyGod, 1);

	return true;
}

int netedict::writeDeltas(mstream& writer, netedict& old) {
	uint64_t startOffset = writer.tell();

	uint32_t deltaBits = 0; // flags which fields were changed

	uint8_t newedflags = edflags;

	if (old.edflags != newedflags) {
		if (!edflags) {
			// 0 deltas indicates edict was deleted
			writer.write(&deltaBits, 4);

			if (writer.eom()) {
				writer.seek(startOffset);
				return EDELTA_OVERFLOW;
			}

			return EDELTA_WRITE;
		}
		if (!old.edflags) {
			// new entity created. Start from a fresh state.
			// some vars may have changed while we didn't send deltas but still memcpy()'d
			// to fileedicts as if the client knows the latest state.
			memset(this, 0, sizeof(netedict));
			edflags = newedflags;
		}
	}

	writer.skip(4); // write delta bits later

	int angleSz = edflags & EDFLAG_BEAM ? 3 : 2;

	WRITE_DELTA(writer, deltaBits, FL_DELTA_EDFLAGS, edflags, 1);
	WRITE_DELTA(writer, deltaBits, FL_DELTA_ORIGIN_X, origin[0], 3);
	WRITE_DELTA(writer, deltaBits, FL_DELTA_ORIGIN_Y, origin[1], 3);
	WRITE_DELTA(writer, deltaBits, FL_DELTA_ORIGIN_Z, origin[2], 3);
	WRITE_DELTA(writer, deltaBits, FL_DELTA_ANGLES_X, angles[0], angleSz);
	WRITE_DELTA(writer, deltaBits, FL_DELTA_ANGLES_Y, angles[1], angleSz);
	WRITE_DELTA(writer, deltaBits, FL_DELTA_ANGLES_Z, angles[2], angleSz);
	WRITE_DELTA(writer, deltaBits, FL_DELTA_MODELINDEX, modelindex, 2);
	WRITE_DELTA(writer, deltaBits, FL_DELTA_SKIN, skin, 1);
	WRITE_DELTA(writer, deltaBits, FL_DELTA_BODY, body, 1);
	WRITE_DELTA(writer, deltaBits, FL_DELTA_EFFECTS, effects, 2);
	WRITE_DELTA(writer, deltaBits, FL_DELTA_SEQUENCE, sequence, 1);
	WRITE_DELTA(writer, deltaBits, FL_DELTA_GAITSEQUENCE, gaitsequence, 1);
	WRITE_DELTA(writer, deltaBits, FL_DELTA_FRAME, frame, 1);
	WRITE_DELTA(writer, deltaBits, FL_DELTA_FRAMERATE, framerate, 1);
	WRITE_DELTA(writer, deltaBits, FL_DELTA_CONTROLLER_0, controller[0], 1);
	WRITE_DELTA(writer, deltaBits, FL_DELTA_CONTROLLER_1, controller[1], 1);
	if (old.controller[2] != controller[2] || old.controller[3] != controller[3]) {
		deltaBits |= FL_DELTA_CONTROLLER_HI;
		g_stats.entDeltaSz[bitoffset(FL_DELTA_CONTROLLER_HI)] += 2;
		writer.write((void*)&controller[2], 2);
	}
	WRITE_DELTA(writer, deltaBits, FL_DELTA_SCALE, scale, 2);
	WRITE_DELTA(writer, deltaBits, FL_DELTA_RENDERMODE, rendermode, 1);
	WRITE_DELTA(writer, deltaBits, FL_DELTA_RENDERAMT, renderamt, 1);
	WRITE_DELTA(writer, deltaBits, FL_DELTA_RENDERCOLOR_0, rendercolor[0], 1);
	WRITE_DELTA(writer, deltaBits, FL_DELTA_RENDERCOLOR_1, rendercolor[1], 1);
	WRITE_DELTA(writer, deltaBits, FL_DELTA_RENDERCOLOR_2, rendercolor[2], 1);
	WRITE_DELTA(writer, deltaBits, FL_DELTA_RENDERFX, renderfx, 1);
	WRITE_DELTA(writer, deltaBits, FL_DELTA_AIMENT, aiment, 2);
	WRITE_DELTA(writer, deltaBits, FL_DELTA_HEALTH, health, 4);
	WRITE_DELTA(writer, deltaBits, FL_DELTA_COLORMAP, colormap, 1);
	WRITE_DELTA(writer, deltaBits, FL_DELTA_CLASSIFYGOD, classifyGod, 1);

	if (writer.eom()) {
		writer.seek(startOffset);
		return EDELTA_OVERFLOW;
	}

	uint64_t currentOffset = writer.tell();
	writer.seek(startOffset);

	if (deltaBits == 0) {
		return EDELTA_NONE;
	}

	writer.write((void*)&deltaBits, 4);
	writer.seek(currentOffset);

	return EDELTA_WRITE;
}
