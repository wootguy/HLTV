#include "netedict.h"
#include "DemoFile.h"
#include "main.h"
#include "DemoPlayer.h"

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
	reset();
}

void netedict::reset() {
	memset(this, 0, sizeof(netedict));
}

bool netedict::matches(netedict& other) {
	if (edflags != other.edflags) {
		ALERT(at_console, "Mismatch isValid\n");
		return false;
	}
	if (!FIXED_EQUALS(origin[0], other.origin[0], 24)) {
		ALERT(at_console, "Mismatch origin[0]\n");
		return false;
	}
	if (!FIXED_EQUALS(origin[1], other.origin[1], 24)) {
		ALERT(at_console, "Mismatch origin[1]\n");
		return false;
	}
	if (!FIXED_EQUALS(origin[2], other.origin[2], 24)) {
		ALERT(at_console, "Mismatch origin[2]\n");
		return false;
	}
	if (!FIXED_EQUALS(angles[0], other.angles[0], 24)) {
		ALERT(at_console, "Mismatch angles[0]\n");
		return false;
	}
	if (!FIXED_EQUALS(angles[1], other.angles[1], 24)) {
		ALERT(at_console, "Mismatch angles[1]\n");
		return false;
	}
	if (!FIXED_EQUALS(angles[2], other.angles[2], 24)) {
		ALERT(at_console, "Mismatch angles[2]\n");
		return false;
	}
	if (modelindex != other.modelindex) {
		ALERT(at_console, "Mismatch modelindex\n");
		return false;
	}
	if (skin != other.skin) {
		ALERT(at_console, "Mismatch skin\n");
		return false;
	}
	if (body != other.body) {
		ALERT(at_console, "Mismatch body\n");
		return false;
	}
	if (effects != other.effects) {
		ALERT(at_console, "Mismatch effects\n");
		return false;
	}
	if (sequence != other.sequence) {
		ALERT(at_console, "Mismatch sequence\n");
		return false;
	}
	if (gaitblend != other.gaitblend) {
		ALERT(at_console, "Mismatch gaitblend\n");
		return false;
	}
	if (frame != other.frame) {
		ALERT(at_console, "Mismatch frame\n");
		return false;
	}
	if (framerate != other.framerate) {
		ALERT(at_console, "Mismatch framerate\n");
		return false;
	}
	if (controller_lo != other.controller_lo) {
		ALERT(at_console, "Mismatch controller_lo\n");
		return false;
	}
	if (controller_hi != other.controller_hi) {
		ALERT(at_console, "Mismatch controller_hi\n");
		return false;
	}
	if (scale != other.scale) {
		ALERT(at_console, "Mismatch scale\n");
		return false;
	}
	if (renderamt != other.renderamt) {
		ALERT(at_console, "Mismatch renderamt\n");
		return false;
	}
	if (rendercolor[0] != other.rendercolor[0]) {
		ALERT(at_console, "Mismatch rendercolor[0]\n");
		return false;
	}
	if (rendercolor[1] != other.rendercolor[1]) {
		ALERT(at_console, "Mismatch rendercolor[1]\n");
		return false;
	}
	if (rendercolor[2] != other.rendercolor[2]) {
		ALERT(at_console, "Mismatch rendercolor[2]\n");
		return false;
	}
	if (rendermodefx != other.rendermodefx) {
		ALERT(at_console, "Mismatch rendermodefx\n");
		return false;
	}
	if (aiment != other.aiment) {
		ALERT(at_console, "Mismatch aiment\n");
		return false;
	}
	if (health != other.health) {
		ALERT(at_console, "Mismatch health\n");
		return false;
	}
	if (colormap != other.colormap) {
		ALERT(at_console, "Mismatch colormap\n");
		return false;
	}
	if (health != other.health) {
		ALERT(at_console, "Mismatch health\n");
		return false;
	}
	if (classify != other.classify) {
		ALERT(at_console, "Mismatch classify\n");
		return false;
	}
	return true;
}

void netedict::load(const edict_t& ed) {
	entvars_t vars = ed.v;

	bool isValid = !ed.free && ed.pvPrivateData && (vars.effects & EF_NODRAW) == 0 && vars.modelindex;

	if (!isValid) {
		reset();
		return; // no need to update other values. Only the isFree var will be sent from now on
	}

	skin = vars.skin;
	body = vars.body;
	effects = vars.effects;
	gaitblend = (vars.gaitsequence << 8) | vars.blending[0];
	int8_t newFramerate = clamp(vars.framerate * 16.0f, INT8_MIN, INT8_MAX);
	uint8_t newSequence = vars.sequence;
	uint16_t newModelindex = vars.modelindex;

	memcpy(&controller_lo, vars.controller, 4);

	scale = clamp(vars.scale * 256.0f, 0, UINT16_MAX);
	rendermodefx = ((vars.rendermode & 0x7) << 5) | (vars.renderfx & 0x1f);
	renderamt = vars.renderamt;
	rendercolor[0] = vars.rendercolor[0];
	rendercolor[1] = vars.rendercolor[1];
	rendercolor[2] = vars.rendercolor[2];
	aiment = vars.aiment ? ENTINDEX(vars.aiment) : 0;
	colormap = vars.colormap;
	health = vars.health > 0 ? V_min(vars.health, UINT32_MAX) : 0;


	CBaseEntity* ent = CBaseEntity::Instance(&ed); 
	CBaseAnimating* anim = ent ? ent->MyAnimatingPointer() : NULL; // TODO: not thread safe
	//CBaseAnimating* anim = NULL;
	bool isBspModel = anim && anim->IsBSPModel();

	bool animationReset = framerate != newFramerate || newSequence != sequence || newModelindex != modelindex;
	if (anim) {
		// probably set in other cases than ResetSequenceInfo, but not in the HLSDK
		// using this you can catch cases where only the frame has changed (e.g. restarting an attack anim)
		// monsters update this every think so only checking for 0 frame resets for those ents
		// players reset to non-zero for things like crowbars or the emotes plugin
		animationReset |= lastAnimationReset < anim->m_flLastEventCheck && (vars.frame == 0 || (ed.v.flags & FL_CLIENT));
		lastAnimationReset = anim->m_flLastEventCheck;
	}
	
	uint8_t oldFrame = frame;
	if (animationReset || isBspModel) {
		// clients can no longer predict the current frame
		frame = vars.frame;
	}

	forceNextFrame = animationReset || (isBspModel && oldFrame != frame);
	framerate = newFramerate;
	sequence = newSequence;
	modelindex = newModelindex;

	edflags |= EDFLAG_VALID;

	if (ed.v.flags & FL_CLIENT) {
		origin[0] = FLOAT_TO_FIXED(ed.v.origin[0], 19, 5);
		origin[1] = FLOAT_TO_FIXED(ed.v.origin[1], 19, 5);
		origin[2] = FLOAT_TO_FIXED(ed.v.origin[2], 19, 5);
		angles[0] = (uint16_t)(normalizeRangef(vars.v_angle.x, 0, 360) * (255.0f / 360.0f));
		angles[1] = (uint16_t)(normalizeRangef(vars.v_angle.y, 0, 360) * (255.0f / 360.0f));
		angles[2] = (uint16_t)(normalizeRangef(vars.v_angle.z, 0, 360) * (255.0f / 360.0f));
		edflags |= EDFLAG_PLAYER;
	}
	else if (ed.v.flags & FL_CUSTOMENTITY) {
		edflags |= EDFLAG_BEAM;
		origin[0] = FLOAT_TO_FIXED(ed.v.origin[0], 21, 3);
		origin[1] = FLOAT_TO_FIXED(ed.v.origin[1], 21, 3);
		origin[2] = FLOAT_TO_FIXED(ed.v.origin[2], 21, 3);
		angles[0] = FLOAT_TO_FIXED(ed.v.angles[0], 21, 3);
		angles[1] = FLOAT_TO_FIXED(ed.v.angles[1], 21, 3);
		angles[2] = FLOAT_TO_FIXED(ed.v.angles[2], 21, 3);
		// not enough bits in sequence/skin so using aiment/owner instead
		// these vars set the start/end entity and attachment points
		aiment = vars.sequence;
		controller_lo = vars.skin;
		sequence = 0;
		skin = 0;
	}
	else {
		origin[0] = FLOAT_TO_FIXED(ed.v.origin[0], 23, 1);
		origin[1] = FLOAT_TO_FIXED(ed.v.origin[1], 23, 1);
		origin[2] = FLOAT_TO_FIXED(ed.v.origin[2], 23, 1);
		angles[0] = (uint16_t)(normalizeRangef(vars.angles.x, 0, 360) * (255.0f / 360.0f));
		angles[1] = (uint16_t)(normalizeRangef(vars.angles.y, 0, 360) * (255.0f / 360.0f));
		angles[2] = (uint16_t)(normalizeRangef(vars.angles.z, 0, 360) * (255.0f / 360.0f));

		if (ed.v.flags & FL_MONSTER) {
			edflags |= EDFLAG_MONSTER;
		}
	}

	if ((vars.flags & FL_GODMODE) || vars.takedamage == DAMAGE_NO) {
		edflags |= EDFLAG_GOD;
	}

	if (vars.flags & FL_NOTARGET) {
		edflags |= EDFLAG_NOTARGET;
	}

	if (ed.v.flags & (FL_CLIENT | FL_MONSTER)) {
		CBaseEntity* bent = (CBaseEntity*)GET_PRIVATE((&ed)); // TODO: not thread safe

#ifdef HLCOOP_BUILD
		uint8_t classifyBits = bent && bent->m_Classify ? bent->m_Classify : 0;
#else
		uint8_t classifyBits = bent && bent->m_fOverrideClass ? bent->m_iClassSelection : 0;
#endif
		
		classify = classifyBits;
	}
}

void netedict::apply(edict_t* ed) {
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
	vars.gaitsequence = gaitblend >> 8;
	vars.frame = frame;
	vars.framerate = framerate / 16.0f;
	memcpy(vars.controller, &controller_lo, 4);
	vars.blending[0] = gaitblend & 0xff;
	vars.scale = scale / 256.0f;
	vars.rendermode = rendermodefx >> 5;
	vars.renderamt = renderamt;
	vars.rendercolor[0] = rendercolor[0];
	vars.rendercolor[1] = rendercolor[1];
	vars.rendercolor[2] = rendercolor[2];
	vars.renderfx = rendermodefx & 0x1f;
	vars.colormap = colormap;
	vars.health = health;
	vars.playerclass = classify;
	vars.flags |= (edflags & EDFLAG_GOD) ? FL_GODMODE : 0;
	vars.aiment = NULL;

	CBaseEntity* baseent = (CBaseEntity*)GET_PRIVATE(ed);
	if (baseent) {
#ifdef HLCOOP_BUILD
		baseent->m_Classify = classify;
#else
		baseent->m_iClassSelection = classify;
		baseent->m_fOverrideClass = baseent->m_iClassSelection != 0;
#endif
	}

	//vars.movetype = vars.aiment ? MOVETYPE_FOLLOW : MOVETYPE_NONE;

	if (edflags & EDFLAG_BEAM) {
		uint16_t startIdx = aiment & 0xfff;
		uint16_t endIdx = controller_lo & 0xfff;

		if (startIdx) {
			edict_t* copyent = g_demoPlayer->getReplayEntity(startIdx);
			if (copyent) {
				vars.sequence = (aiment & 0xf000) | ENTINDEX(copyent);
				vars.origin = copyent->v.origin; // must be set even if not used
			}
			else {
				ALERT(at_console, "Invalid beam start entity %d", startIdx);
			}
		}
		else {
			vars.origin[0] = FIXED_TO_FLOAT(origin[0], 21, 3);
			vars.origin[1] = FIXED_TO_FLOAT(origin[1], 21, 3);
			vars.origin[2] = FIXED_TO_FLOAT(origin[2], 21, 3);
		}
		if (endIdx) {
			edict_t* copyent = g_demoPlayer->getReplayEntity(endIdx);
			if (copyent) {
				vars.skin = (controller_lo & 0xf000) | ENTINDEX(copyent);
			}
			else {
				ALERT(at_console, "Invalid beam end entity %d", endIdx);
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
			edict_t* copyent = g_demoPlayer->getReplayEntity(aiment);
			if (!copyent) {
				ALERT(at_console, "Invalid aiment %d", aiment);
				vars.movetype = MOVETYPE_NONE;
				return;
			}
			else {
				//vars.aiment = simEnts[aiment];
				// aiment causing hard-to-troubleshoot crashes, so just set origin

				if (!(edflags & (EDFLAG_MONSTER|EDFLAG_PLAYER)) && vars.skin && vars.body) {

					if (!strstr(STRING(copyent->v.model), ".spr")) {
						studiohdr_t* pstudiohdr = (studiohdr_t*)GET_MODEL_PTR(copyent);
						if (pstudiohdr && pstudiohdr->numattachments > vars.body - 1) {
							GET_ATTACHMENT(copyent, vars.body - 1, vars.origin, vars.angles);
						}
						else {
							ALERT(at_console, "Failed to get attachment %d on model %s\n", vars.body - 1, STRING(copyent->v.model));
						}
					}

					vars.skin = 0;
					vars.body = 0;
				}
				else {
					vars.origin = copyent->v.origin;
				}
			}
		}
		else {
			if (edflags & EDFLAG_PLAYER) {
				vars.origin[0] = FIXED_TO_FLOAT(origin[0], 19, 5);
				vars.origin[1] = FIXED_TO_FLOAT(origin[1], 19, 5);
				vars.origin[2] = FIXED_TO_FLOAT(origin[2], 19, 5);
			}
			else {
				vars.origin[0] = FIXED_TO_FLOAT(origin[0], 23, 1);
				vars.origin[1] = FIXED_TO_FLOAT(origin[1], 23, 1);
				vars.origin[2] = FIXED_TO_FLOAT(origin[2], 23, 1);
			}
			
		}

		const float angleConvert = (360.0f / 255.0f);
		vars.angles = Vector((float)angles[0] * angleConvert, (float)angles[1] * angleConvert, (float)angles[2] * angleConvert);
	}

	// calculate instantaneous velocity for gait calculations
	if (edflags & EDFLAG_PLAYER)
		vars.velocity = (vars.origin - oldorigin);
}

bool netedict::readDeltas(mstream& reader) {
	uint32_t deltaBits = 0;
	reader.read(&deltaBits, 1);

	if (deltaBits & FL_BIGENTDELTA) {
		uint32_t upperBits = 0;
		reader.read(&upperBits, 1);
		deltaBits = deltaBits | (upperBits << 8);

		if (deltaBits & FL_BIGGERENTDELTA) {
			upperBits = 0;
			reader.read(&upperBits, 2);
			deltaBits = deltaBits | (upperBits << 16);
			g_stats.entBigUpdates++;

			uint32_t bigUpdateBits = deltaBits & 0xffff0000;
			for (int i = 0; i < 32; i++) {
				uint32_t bit = 1U << i;
				if (bigUpdateBits & bit)
					g_stats.entDeltaBigReason[bitoffset(bit)]++;
			}
		}
		else {
			g_stats.entMedUpdates++;
		}
	}

	g_stats.entUpdateCount++;

	if (deltaBits == 0) {
		edflags = 0;
		deltaBitsLast = 0;
		return false;
	}

	uint8_t oldedflags = edflags;
	uint8_t newedflags = edflags;

	READ_DELTA(reader, deltaBits, FL_DELTA_EDFLAGS, newedflags, 1);

	if (!oldedflags && newedflags) {
		// new entity created. Start deltas from a fresh state.
		reset();
	}

	deltaBitsLast = deltaBits;
	edflags = newedflags;

	int angleSz = edflags & EDFLAG_BEAM ? 3 : 1;

	const uint32_t BIGORIGIN_MASK = FL_BIGGERENTDELTA | FL_DELTA_BIGORIGIN;

	uint32_t originFlags = deltaBits & BIGORIGIN_MASK;

	if (originFlags == BIGORIGIN_MASK) {
		READ_DELTA(reader, deltaBits, FL_DELTA_ORIGIN_X, origin[0], 3);
		READ_DELTA(reader, deltaBits, FL_DELTA_ORIGIN_Y, origin[1], 3);
		READ_DELTA(reader, deltaBits, FL_DELTA_ORIGIN_Z, origin[2], 3);
	}
	else if (originFlags == FL_DELTA_BIGORIGIN || originFlags == FL_BIGGERENTDELTA) {
		int16_t dx = 0, dy = 0, dz = 0;
		READ_DELTA(reader, deltaBits, FL_DELTA_ORIGIN_X, dx, 2);
		READ_DELTA(reader, deltaBits, FL_DELTA_ORIGIN_Y, dy, 2);
		READ_DELTA(reader, deltaBits, FL_DELTA_ORIGIN_Z, dz, 2);
		origin[0] += dx;
		origin[1] += dy;
		origin[2] += dz;
	}
	else {
		int8_t dx = 0, dy = 0, dz = 0;
		READ_DELTA(reader, deltaBits, FL_DELTA_ORIGIN_X, dx, 1);
		READ_DELTA(reader, deltaBits, FL_DELTA_ORIGIN_Y, dy, 1);
		READ_DELTA(reader, deltaBits, FL_DELTA_ORIGIN_Z, dz, 1);
		origin[0] += dx;
		origin[1] += dy;
		origin[2] += dz;
	}
	
	READ_DELTA(reader, deltaBits, FL_DELTA_ANGLES_X, angles[0], angleSz);
	READ_DELTA(reader, deltaBits, FL_DELTA_ANGLES_Y, angles[1], angleSz);
	READ_DELTA(reader, deltaBits, FL_DELTA_ANGLES_Z, angles[2], angleSz);
	READ_DELTA(reader, deltaBits, FL_DELTA_FRAME, frame, 1);
	READ_DELTA(reader, deltaBits, FL_DELTA_CONTROLLER_LO, controller_lo, 2);
	READ_DELTA(reader, deltaBits, FL_DELTA_SEQUENCE, sequence, 1);
	READ_DELTA(reader, deltaBits, FL_DELTA_GAITBLEND, gaitblend, 2);
	READ_DELTA(reader, deltaBits, FL_DELTA_RENDERAMT, renderamt, 1);
	READ_DELTA(reader, deltaBits, FL_DELTA_HEALTH, health, 4);
	READ_DELTA(reader, deltaBits, FL_DELTA_FRAMERATE, framerate, 1);
	READ_DELTA(reader, deltaBits, FL_DELTA_EFFECTS, effects, 2);
	READ_DELTA(reader, deltaBits, FL_DELTA_RENDERCOLOR_0, rendercolor[0], 1);
	READ_DELTA(reader, deltaBits, FL_DELTA_RENDERCOLOR_1, rendercolor[1], 1);
	READ_DELTA(reader, deltaBits, FL_DELTA_RENDERCOLOR_2, rendercolor[2], 1);
	READ_DELTA(reader, deltaBits, FL_DELTA_RENDERMODEFX, rendermodefx, 1);
	READ_DELTA(reader, deltaBits, FL_DELTA_SKIN, skin, 1);
	READ_DELTA(reader, deltaBits, FL_DELTA_BODY, body, 1);
	READ_DELTA(reader, deltaBits, FL_DELTA_SCALE, scale, 2);
	READ_DELTA(reader, deltaBits, FL_DELTA_COLORMAP, colormap, 1);
	READ_DELTA(reader, deltaBits, FL_DELTA_MODELINDEX, modelindex, 2);
	READ_DELTA(reader, deltaBits, FL_DELTA_CONTROLLER_HI, controller_hi, 2);
	READ_DELTA(reader, deltaBits, FL_DELTA_AIMENT, aiment, 2);
	READ_DELTA(reader, deltaBits, FL_DELTA_CLASSIFY, classify, 1);

	return true;
}

int netedict::writeDeltas(mstream& writer, netedict& old) {
	uint64_t startOffset = writer.tell();

	uint32_t deltaBits = 0; // flags which fields were changed

	uint8_t newedflags = edflags;
	bool entityCreated = false;

	if ((old.edflags & EDFLAG_VALID) != (newedflags & EDFLAG_VALID)) {
		if (!(edflags & EDFLAG_VALID)) {
			// 0 deltas indicates edict was deleted
			writer.write(&deltaBits, 1);

			if (writer.eom()) {
				writer.seek(startOffset);
				return EDELTA_OVERFLOW;
			}

			return EDELTA_WRITE;
		}
		if (!old.edflags) {
			// new entity created. Start from a fresh previous state.
			// some vars may have changed while we didn't send deltas but still memcpy()'d
			// to fileedicts as if the client knows the latest state.
			old.reset();
			edflags = newedflags;
			entityCreated = true;
		}
	}

	writer.skip(ENT_DELTA_BYTES); // write delta bits later

	int angleSz = edflags & EDFLAG_BEAM ? 3 : 1;

	bool canWrite16bitOriginDeltas = true;
	bool canWrite8bitOriginDeltas = true;
	for (int i = 0; i < 3; i++) {
		int32_t delta = abs(origin[i] - old.origin[i]);
		if (delta > INT8_MAX) {
			canWrite8bitOriginDeltas = false;
		}
		if (delta > INT16_MAX) {
			canWrite16bitOriginDeltas = false;
			canWrite8bitOriginDeltas = false;
			break;
		}
	}

	// last bit but written first because it decides how other deltas are read
	WRITE_DELTA(writer, deltaBits, FL_DELTA_EDFLAGS, edflags, 1);

	uint32_t originOffset = writer.tell();
	if (canWrite16bitOriginDeltas) {
		if (old.origin[0] != origin[0]) {
			deltaBits |= FL_DELTA_ORIGIN_X;
			g_stats.entDeltaSz[bitoffset(FL_DELTA_ORIGIN_X)] += 2;
			int16_t delta = origin[0] - old.origin[0];
			writer.write((void*)&delta, 2);
		}
		if (old.origin[1] != origin[1]) {
			deltaBits |= FL_DELTA_ORIGIN_Y;
			g_stats.entDeltaSz[bitoffset(FL_DELTA_ORIGIN_Y)] += 2;
			int16_t delta = origin[1] - old.origin[1];
			writer.write((void*)&delta, 2);
		}
		if (old.origin[2] != origin[2]) {
			deltaBits |= FL_DELTA_ORIGIN_Z;
			g_stats.entDeltaSz[bitoffset(FL_DELTA_ORIGIN_Z)] += 2;
			int16_t delta = origin[2] - old.origin[2];
			writer.write((void*)&delta, 2);
		}
	}
	else {
		deltaBits |= FL_DELTA_BIGORIGIN;
		WRITE_DELTA(writer, deltaBits, FL_DELTA_ORIGIN_X, origin[0], 3);
		WRITE_DELTA(writer, deltaBits, FL_DELTA_ORIGIN_Y, origin[1], 3);
		WRITE_DELTA(writer, deltaBits, FL_DELTA_ORIGIN_Z, origin[2], 3);
	}
	WRITE_DELTA(writer, deltaBits, FL_DELTA_ANGLES_X, angles[0], angleSz);
	WRITE_DELTA(writer, deltaBits, FL_DELTA_ANGLES_Y, angles[1], angleSz);
	WRITE_DELTA(writer, deltaBits, FL_DELTA_ANGLES_Z, angles[2], angleSz);
	if (old.frame != frame || forceNextFrame) {
		forceNextFrame = false;
		deltaBits |= FL_DELTA_FRAME;
		g_stats.entDeltaSz[bitoffset(FL_DELTA_FRAME)] += 1;
		writer.write((void*)&frame, 1);
	}
	WRITE_DELTA(writer, deltaBits, FL_DELTA_CONTROLLER_LO, controller_lo, 2);
	WRITE_DELTA(writer, deltaBits, FL_DELTA_SEQUENCE, sequence, 1);
	WRITE_DELTA(writer, deltaBits, FL_DELTA_GAITBLEND, gaitblend, 2);
	WRITE_DELTA(writer, deltaBits, FL_DELTA_RENDERAMT, renderamt, 1);
	WRITE_DELTA(writer, deltaBits, FL_DELTA_HEALTH, health, 4);
	WRITE_DELTA(writer, deltaBits, FL_DELTA_FRAMERATE, framerate, 1);

	WRITE_DELTA(writer, deltaBits, FL_DELTA_EFFECTS, effects, 2);
	WRITE_DELTA(writer, deltaBits, FL_DELTA_RENDERCOLOR_0, rendercolor[0], 1);
	WRITE_DELTA(writer, deltaBits, FL_DELTA_RENDERCOLOR_1, rendercolor[1], 1);
	WRITE_DELTA(writer, deltaBits, FL_DELTA_RENDERCOLOR_2, rendercolor[2], 1);
	WRITE_DELTA(writer, deltaBits, FL_DELTA_RENDERMODEFX, rendermodefx, 1);
	WRITE_DELTA(writer, deltaBits, FL_DELTA_SKIN, skin, 1);
	WRITE_DELTA(writer, deltaBits, FL_DELTA_BODY, body, 1);
	WRITE_DELTA(writer, deltaBits, FL_DELTA_SCALE, scale, 2);
	WRITE_DELTA(writer, deltaBits, FL_DELTA_COLORMAP, colormap, 1);
	WRITE_DELTA(writer, deltaBits, FL_DELTA_MODELINDEX, modelindex, 2);
	WRITE_DELTA(writer, deltaBits, FL_DELTA_CONTROLLER_HI, controller_hi, 2);
	WRITE_DELTA(writer, deltaBits, FL_DELTA_AIMENT, aiment, 2);
	WRITE_DELTA(writer, deltaBits, FL_DELTA_CLASSIFY, classify, 1);

	deltaBitsLast = 0;

	if (writer.eom()) {
		writer.seek(startOffset);
		return EDELTA_OVERFLOW;
	}

	uint64_t endOffset = writer.tell();
	writer.seek(startOffset);

	if (deltaBits == 0) {
		return EDELTA_NONE;
	}

	g_stats.entUpdateCount++;

	if ((deltaBits & 0xffff) == deltaBits && canWrite16bitOriginDeltas) {
		if (canWrite8bitOriginDeltas) {
			// shrink the origin deltas
			writer.seek(originOffset);
			int originPartsWritten = 0;
			if (old.origin[0] != origin[0]) {
				deltaBits |= FL_DELTA_ORIGIN_X;
				g_stats.entDeltaSz[bitoffset(FL_DELTA_ORIGIN_X)] -= 1;
				int8_t delta = origin[0] - old.origin[0];
				writer.write((void*)&delta, 1);
				originPartsWritten++;
			}
			if (old.origin[1] != origin[1]) {
				deltaBits |= FL_DELTA_ORIGIN_Y;
				g_stats.entDeltaSz[bitoffset(FL_DELTA_ORIGIN_Y)] -= 1;
				int8_t delta = origin[1] - old.origin[1];
				writer.write((void*)&delta, 1);
				originPartsWritten++;
			}
			if (old.origin[2] != origin[2]) {
				deltaBits |= FL_DELTA_ORIGIN_Z;
				g_stats.entDeltaSz[bitoffset(FL_DELTA_ORIGIN_Z)] -= 1;
				int8_t delta = origin[2] - old.origin[2];
				writer.write((void*)&delta, 1);
				originPartsWritten++;
			}
			uint32_t smallOriginsEnd = originOffset + originPartsWritten;
			uint32_t oldOriginsEnd = originOffset + (originPartsWritten * 2);
			if (originPartsWritten) {
				memmove(writer.getBuffer() + smallOriginsEnd,
					writer.getBuffer() + oldOriginsEnd,
					endOffset - oldOriginsEnd);
				endOffset -= oldOriginsEnd - smallOriginsEnd;
			}
			writer.seek(startOffset);
		}
		else {
			deltaBits |= FL_DELTA_BIGORIGIN;
		}

		// delta bits can fit into 2 or fewer bytes. Move the deltas back X bytes.
		int deltaBytes = 1;

		if ((deltaBits & 0xff) != deltaBits) {
			deltaBytes = 2;
			deltaBits |= FL_BIGENTDELTA;
			g_stats.entMedUpdates++;
		}

		int moveBytes = endOffset - (startOffset + ENT_DELTA_BYTES);
		memmove(writer.getBuffer() + startOffset + deltaBytes,
			writer.getBuffer() + startOffset + ENT_DELTA_BYTES,
			moveBytes);
		writer.write((void*)&deltaBits, deltaBytes);
		writer.seek(endOffset - (ENT_DELTA_BYTES - deltaBytes));
	}
	else {
		if (canWrite16bitOriginDeltas && !entityCreated) {
			uint32_t bigUpdateBits = deltaBits & 0xffff0000;
			for (int i = 0; i < 32; i++) {
				uint32_t bit = 1U << i;
				if (bigUpdateBits & bit)
					g_stats.entDeltaBigReason[bitoffset(bit)]++;
			}
		}

		deltaBits |= FL_BIGENTDELTA | FL_BIGGERENTDELTA;
		writer.write((void*)&deltaBits, ENT_DELTA_BYTES);
		writer.seek(endOffset);
		g_stats.entBigUpdates++;
	}

	deltaBitsLast = deltaBits;

	return EDELTA_WRITE;
}
