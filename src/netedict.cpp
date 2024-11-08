#include "netedict.h"
#include "DemoFile.h"
#include "main.h"
#include "DemoPlayer.h"

using namespace std;

#undef read
#undef write

#define WRITE_DELTA_BITS(cat, field, sz) \
	writer.writeBit(old.field != field); \
	g_stats.entDeltaCatSz[cat] += 1; \
	if (old.field != field) { \
		g_stats.entDeltaCatSz[cat] += sz; \
		writer.writeBits(field, sz); \
	}

#define READ_DELTA_BITS(cat, field, sz) \
	g_stats.entDeltaCatSz[cat] += 1; \
	if (reader.readBit()) { \
		field = reader.readBits(sz); \
		g_stats.entDeltaCatSz[cat] += sz; \
	}

#define CHECK_CHANGED(field) if (old.field != field) categoryFlag = true;

#define CHECK_MATCH(field) \
	if (field != other.field) { \
		ALERT(at_console, "Mismatch " #field " (%d != %d)\n", (int)field, (int)other.field); \
		return false; \
	}

netedict::netedict() {
	reset();
}

void netedict::reset() {
	memset(this, 0, sizeof(netedict));
}

bool netedict::matches(netedict& other) {
	CHECK_MATCH(edflags);

	if (!FIXED_EQUALS(origin[0], other.origin[0], 24)) {
		ALERT(at_console, "Mismatch origin[0] (%d != %d)\n", origin[0], other.origin[0]);
		return false;
	}
	if (!FIXED_EQUALS(origin[1], other.origin[1], 24)) {
		ALERT(at_console, "Mismatch origin[1] (%d != %d)\n", origin[1], other.origin[1]);
		return false;
	}
	if (!FIXED_EQUALS(origin[2], other.origin[2], 24)) {
		ALERT(at_console, "Mismatch origin[2] (%d != %d)\n", origin[2], other.origin[2]);
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

	CHECK_MATCH(modelindex);
	CHECK_MATCH(skin);
	CHECK_MATCH(body);
	CHECK_MATCH(effects);
	CHECK_MATCH(modelindex);
	CHECK_MATCH(sequence);
	CHECK_MATCH(gait);
	CHECK_MATCH(blend);
	CHECK_MATCH(frame);
	CHECK_MATCH(framerate);
	CHECK_MATCH(controller_lo);
	CHECK_MATCH(controller_hi);
	CHECK_MATCH(scale);
	CHECK_MATCH(renderamt);
	CHECK_MATCH(rendercolor[0]);
	CHECK_MATCH(rendercolor[1]);
	CHECK_MATCH(rendercolor[2]);
	CHECK_MATCH(rendermode);
	CHECK_MATCH(renderfx);
	CHECK_MATCH(aiment);
	CHECK_MATCH(health);
	CHECK_MATCH(colormap);
	CHECK_MATCH(health);
	CHECK_MATCH(classify);
	CHECK_MATCH(classname);
	CHECK_MATCH(schedule);
	CHECK_MATCH(monsterstate);
	CHECK_MATCH(task);
	CHECK_MATCH(conditions_lo);
	CHECK_MATCH(conditions_md);
	CHECK_MATCH(conditions_hi);
	CHECK_MATCH(memories);
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
	gait = vars.gaitsequence;
	blend = vars.blending[0];
	int8_t newFramerate = clamp(vars.framerate * 16.0f, INT8_MIN, INT8_MAX);
	uint8_t newSequence = vars.sequence;
	uint16_t newModelindex = vars.modelindex;

	memcpy(&controller_lo, vars.controller, 4);

	scale = clamp(vars.scale * 256.0f, 0, UINT16_MAX);
	rendermode = vars.rendermode & 0x7; // only first 3 bits are valid modes
	renderfx = vars.renderfx & 0x31; // only first 5 bits are valid modes
	renderamt = vars.renderamt;
	rendercolor[0] = vars.rendercolor[0];
	rendercolor[1] = vars.rendercolor[1];
	rendercolor[2] = vars.rendercolor[2];
	aiment = vars.aiment ? ENTINDEX(vars.aiment) : 0;
	colormap = vars.colormap;

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
	if (animationReset || isBspModel || framerate == 0) {
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

	/*
	if ((vars.flags & FL_GODMODE) || vars.takedamage == DAMAGE_NO) {
		edflags |= EDFLAG_GOD;
	}

	if (vars.flags & FL_NOTARGET) {
		edflags |= EDFLAG_NOTARGET;
	}
	*/

	if (g_write_debug_info->value && (ed.v.flags & (FL_CLIENT | FL_MONSTER))) {
		CBaseEntity* bent = (CBaseEntity*)GET_PRIVATE(&ed); // TODO: not thread safe
		health = vars.health > 0 ? V_min(vars.health, UINT32_MAX) : 0;
		
		if (g_write_debug_info->value && bent->IsNormalMonster()) {
			CBaseMonster* mon = bent->MyMonsterPointer();

			monsterstate = mon->m_MonsterState;
			schedule = mon->GetScheduleTableIdx();
			task = mon->m_iScheduleIndex;
			conditions_lo = mon->m_afConditions & 0xff;
			conditions_md = (mon->m_afConditions >> 8) & 0xff;
			conditions_hi = mon->m_afConditions >> 16;
			memories = mon->m_afMemory;
		}

#ifdef HLCOOP_BUILD
		uint8_t classifyBits = bent && bent->m_Classify ? bent->m_Classify : 0;
#else
		uint8_t classifyBits = bent && bent->m_fOverrideClass ? bent->m_iClassSelection : 0;
#endif
		
		classify = classifyBits;
	}

	if (classname_stringt != vars.classname) {
		classname_stringt = vars.classname;
		classname = getPoolOffsetForString(vars.classname);
	}
}

void netedict::apply(edict_t* ed, char* stringpool) {
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
	vars.gaitsequence = gait;
	vars.frame = frame;
	vars.framerate = framerate / 16.0f;
	memcpy(vars.controller, &controller_lo, 4);
	vars.blending[0] = blend;
	vars.scale = scale / 256.0f;
	vars.rendermode = rendermode;
	vars.renderamt = renderamt;
	vars.rendercolor[0] = rendercolor[0];
	vars.rendercolor[1] = rendercolor[1];
	vars.rendercolor[2] = rendercolor[2];
	vars.renderfx = renderfx;
	vars.colormap = colormap;
	vars.health = health;
	vars.playerclass = classify;
	//vars.flags |= (edflags & EDFLAG_GOD) ? FL_GODMODE : 0;
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

	UTIL_SetOrigin(&vars, vars.origin);

	// calculate instantaneous velocity for gait calculations
	if (edflags & EDFLAG_PLAYER)
		vars.velocity = (vars.origin - oldorigin);

	vars.noise = MAKE_STRING(stringpool + classname);

	if (baseent->IsMonster() && (edflags & EDFLAG_MONSTER) && classname) {
		CBaseMonster* mon = baseent->MyMonsterPointer();
		
		edict_t* temp = CREATE_NAMED_ENTITY(MAKE_STRING(stringpool + classname));
		CBaseEntity* ent = CBaseEntity::Instance(temp);
		if (ent && ent->IsNormalMonster() && schedule != 127) {
			CBaseMonster* tempmon = ent->MyMonsterPointer();
			mon->m_pSchedule = tempmon->ScheduleFromTableIdx(schedule);
			if (!mon->m_pSchedule) {
				ALERT(at_console, "Schedule %d does not exist for %s (%d schedules total)\n", (int)schedule, stringpool + classname, tempmon->GetScheduleTableSize());
			}
			mon->m_iScheduleIndex = mon->m_iScheduleIndex;
			mon->pev->iuser3 = 1337; // HACK: this entity is a normal monster, not just a cycler
			mon->pev->nextthink = 0;
			
		}
		if (temp) {
			REMOVE_ENTITY(temp);
			temp->freetime = 0; // allow the slot to be used right away
		}

		mon->m_MonsterState = (MONSTERSTATE)monsterstate;
		mon->m_afConditions = ((uint32_t)conditions_hi << 16) | ((uint32_t)conditions_md << 8) | (uint32_t)conditions_lo;
		mon->m_afMemory = memories;
	}
}

bool netedict::readDeltas(mstream& reader) {
	uint8_t oldedflags = edflags;
	uint8_t newedflags = edflags;

	deltaBitsLast = 0;

	if (reader.readBit()) {
		deltaBitsLast |= FL_DELTA_FLAGS_CHANGED;
		newedflags = reader.readBits(4);
		g_stats.entDeltaCatSz[FL_DELTA_CAT_EDFLAG] += 4;
	}

	if (!oldedflags && newedflags) {
		// new entity created. Start deltas from a fresh state.
		reset();
	}

	edflags = newedflags;

	// origin category
	if (reader.readBit()) {
		deltaBitsLast |= FL_DELTA_ORIGIN_CHANGED;

		for (int i = 0; i < 3; i++) {
			uint8_t coordSz = reader.readBits(2);

			if (coordSz == 0) {
				continue;
			}
			else if (coordSz == 1) {
				origin[i] += SIGN_EXTEND_FIXED(reader.readBits(8), 8);
				g_stats.entDeltaCatSz[FL_DELTA_CAT_ORIGIN] += 10;
			}
			else if (coordSz == 2) {
				origin[i] += SIGN_EXTEND_FIXED(reader.readBits(14), 14);
				g_stats.entDeltaCatSz[FL_DELTA_CAT_ORIGIN] += 16;
			}
			else if (coordSz == 3) {
				origin[i] = reader.readBits(32);
				g_stats.entDeltaCatSz[FL_DELTA_CAT_ORIGIN] += 34;
			}
		}
	}

	// angles category
	if (reader.readBit()) {
		deltaBitsLast |= FL_DELTA_ANGLES_CHANGED;

		bool bigAngles = reader.readBit();

		for (int i = 0; i < 3; i++) {
			if (bigAngles) {
				READ_DELTA_BITS(FL_DELTA_CAT_ANGLES, angles[i], 32);
			}
			else {
				READ_DELTA_BITS(FL_DELTA_CAT_ANGLES, angles[i], 8);
			}
		}
	}

	// animation deltas category
	if (reader.readBit()) {
		uint8_t oldFrame = frame;
		uint8_t oldSequence = sequence;
		uint8_t oldFramerate = framerate;

		READ_DELTA_BITS(FL_DELTA_CAT_ANIM, controller_lo, 16);
		READ_DELTA_BITS(FL_DELTA_CAT_ANIM, controller_hi, 16);
		READ_DELTA_BITS(FL_DELTA_CAT_ANIM, sequence, 8);
		READ_DELTA_BITS(FL_DELTA_CAT_ANIM, gait, 8);
		READ_DELTA_BITS(FL_DELTA_CAT_ANIM, blend, 8);
		READ_DELTA_BITS(FL_DELTA_CAT_ANIM, framerate, 8);
		READ_DELTA_BITS(FL_DELTA_CAT_ANIM, frame, 8);

		if (oldFrame != frame || oldSequence != sequence || oldFramerate != framerate) {
			deltaBitsLast |= FL_DELTA_ANIM_CHANGED;
		}
	}

	// rendering deltas category
	if (reader.readBit()) {
		READ_DELTA_BITS(FL_DELTA_CAT_RENDER, renderamt, 8);
		READ_DELTA_BITS(FL_DELTA_CAT_RENDER, rendercolor[0], 8);
		READ_DELTA_BITS(FL_DELTA_CAT_RENDER, rendercolor[1], 8);
		READ_DELTA_BITS(FL_DELTA_CAT_RENDER, rendercolor[2], 8);
		READ_DELTA_BITS(FL_DELTA_CAT_RENDER, rendermode, 3);
		READ_DELTA_BITS(FL_DELTA_CAT_RENDER, renderfx, 5);
		READ_DELTA_BITS(FL_DELTA_CAT_RENDER, effects, 16);
		READ_DELTA_BITS(FL_DELTA_CAT_RENDER, scale, 16);
	}

	// misc category (infrequently updated)
	if (reader.readBit()) {
		READ_DELTA_BITS(FL_DELTA_CAT_MISC, skin, 8);
		READ_DELTA_BITS(FL_DELTA_CAT_MISC, body, 8);
		READ_DELTA_BITS(FL_DELTA_CAT_MISC, colormap, 8);
		READ_DELTA_BITS(FL_DELTA_CAT_MISC, aiment, 16);
		READ_DELTA_BITS(FL_DELTA_CAT_MISC, modelindex, MODEL_BITS);
	}

	// internal state category
	if (reader.readBit()) {
		READ_DELTA_BITS(FL_DELTA_CAT_INTERNAL, classname, 12);
		READ_DELTA_BITS(FL_DELTA_CAT_INTERNAL, monsterstate, 4);
		READ_DELTA_BITS(FL_DELTA_CAT_INTERNAL, schedule, 7);
		READ_DELTA_BITS(FL_DELTA_CAT_INTERNAL, task, 5);
		READ_DELTA_BITS(FL_DELTA_CAT_INTERNAL, conditions_lo, 8);
		READ_DELTA_BITS(FL_DELTA_CAT_INTERNAL, conditions_md, 8);
		READ_DELTA_BITS(FL_DELTA_CAT_INTERNAL, conditions_hi, 16);
		READ_DELTA_BITS(FL_DELTA_CAT_INTERNAL, memories, 12);

		if (reader.readBit()) {
			if (reader.readBit()) {
				health = reader.readBits(32);
				g_stats.entDeltaCatSz[FL_DELTA_CAT_INTERNAL] += 32 + 2;
			}
			else {
				health = reader.readBits(10);
				g_stats.entDeltaCatSz[FL_DELTA_CAT_INTERNAL] += 10 + 2;
			}
		}
	}

	g_stats.entUpdateCount++;

	return reader.eom();
}

int netedict::writeDeltas(mstream& writer, netedict& old) {
	uint64_t startOffset = writer.tellBits();

	if (!old.edflags) {
		// new entity created. Start from a fresh previous state.
		// some vars may have changed while we didn't send deltas but still memcpy()'d
		// to fileedicts as if the client knows the latest state.
		old.reset();
	}

	bool wroteAnyDeltas = false;

	// edflags must come first, so the reader can know to reset state when new ents are created.
	// TODO: really? can't you know that by checking the old flags in the reader?
	if (edflags != old.edflags) {
		wroteAnyDeltas = true;
		writer.writeBit(1);
		
		if (edflags & EDFLAG_VALID) {
			writer.writeBits(edflags, 4);
		}
		else {
			writer.writeBits(0, 4); // 0 flags = deleted ent
		}

		g_stats.entDeltaCatSz[FL_DELTA_CAT_EDFLAG] += 4;
	}
	else {
		writer.writeBit(0);
	}

	// origin category
	{
		int32_t dx = origin[0] - old.origin[0];
		int32_t dy = origin[1] - old.origin[1];
		int32_t dz = origin[2] - old.origin[2];

		bool originChanged = dx || dy || dz;

		writer.writeBit(originChanged);
		if (originChanged) {
			wroteAnyDeltas = true;

			for (int i = 0; i < 3; i++) {
				int32_t d = origin[i] - old.origin[i];

				if (d == 0) {
					writer.writeBits(0, 2);
					g_stats.entDeltaCatSz[FL_DELTA_CAT_ORIGIN] += 2;
				}
				else if (abs(d) < 128) { // for small movements
					writer.writeBits(1, 2);
					writer.writeBits(d, 8);
					g_stats.entDeltaCatSz[FL_DELTA_CAT_ORIGIN] += 10;
				}
				else if (abs(d) < 2048) { // can handle the speed of rockets and gauss jumps
					writer.writeBits(2, 2);
					writer.writeBits(d, 14);
					g_stats.entDeltaCatSz[FL_DELTA_CAT_ORIGIN] += 16;
				}
				else {
					writer.writeBits(3, 2);
					writer.writeBits(origin[i], 32);
					g_stats.entDeltaCatSz[FL_DELTA_CAT_ORIGIN] += 34;
				}
			}
		}
	}

	// angles category
	{
		bool categoryFlag = false;
		CHECK_CHANGED(angles[0]);
		CHECK_CHANGED(angles[1]);
		CHECK_CHANGED(angles[2]);

		writer.writeBit(categoryFlag);
		if (categoryFlag) {
			wroteAnyDeltas = true;

			bool bigAngles = edflags & EDFLAG_BEAM;
			writer.writeBit(bigAngles);

			for (int i = 0; i < 3; i++) {
				if (bigAngles) {
					WRITE_DELTA_BITS(FL_DELTA_CAT_ANGLES, angles[i], 32);
				}
				else {
					WRITE_DELTA_BITS(FL_DELTA_CAT_ANGLES, angles[i], 8);
				}
			}
		}
	}

	// animation deltas category
	{
		bool categoryFlag = forceNextFrame;
		CHECK_CHANGED(frame);
		CHECK_CHANGED(controller_lo);
		CHECK_CHANGED(controller_hi);
		CHECK_CHANGED(sequence);
		CHECK_CHANGED(gait);
		CHECK_CHANGED(blend);
		CHECK_CHANGED(framerate);

		writer.writeBit(categoryFlag);
		if (categoryFlag) {
			wroteAnyDeltas = true;

			WRITE_DELTA_BITS(FL_DELTA_CAT_ANIM, controller_lo, 16);
			WRITE_DELTA_BITS(FL_DELTA_CAT_ANIM, controller_hi, 16);
			WRITE_DELTA_BITS(FL_DELTA_CAT_ANIM, sequence, 8);
			WRITE_DELTA_BITS(FL_DELTA_CAT_ANIM, gait, 8);
			WRITE_DELTA_BITS(FL_DELTA_CAT_ANIM, blend, 8);
			WRITE_DELTA_BITS(FL_DELTA_CAT_ANIM, framerate, 8);

			bool sendFrame = old.frame != frame || forceNextFrame;
			writer.writeBit(sendFrame);
			if (sendFrame) {
				writer.writeBits(frame, 8);
			}
		}
	}

	// rendering deltas category
	{
		bool categoryFlag = false;
		CHECK_CHANGED(renderamt);
		CHECK_CHANGED(rendercolor[0]);
		CHECK_CHANGED(rendercolor[1]);
		CHECK_CHANGED(rendercolor[2]);
		CHECK_CHANGED(rendermode);
		CHECK_CHANGED(renderfx);
		CHECK_CHANGED(effects);
		CHECK_CHANGED(scale);

		writer.writeBit(categoryFlag);
		if (categoryFlag) {
			wroteAnyDeltas = true;
			WRITE_DELTA_BITS(FL_DELTA_CAT_RENDER, renderamt, 8);
			WRITE_DELTA_BITS(FL_DELTA_CAT_RENDER, rendercolor[0], 8);
			WRITE_DELTA_BITS(FL_DELTA_CAT_RENDER, rendercolor[1], 8);
			WRITE_DELTA_BITS(FL_DELTA_CAT_RENDER, rendercolor[2], 8);
			WRITE_DELTA_BITS(FL_DELTA_CAT_RENDER, rendermode, 3);
			WRITE_DELTA_BITS(FL_DELTA_CAT_RENDER, renderfx, 5);
			WRITE_DELTA_BITS(FL_DELTA_CAT_RENDER, effects, 16);
			WRITE_DELTA_BITS(FL_DELTA_CAT_RENDER, scale, 16);
		}
	}

	// misc category (infrequently updated)
	{
		bool categoryFlag = false;
		CHECK_CHANGED(skin);
		CHECK_CHANGED(body);
		CHECK_CHANGED(colormap);
		CHECK_CHANGED(aiment);
		CHECK_CHANGED(modelindex);

		writer.writeBit(categoryFlag);
		if (categoryFlag) {
			wroteAnyDeltas = true;

			WRITE_DELTA_BITS(FL_DELTA_CAT_MISC, skin, 8);
			WRITE_DELTA_BITS(FL_DELTA_CAT_MISC, body, 8);
			WRITE_DELTA_BITS(FL_DELTA_CAT_MISC, colormap, 8);
			WRITE_DELTA_BITS(FL_DELTA_CAT_MISC, aiment, 16);
			WRITE_DELTA_BITS(FL_DELTA_CAT_MISC, modelindex, MODEL_BITS);
		}
	}

	// internal state category
	{
		bool categoryFlag = false;
		CHECK_CHANGED(classname);
		CHECK_CHANGED(monsterstate);
		CHECK_CHANGED(schedule);
		CHECK_CHANGED(task);
		CHECK_CHANGED(conditions_lo);
		CHECK_CHANGED(conditions_md);
		CHECK_CHANGED(conditions_hi);
		CHECK_CHANGED(memories);
		CHECK_CHANGED(health);

		writer.writeBit(categoryFlag);
		if (categoryFlag) {
			wroteAnyDeltas = true;

			WRITE_DELTA_BITS(FL_DELTA_CAT_INTERNAL, classname, 12);
			WRITE_DELTA_BITS(FL_DELTA_CAT_INTERNAL, monsterstate, 4);
			WRITE_DELTA_BITS(FL_DELTA_CAT_INTERNAL, schedule, 7);
			WRITE_DELTA_BITS(FL_DELTA_CAT_INTERNAL, task, 5);
			WRITE_DELTA_BITS(FL_DELTA_CAT_INTERNAL, conditions_lo, 8);
			WRITE_DELTA_BITS(FL_DELTA_CAT_INTERNAL, conditions_md, 8);
			WRITE_DELTA_BITS(FL_DELTA_CAT_INTERNAL, conditions_hi, 16);
			WRITE_DELTA_BITS(FL_DELTA_CAT_INTERNAL, memories, 12);

			bool healthChanged = old.health != health;
			writer.writeBit(healthChanged);
			if (healthChanged) {
				if (health > 1023) {
					writer.writeBit(1);
					writer.writeBits(health, 32);
					g_stats.entDeltaCatSz[FL_DELTA_CAT_INTERNAL] += 32 + 2;
				}
				else {
					writer.writeBit(0);
					writer.writeBits(health, 10);
					g_stats.entDeltaCatSz[FL_DELTA_CAT_INTERNAL] += 10 + 2;
				}
			}
		}
	}

	if (writer.eom()) {
		return EDELTA_OVERFLOW;
	}

	if (!wroteAnyDeltas) {
		writer.seekBits(startOffset);
		return EDELTA_NONE;
	}

	return EDELTA_WRITE;
}
