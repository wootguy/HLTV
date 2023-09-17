#include "netedict.h"
#include "DemoFile.h"

using namespace std;

#undef read
#undef write

netedict::netedict() {
	memset(&edtype, 0, sizeof(netedict));
}

bool netedict::matches(netedict& other) {
	if (edtype != other.edtype) {
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
	if (animtime != other.animtime) {
		println("Mismatch animtime");
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
	if (blending[0] != other.blending[0]) {
		println("Mismatch blending[0]");
		return false;
	}
	if (blending[1] != other.blending[1]) {
		println("Mismatch blending[1]");
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
		memset(&edtype, 0, sizeof(netedict));
		return; // no need to update other values. Only the isFree var will be sent from now on
	}

	origin[0] = clamp(FLOAT_TO_FIXED(ed.v.origin[0], 19, 5), INT24_MIN, INT24_MAX);
	origin[1] = clamp(FLOAT_TO_FIXED(ed.v.origin[1], 19, 5), INT24_MIN, INT24_MAX);
	origin[2] = clamp(FLOAT_TO_FIXED(ed.v.origin[2], 19, 5), INT24_MIN, INT24_MAX);

	if (ed.v.flags & FL_CLIENT) {
		angles[0] = normalizeRangef(vars.v_angle.x, 0, 360) * (65535.0f / 360.0f);
		angles[1] = normalizeRangef(vars.v_angle.y, 0, 360) * (65535.0f / 360.0f);
		angles[2] = normalizeRangef(vars.v_angle.z, 0, 360) * (65535.0f / 360.0f);
		edtype = NETED_PLAYER;
	}
	else {
		angles[0] = normalizeRangef(vars.angles.x, 0, 360) * (65535.0f / 360.0f);
		angles[1] = normalizeRangef(vars.angles.y, 0, 360) * (65535.0f / 360.0f);
		angles[2] = normalizeRangef(vars.angles.z, 0, 360) * (65535.0f / 360.0f);

		if (ed.v.flags & FL_MONSTER) {
			edtype = NETED_MONSTER;
		}
		else if (ed.v.flags & FL_CUSTOMENTITY) {
			edtype = NETED_BEAM;
		}
		else {
			edtype = NETED_MODEL;
		}
	}

	modelindex = vars.modelindex;
	skin = vars.skin;
	body = vars.body;
	effects = vars.effects;
	sequence = vars.sequence;
	gaitsequence = vars.gaitsequence;
	frame = vars.frame;
	animtime = vars.animtime;
	framerate = clamp(vars.framerate * 16.0f, INT8_MIN, INT8_MAX);
	memcpy(controller, vars.controller, 6); // copy controller AND blending bytes
	scale = clamp(vars.scale * 256.0f, 0, UINT16_MAX);
	rendermode = vars.rendermode;
	renderamt = vars.renderamt;
	memcpy(rendercolor, vars.rendercolor, 3);
	renderfx = vars.renderfx;
	aiment = vars.aiment ? ENTINDEX(vars.aiment) : 0;
	colormap = vars.colormap;
	health = clamp(vars.health, 0, UINT32_MAX);

	uint8_t godbit = (vars.flags & FL_GODMODE) || vars.takedamage == DAMAGE_NO;
	CBaseEntity* bent = (CBaseEntity*)GET_PRIVATE((&ed));
	uint8_t classifyBits = bent && bent->m_fOverrideClass ? bent->m_iClassSelection : 0;
	classifyGod = (classifyBits << 1) | godbit;
}

void netedict::apply(edict_t* ed, vector<EHandle>& simEnts) {
	entvars_t& vars = ed->v;

	if (edtype == NETED_INVALID) {
		vars.effects |= EF_NODRAW;
		return; // no need to update other values. Only the isFree var will be sent from now on
	}

	// calculate instantaneous velocity for gait calculations
	vars.velocity[0] = origin[0] - vars.origin[0];
	vars.velocity[1] = origin[1] - vars.origin[1];
	vars.velocity[2] = origin[2] - vars.origin[2];

	memcpy(&vars.origin, origin, 3 * sizeof(float));
	vars.origin[0] = FIXED_TO_FLOAT(origin[0], 19, 5);
	vars.origin[1] = FIXED_TO_FLOAT(origin[1], 19, 5);
	vars.origin[2] = FIXED_TO_FLOAT(origin[2], 19, 5);

	const float angleConvert = (360.0f / 65535.0f);
	vars.angles = Vector((float)angles[0] * angleConvert, (float)angles[1] * angleConvert, (float)angles[2] * angleConvert);

	vars.modelindex = modelindex;
	vars.skin = skin;
	vars.body = body;
	vars.effects = effects;
	vars.sequence = sequence;
	vars.gaitsequence = gaitsequence;
	vars.frame = frame;
	vars.animtime = animtime;
	vars.framerate = framerate / 16.0f;
	memcpy(vars.controller, controller, 6); // copy controller AND blending bytes
	vars.scale = scale / 256.0f;
	vars.rendermode = rendermode;
	vars.renderamt = renderamt;
	memcpy(vars.rendercolor, rendercolor, 3);
	vars.renderfx = renderfx;
	vars.colormap = colormap;
	vars.health = health;
	vars.playerclass = classifyGod >> 1;
	vars.flags |= (classifyGod & 1) ? FL_GODMODE : 0;

	vars.movetype = vars.aiment ? MOVETYPE_FOLLOW : MOVETYPE_NONE;

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
			memcpy(&vars.origin, &copyent->v.origin, 3 * sizeof(float));
		}
	}
	else {
		vars.aiment = NULL;
	}
}

bool netedict::readDeltas(mstream& reader) {
	uint32_t deltaBits;
	reader.read(&deltaBits, 4);

	if (deltaBits == 0) {
		edtype = NETED_INVALID;
		return false;
	}

	if (deltaBits & FL_DELTA_EDTYPE) {
		reader.read((void*)&edtype, 1);
	}
	if (deltaBits & FL_DELTA_ORIGIN_X) {
		reader.read((void*)&origin[0], 3);
	}
	if (deltaBits & FL_DELTA_ORIGIN_Y) {
		reader.read((void*)&origin[1], 3);
	}
	if (deltaBits & FL_DELTA_ORIGIN_Z) {
		reader.read((void*)&origin[2], 3);
	}
	if (deltaBits & FL_DELTA_ANGLES_X) {
		reader.read((void*)&angles[0], 2);
	}
	if (deltaBits & FL_DELTA_ANGLES_Y) {
		reader.read((void*)&angles[1], 2);
	}
	if (deltaBits & FL_DELTA_ANGLES_Z) {
		reader.read((void*)&angles[2], 2);
	}
	if (deltaBits & FL_DELTA_MODELINDEX) {
		reader.read((void*)&modelindex, 2);
	}
	if (deltaBits & FL_DELTA_SKIN) {
		reader.read((void*)&skin, 1);
	}
	if (deltaBits & FL_DELTA_BODY) {
		reader.read((void*)&body, 1);
	}
	if (deltaBits & FL_DELTA_EFFECTS) {
		reader.read((void*)&effects, 2);
	}
	if (deltaBits & FL_DELTA_SEQUENCE) {
		reader.read((void*)&sequence, 1);
	}
	if (deltaBits & FL_DELTA_GAITSEQUENCE) {
		reader.read((void*)&gaitsequence, 1);
	}
	if (deltaBits & FL_DELTA_FRAME) {
		reader.read((void*)&frame, 1);
	}
	if (deltaBits & FL_DELTA_ANIMTIME) {
		reader.read((void*)&animtime, 1);
	}
	if (deltaBits & FL_DELTA_FRAMERATE) {
		reader.read((void*)&framerate, 1);
	}
	if (deltaBits & FL_DELTA_CONTROLLER_0) {
		reader.read((void*)&controller[0], 1);
	}
	if (deltaBits & FL_DELTA_CONTROLLER_1) {
		reader.read((void*)&controller[1], 1);
	}
	if (deltaBits & FL_DELTA_CONTROLLER_2) {
		reader.read((void*)&controller[2], 1);
	}
	if (deltaBits & FL_DELTA_CONTROLLER_3) {
		reader.read((void*)&controller[3], 1);
	}
	if (deltaBits & FL_DELTA_BLENDING) {
		reader.read((void*)blending, 2);
	}
	if (deltaBits & FL_DELTA_SCALE) {
		reader.read((void*)&scale, 2);
	}
	if (deltaBits & FL_DELTA_RENDERMODE) {
		reader.read((void*)&rendermode, 1);
	}
	if (deltaBits & FL_DELTA_RENDERAMT) {
		reader.read((void*)&renderamt, 1);
	}
	if (deltaBits & FL_DELTA_RENDERCOLOR_0) {
		reader.read((void*)&rendercolor[0], 1);
	}
	if (deltaBits & FL_DELTA_RENDERCOLOR_1) {
		reader.read((void*)&rendercolor[1], 1);
	}
	if (deltaBits & FL_DELTA_RENDERCOLOR_2) {
		reader.read((void*)&rendercolor[2], 1);
	}
	if (deltaBits & FL_DELTA_RENDERFX) {
		reader.read((void*)&renderfx, 1);
	}
	if (deltaBits & FL_DELTA_AIMENT) {
		reader.read((void*)&aiment, 2);
	}
	if (deltaBits & FL_DELTA_HEALTH) {
		reader.read((void*)&health, 4);
	}
	if (deltaBits & FL_DELTA_COLORMAP) {
		reader.read((void*)&colormap, 1);
	}
	if (deltaBits & FL_DELTA_CLASSIFYGOD) {
		reader.read((void*)&classifyGod, 1);
	}

	return true;
}

int netedict::writeDeltas(mstream& writer, netedict& old) {
	uint64_t startOffset = writer.tell();

	uint32_t deltaBits = 0; // flags which fields were changed

	if (old.edtype != edtype) {
		if (!edtype) {
			// 0 deltas indicates edict was deleted
			writer.write(&deltaBits, 4);

			if (writer.eom()) {
				writer.seek(startOffset);
				return EDELTA_OVERFLOW;
			}

			return EDELTA_WRITE;
		}
	}

	writer.skip(4); // write delta bits later

	if (old.edtype != edtype) {
		deltaBits |= FL_DELTA_EDTYPE;
		writer.write((void*)&edtype, 1);
	}
	if (old.origin[0] != origin[0]) {
		deltaBits |= FL_DELTA_ORIGIN_X;
		writer.write((void*)&origin[0], 3);
	}
	if (old.origin[1] != origin[1]) {
		deltaBits |= FL_DELTA_ORIGIN_Y;
		writer.write((void*)&origin[1], 3);
	}
	if (old.origin[2] != origin[2]) {
		deltaBits |= FL_DELTA_ORIGIN_Z;
		writer.write((void*)&origin[2], 3);
	}
	if (old.angles[0] != angles[0]) {
		deltaBits |= FL_DELTA_ANGLES_X;
		writer.write((void*)&angles[0], 2);
	}
	if (old.angles[1] != angles[1]) {
		deltaBits |= FL_DELTA_ANGLES_Y;
		writer.write((void*)&angles[1], 2);
	}
	if (old.angles[2] != angles[2]) {
		deltaBits |= FL_DELTA_ANGLES_Z;
		writer.write((void*)&angles[2], 2);
	}
	if (old.modelindex != modelindex) {
		deltaBits |= FL_DELTA_MODELINDEX;
		writer.write((void*)&modelindex, 2);
	}
	if (old.skin != skin) {
		deltaBits |= FL_DELTA_SKIN;
		writer.write((void*)&skin, 1);
	}
	if (old.body != body) {
		deltaBits |= FL_DELTA_BODY;
		writer.write((void*)&body, 1);
	}
	if (old.effects != effects) {
		deltaBits |= FL_DELTA_EFFECTS;
		writer.write((void*)&effects, 2);
	}
	if (old.sequence != sequence) {
		deltaBits |= FL_DELTA_SEQUENCE;
		writer.write((void*)&sequence, 1);
	}
	if (old.gaitsequence != gaitsequence) {
		deltaBits |= FL_DELTA_GAITSEQUENCE;
		writer.write((void*)&gaitsequence, 1);
	}
	if (old.frame != frame) {
		deltaBits |= FL_DELTA_FRAME;
		writer.write((void*)&frame, 1);
	}
	if (old.animtime != animtime) {
		deltaBits |= FL_DELTA_ANIMTIME;
		writer.write((void*)&animtime, 1);
	}
	if (old.framerate != framerate) {
		deltaBits |= FL_DELTA_FRAMERATE;
		writer.write((void*)&framerate, 1);
	}
	if (old.controller[0] != controller[0]) {
		deltaBits |= FL_DELTA_CONTROLLER_0;
		writer.write((void*)&controller[0], 1);
	}
	if (old.controller[1] != controller[1]) {
		deltaBits |= FL_DELTA_CONTROLLER_1;
		writer.write((void*)&controller[1], 1);
	}
	if (old.controller[2] != controller[2]) {
		deltaBits |= FL_DELTA_CONTROLLER_2;
		writer.write((void*)&controller[2], 1);
	}
	if (old.controller[3] != controller[3]) {
		deltaBits |= FL_DELTA_CONTROLLER_3;
		writer.write((void*)&controller[3], 1);
	}
	if (old.blending[0] != blending[0] || old.blending[1] != blending[1]) {
		deltaBits |= FL_DELTA_BLENDING;
		writer.write((void*)&blending, 2);
	}
	if (old.scale != scale) {
		deltaBits |= FL_DELTA_SCALE;
		writer.write((void*)&scale, 2);
	}
	if (old.rendermode != rendermode) {
		deltaBits |= FL_DELTA_RENDERMODE;
		writer.write((void*)&rendermode, 1);
	}
	if (old.renderamt != renderamt) {
		deltaBits |= FL_DELTA_RENDERAMT;
		writer.write((void*)&renderamt, 1);
	}
	if (old.rendercolor[0] != rendercolor[0]) {
		deltaBits |= FL_DELTA_RENDERCOLOR_0;
		writer.write((void*)&rendercolor[0], 1);
	}
	if (old.rendercolor[1] != rendercolor[1]) {
		deltaBits |= FL_DELTA_RENDERCOLOR_1;
		writer.write((void*)&rendercolor[1], 1);
	}
	if (old.rendercolor[2] != rendercolor[2]) {
		deltaBits |= FL_DELTA_RENDERCOLOR_2;
		writer.write((void*)&rendercolor[2], 1);
	}
	if (old.renderfx != renderfx) {
		deltaBits |= FL_DELTA_RENDERFX;
		writer.write((void*)&renderfx, 1);
	}
	if (old.aiment != aiment) {
		deltaBits |= FL_DELTA_AIMENT;
		writer.write((void*)&aiment, 2);
	}
	if (old.health != health) {
		deltaBits |= FL_DELTA_HEALTH;
		writer.write((void*)&health, 4);
	}
	if (old.colormap != colormap) {
		deltaBits |= FL_DELTA_COLORMAP;
		writer.write((void*)&colormap, 1);
	}
	if (old.classifyGod != classifyGod) {
		deltaBits |= FL_DELTA_CLASSIFYGOD;
		writer.write((void*)&classifyGod, 1);
	}

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
