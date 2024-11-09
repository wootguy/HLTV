#include "netedict.h"
#include "DemoFile.h"
#include "main.h"
#include "DemoPlayer.h"
#include "pm_shared.h"

using namespace std;

#define BEGIN_DELTA_CATEGORY_COND(cat, cond) \
	if (cond) { \
		uint64_t start_##cat = writer.tellBits(); \
		bool filled_##cat = false; \
		writer.writeBit(0);

#define BEGIN_DELTA_CATEGORY(cat) BEGIN_DELTA_CATEGORY_COND(cat, true)

#define MARK_CATEGORY_FILLED(cat) \
	filled_##cat = true; \
	wroteAnyDeltas = true; \

#define WRITE_DELTA_SINGLE(cat, field, sz) \
	writer.writeBit(old.field != field); \
	g_stats.entDeltaCatSz[cat] += 1; \
	if (old.field != field) { \
		writer.writeBits(field, sz); \
		g_stats.entDeltaCatSz[cat] += sz; \
	}

#define WRITE_DELTA_COND(cat, field, sz, cond) \
	writer.writeBit(cond); \
	if (cond) { \
		MARK_CATEGORY_FILLED(cat) \
		writer.writeBits(field, sz); \
	}

#define WRITE_DELTA_STR(cat, field) \
	if (strcmp(old.field, field)) { \
		MARK_CATEGORY_FILLED(cat) \
		uint8_t len = strlen(field); \
		writer.writeBit(1); \
		writer.writeBits(len, 8); \
		for (int z = 0; z < len; z++) { \
			writer.writeBits(field[z], 8); \
		} \
	} else { \
		writer.writeBit(0); \
	}

#define WRITE_DELTA(cat, field, sz) \
	WRITE_DELTA_COND(cat, field, sz, old.field != field)

#define END_DELTA_CATEGORY(cat) \
		if (filled_##cat) { \
			uint64_t end_##cat = writer.tellBits(); \
			writer.seekBits(start_##cat); \
			writer.writeBit(1); \
			writer.seekBits(end_##cat); \
			g_stats.entDeltaCatSz[cat] += end_##cat - start_##cat; \
		} else { \
			writer.seekBits(start_##cat + 1); \
		} \
	}

#define END_DELTA_CATEGROY_NESTED(cat, parentcat) \
		if (filled_##cat) { \
			MARK_CATEGORY_FILLED(parentcat) \
			uint64_t end_##cat = writer.tellBits(); \
			writer.seekBits(start_##cat); \
			writer.writeBit(1); \
			writer.seekBits(end_##cat); \
			g_stats.entDeltaCatSz[cat] += end_##cat - start_##cat; \
		} else { \
			writer.seekBits(start_##cat + 1); \
		} \
	}

#define READ_DELTA(cat, field, sz) \
	g_stats.entDeltaCatSz[cat] += 1; \
	if (reader.readBit()) { \
		field = reader.readBits(sz); \
		g_stats.entDeltaCatSz[cat] += sz; \
	}

#define READ_DELTA_STR(cat, field) \
	g_stats.entDeltaCatSz[cat] += 1; \
	if (reader.readBit()) { \
		uint8_t len = reader.readBits(8); \
		if (len >= sizeof(field)) { \
			ALERT(at_console, "Invalid " #field " length %d\n", (int)len); \
			len = sizeof(field)-1; \
		} \
		for (int z = 0; z < (int)len; z++) { \
			field[z] = reader.readBits(8); \
		} \
		field[len] = 0; \
		g_stats.entDeltaCatSz[cat] += 8 + len*8; \
	}

#define CHECK_MATCH(field) \
	if (field != other.field) { \
		ALERT(at_console, "Mismatch " #field " (%d != %d)\n", (int)field, (int)other.field); \
		return false; \
	}

#define CHECK_MATCH_STR(field) \
	if (strcmp(field, other.field)) { \
		ALERT(at_console, "Mismatch " #field " (%s != %s)\n", field, other.field); \
		return false; \
	}

netedict::netedict() {
	reset();
}

void netedict::reset() {
	memset(this, 0, sizeof(netedict));
}

bool netedict::matches(netedict& other) {
	CHECK_MATCH(etype);

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
	//CHECK_MATCH(classify);
	CHECK_MATCH(classname);
	CHECK_MATCH(schedule);
	CHECK_MATCH(monsterstate);
	CHECK_MATCH(task);
	CHECK_MATCH(conditions_lo);
	CHECK_MATCH(conditions_md);
	CHECK_MATCH(conditions_hi);
	CHECK_MATCH(memories);
	CHECK_MATCH(visibility[0]);
	CHECK_MATCH(visibility[1]);
	CHECK_MATCH(visibility[2]);
	CHECK_MATCH(visibility[3]);
	CHECK_MATCH(steamid64);
	CHECK_MATCH_STR(name);
	CHECK_MATCH_STR(model);
	CHECK_MATCH(topColor);
	CHECK_MATCH(bottomColor);
	CHECK_MATCH(frags);
	CHECK_MATCH(button);
	CHECK_MATCH(armorvalue);
	CHECK_MATCH(ping);
	CHECK_MATCH(punchangle[0]);
	CHECK_MATCH(punchangle[1]);
	CHECK_MATCH(punchangle[2]);
	CHECK_MATCH(playerFlags);
	CHECK_MATCH(viewEnt);
	CHECK_MATCH(viewmodel);
	CHECK_MATCH(weaponmodel);
	CHECK_MATCH(view_ofs);
	CHECK_MATCH(fov);
	CHECK_MATCH(specTarget);
	CHECK_MATCH(specMode);
	CHECK_MATCH(weaponId);
	CHECK_MATCH(deadFlag);
	CHECK_MATCH(clip);
	CHECK_MATCH(clip2);
	CHECK_MATCH(ammo);
	CHECK_MATCH(ammo2);
	CHECK_MATCH(weaponanim);
	CHECK_MATCH(weaponState);
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
	uint16_t newModelindex = vars.modelindex & ((1 << MODEL_BITS) - 1);

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

	etype = ETYPE_GENERIC;

	if (ed.v.flags & FL_CLIENT) {
		origin[0] = FLOAT_TO_FIXED(ed.v.origin[0], 19, 5);
		origin[1] = FLOAT_TO_FIXED(ed.v.origin[1], 19, 5);
		origin[2] = FLOAT_TO_FIXED(ed.v.origin[2], 19, 5);
		angles[0] = (uint16_t)(normalizeRangef(vars.v_angle.x, 0, 360) * (255.0f / 360.0f));
		angles[1] = (uint16_t)(normalizeRangef(vars.v_angle.y, 0, 360) * (255.0f / 360.0f));
		angles[2] = (uint16_t)(normalizeRangef(vars.v_angle.z, 0, 360) * (255.0f / 360.0f));
		etype = ETYPE_PLAYER;
	}
	else if (ed.v.flags & FL_CUSTOMENTITY) {
		etype = ETYPE_BEAM;
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
			etype = ETYPE_MONSTER;
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

	if (g_write_debug_info->value && ent && (ed.v.flags & (FL_CLIENT | FL_MONSTER))) {
		health = vars.health > 0 ? V_min(vars.health, UINT32_MAX) : 0;
		
		if (g_write_debug_info->value && ent->IsNormalMonster()) {
			CBaseMonster* mon = ent->MyMonsterPointer();

			monsterstate = mon->m_MonsterState;
			schedule = mon->GetScheduleTableIdx();
			if (schedule == 255) {
				schedule = 127; // reduced bit count
			}
			else if (schedule > 127) {
				ALERT(at_console, "Schedule too large for datatype! %d\n", mon->GetScheduleTableIdx());
			}
			task = mon->m_iScheduleIndex;
			conditions_lo = mon->m_afConditions & 0xff;
			conditions_md = (mon->m_afConditions >> 8) & 0xff;
			conditions_hi = mon->m_afConditions >> 16;
			memories = mon->m_afMemory;
		}

#ifdef HLCOOP_BUILD
		uint8_t classifyBits = ent && ent->m_Classify ? ent->m_Classify : 0;
#else
		uint8_t classifyBits = ent && ent->m_fOverrideClass ? ent->m_iClassSelection : 0;
#endif
		
		classify = classifyBits;
	}

	if (classname_stringt != vars.classname) {
		classname_stringt = vars.classname;
		classname = getPoolOffsetForString(vars.classname);
	}

	if (ent) {
		visibility[0] = ent->m_netPlayers >> 0;
		visibility[1] = ent->m_netPlayers >> 8;
		visibility[2] = ent->m_netPlayers >> 16;
		visibility[3] = ent->m_netPlayers >> 24;
	}

	if (ent->IsPlayer()) {
		loadPlayer((CBasePlayer*)ent);
	}
}

// definitely not thread safe
void netedict::loadPlayer(CBasePlayer* plr) {
	edict_t* ent = plr->edict();

	const char* vmodel = STRING(ent->v.viewmodel);
	const char* pmodel = STRING(ent->v.weaponmodel);

	armorvalue = clamp(ent->v.armorvalue + 0.5f, 0, UINT16_MAX);
	fov = clamp(ent->v.fov + 0.5f, 0, 255);
	frags = clamp(ent->v.frags, INT16_MIN, INT16_MAX);
	punchangle[0] = clamp(ent->v.punchangle[0] * 8, INT16_MIN, INT16_MAX);
	punchangle[1] = clamp(ent->v.punchangle[1] * 8, INT16_MIN, INT16_MAX);
	punchangle[2] = clamp(ent->v.punchangle[2] * 8, INT16_MIN, INT16_MAX);
	viewmodel = MODEL_INDEX(g_precachedModels.find(vmodel) != g_precachedModels.end() ? vmodel : NOT_PRECACHED_MODEL);
	weaponmodel = MODEL_INDEX(g_precachedModels.find(pmodel) != g_precachedModels.end() ? pmodel : NOT_PRECACHED_MODEL);
	weaponanim = ent->v.weaponanim;
	view_ofs = clamp(ent->v.view_ofs[2] * 16, INT16_MIN, INT16_MAX);
	specMode = ent->v.iuser1;
	specTarget = ent->v.iuser2;
	viewEnt = plr->m_hViewEntity ? ENTINDEX(plr->m_hViewEntity.GetEdict()) : 0;
	deadFlag = ent->v.deadflag;
	button = (plr->m_afButtonLast | plr->m_afButtonPressed | plr->m_afButtonReleased | ent->v.button) & 0xffff;

	if (pmodel[0] == '\0') {
		weaponmodel = PLR_NO_WEAPON_MODEL;
	}
	if (vmodel[0] == '\0') {
		viewmodel = PLR_NO_WEAPON_MODEL;
	}

	CBasePlayerWeapon* wep = plr->m_pActiveItem ? plr->m_pActiveItem->GetWeaponPtr() : NULL;

	if (wep) {
		int ammoidx = wep->PrimaryAmmoIndex();
		int ammoidx2 = wep->SecondaryAmmoIndex();

		weaponId = wep->m_iId;
		clip = wep->m_iClip;
		clip2 = 0;
		ammo = ammoidx != -1 ? plr->m_rgAmmo[ammoidx] : 0;
		ammo2 = ammoidx2 != -1 ? plr->m_rgAmmo[ammoidx2] : 0;
		chargeReady = wep->m_chargeReady;
		inAttack = (int)wep->m_fInAttack;
		inReload = (int)wep->m_fInReload;
		inReloadSpecial = (int)wep->m_fInSpecialReload;
		fireState = (int)wep->m_fireState;
		weaponState = PACK_WEAPON_STATE(fireState, inReloadSpecial, inReload, inAttack, chargeReady);
	}
	else {
		weaponId = 0;
		clip = 0;
		clip2 = 0;
		ammo = 0;
		ammo2 = 0;
		chargeReady = 0;
		inAttack = 0;
		inReload = 0;
		inReloadSpecial = 0;
		fireState = 0;
	}

	char* info = g_engfuncs.pfnGetInfoKeyBuffer(ent);
	char* infomodel = g_engfuncs.pfnInfoKeyValue(info, "model");
	topColor = atoi(g_engfuncs.pfnInfoKeyValue(info, "topcolor"));
	bottomColor = atoi(g_engfuncs.pfnInfoKeyValue(info, "bottomcolor"));
	strcpy_safe(model, infomodel, 23);
	strcpy_safe(name, plr->DisplayName(), 32);

	if (gpGlobals->time - lastPingTime >= 1.0f) {
		lastPingTime = gpGlobals->time;
		int iping;
		int iloss;
		g_engfuncs.pfnGetPlayerStats(ent, &iping, &iloss);
		ping = clamp(iping, 0, 65535);
	}

	if (steamid64 == 0) {
		playerFlags |= PLR_FL_CONNECTED;
		steamid64 = getPlayerCommunityId(ent);
	}
	else if (!IsValidPlayer(ent)) {
		playerFlags = 0;
		steamid64 = 0;
	}

	if (playerFlags & PLR_FL_CONNECTED) {
		int fl = ent->v.flags;

		playerFlags = (fl & FL_INWATER ? PLR_FL_INWATER : 0)
			| (fl & (FL_ONGROUND | FL_PARTIALGROUND) ? PLR_FL_ONGROUND : 0)
			| (fl & FL_WATERJUMP ? PLR_FL_WATERJUMP : 0)
			| (fl & FL_FROZEN ? PLR_FL_FROZEN : 0)
			| (fl & FL_DUCKING ? PLR_FL_DUCKING : 0)
			| PLR_FL_CONNECTED;
	}
}

void netedict::apply(edict_t* ed, char* stringpool) {
	entvars_t& vars = ed->v;

	if (!etype) {
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

	if (etype == ETYPE_BEAM) {
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

				if (!(etype == ETYPE_MONSTER || etype == ETYPE_PLAYER) && vars.skin && vars.body) {

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
			if (etype == ETYPE_PLAYER) {
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
	if (etype == ETYPE_PLAYER)
		vars.velocity = (vars.origin - oldorigin);

	vars.noise = MAKE_STRING(stringpool + classname);

	if (baseent->IsMonster() && (etype == ETYPE_MONSTER) && classname) {
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

	if (baseent->IsPlayer()) {
		applyPlayer((CBasePlayer*)baseent);
	}
}

void netedict::applyPlayer(CBasePlayer* plr) {
	if (!plr || (plr->pev->flags & FL_FAKECLIENT) == 0) {
		return;
	}

	int eidx = plr->entindex();
	char* infoBuffer = g_engfuncs.pfnGetInfoKeyBuffer(plr->edict());

	if (strcmp(name, plr->DisplayName())) {
		g_engfuncs.pfnSetClientKeyValue(eidx, infoBuffer, "name", name);
	}
	if (strcmp(name, STRING(plr->pev->model))) {
		g_engfuncs.pfnSetClientKeyValue(eidx, infoBuffer, "model", model);
	}
	//if (deltas & FL_DELTA_COLORS) {
		//TODO: is this expensive?
		g_engfuncs.pfnSetClientKeyValue(eidx, infoBuffer, "topcolor", UTIL_VarArgs("%d", topColor));
		g_engfuncs.pfnSetClientKeyValue(eidx, infoBuffer, "bottomcolor", UTIL_VarArgs("%d", bottomColor));
	//}
	//if (deltas & FL_DELTA_WEAPONMODEL) {
		plr->pev->weaponmodel = MAKE_STRING(g_demoPlayer->getReplayModel(weaponmodel));
	//}
	//if (deltas & FL_DELTA_VIEWMODEL) {
		plr->pev->viewmodel = MAKE_STRING(g_demoPlayer->getReplayModel(viewmodel));
	//}
	plr->pev->frags = frags;
	plr->pev->weaponanim = weaponanim;
	plr->pev->armorvalue = armorvalue;
	plr->pev->view_ofs.z = view_ofs / 16.0f;
	plr->pev->deadflag = deadFlag;

	edict_t* specViewEnt = plr->edict();
	if (viewEnt) {
		edict_t* camera = g_demoPlayer->getReplayEntity(viewEnt);
		if (camera) {
			SET_VIEW(plr->edict(), camera);
			specViewEnt = camera;
		}
		else {
			ALERT(at_console, "Bad view entity %d\n", viewEnt);
		}
	}
	else {
		SET_VIEW(plr->edict(), plr->edict());
	}

	// update spectator views
	for (int i = 1; i < gpGlobals->maxClients; i++) {
		CBasePlayer* spec = (CBasePlayer*)UTIL_PlayerByIndex(i);
		if (!spec || (spec->pev->flags & FL_FAKECLIENT)) {
			continue;
		}

		if (spec->m_hObserverTarget.GetEntity() == plr && spec->pev->iuser1 == OBS_IN_EYE) {
			SET_VIEW(spec->edict(), specViewEnt);
		}
		else {
			SET_VIEW(spec->edict(), spec->edict());
		}
	}
}

bool netedict::readDeltas(mstream& reader) {
	uint8_t oldtype = etype;
	uint8_t newtype = etype;

	deltaBitsLast = 0;

	if (reader.readBit()) {
		deltaBitsLast |= FL_DELTA_FLAGS_CHANGED;
		newtype = reader.readBits(3);
		g_stats.entDeltaCatSz[FL_DELTA_CAT_EDFLAG] += 3;
	}

	if ((!oldtype && newtype) || !newtype) {
		// new entity created or old deleted. Start deltas from a fresh state.
		reset();
	}

	etype = newtype;

	// origin category
	if (etype && reader.readBit()) {
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
	if (etype && reader.readBit()) {
		deltaBitsLast |= FL_DELTA_ANGLES_CHANGED;

		bool bigAngles = reader.readBit();

		for (int i = 0; i < 3; i++) {
			if (bigAngles) {
				READ_DELTA(FL_DELTA_CAT_ANGLES, angles[i], 32);
			}
			else {
				READ_DELTA(FL_DELTA_CAT_ANGLES, angles[i], 8);
			}
		}
	}

	if (etype && reader.readBit()) {
		// visibility category
		if (reader.readBit()) {
			READ_DELTA(FL_DELTA_CAT_VISIBILITY, visibility[0], 8);
			READ_DELTA(FL_DELTA_CAT_VISIBILITY, visibility[1], 8);
			READ_DELTA(FL_DELTA_CAT_VISIBILITY, visibility[2], 8);
			READ_DELTA(FL_DELTA_CAT_VISIBILITY, visibility[3], 8);
		}

		// animation deltas category
		if (reader.readBit()) {
			uint8_t oldFrame = frame;
			uint8_t oldSequence = sequence;
			uint8_t oldFramerate = framerate;

			READ_DELTA(FL_DELTA_CAT_ANIM, controller_lo, 16);
			READ_DELTA(FL_DELTA_CAT_ANIM, controller_hi, 16);
			READ_DELTA(FL_DELTA_CAT_ANIM, sequence, 8);
			READ_DELTA(FL_DELTA_CAT_ANIM, gait, 8);
			READ_DELTA(FL_DELTA_CAT_ANIM, blend, 8);
			READ_DELTA(FL_DELTA_CAT_ANIM, framerate, 8);
			READ_DELTA(FL_DELTA_CAT_ANIM, frame, 8);

			if (oldFrame != frame || oldSequence != sequence || oldFramerate != framerate) {
				deltaBitsLast |= FL_DELTA_ANIM_CHANGED;
			}
		}

		// rendering deltas category
		if (reader.readBit()) {
			READ_DELTA(FL_DELTA_CAT_RENDER, renderamt, 8);
			READ_DELTA(FL_DELTA_CAT_RENDER, rendercolor[0], 8);
			READ_DELTA(FL_DELTA_CAT_RENDER, rendercolor[1], 8);
			READ_DELTA(FL_DELTA_CAT_RENDER, rendercolor[2], 8);
			READ_DELTA(FL_DELTA_CAT_RENDER, rendermode, 3);
			READ_DELTA(FL_DELTA_CAT_RENDER, renderfx, 5);
			READ_DELTA(FL_DELTA_CAT_RENDER, effects, 16);
			READ_DELTA(FL_DELTA_CAT_RENDER, scale, 16);
		}
	}

	// misc category (infrequently updated)
	if (etype && reader.readBit()) {
		READ_DELTA(FL_DELTA_CAT_MISC, classname, 12);
		READ_DELTA(FL_DELTA_CAT_MISC, skin, 8);
		READ_DELTA(FL_DELTA_CAT_MISC, body, 8);
		READ_DELTA(FL_DELTA_CAT_MISC, colormap, 8);
		READ_DELTA(FL_DELTA_CAT_MISC, aiment, 16);
		READ_DELTA(FL_DELTA_CAT_MISC, modelindex, MODEL_BITS);
	}

	// internal state category
	if (etype && reader.readBit()) {
		READ_DELTA(FL_DELTA_CAT_INTERNAL, monsterstate, 4);
		READ_DELTA(FL_DELTA_CAT_INTERNAL, schedule, 7);
		READ_DELTA(FL_DELTA_CAT_INTERNAL, task, 5);
		READ_DELTA(FL_DELTA_CAT_INTERNAL, conditions_lo, 8);
		READ_DELTA(FL_DELTA_CAT_INTERNAL, conditions_md, 8);
		READ_DELTA(FL_DELTA_CAT_INTERNAL, conditions_hi, 16);
		READ_DELTA(FL_DELTA_CAT_INTERNAL, memories, 12);

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

	if (etype == ETYPE_PLAYER && reader.readBit()) {
		// player state
		if (reader.readBit()) {
			READ_DELTA(FL_DELTA_CAT_PLAYER_INFO, steamid64, 64);
			READ_DELTA_STR(FL_DELTA_CAT_PLAYER_INFO, name);
			READ_DELTA_STR(FL_DELTA_CAT_PLAYER_INFO, model);
			READ_DELTA(FL_DELTA_CAT_PLAYER_INFO, topColor, 8);
			READ_DELTA(FL_DELTA_CAT_PLAYER_INFO, bottomColor, 8);
			READ_DELTA(FL_DELTA_CAT_PLAYER_INFO, ping, 16);
			READ_DELTA(FL_DELTA_CAT_PLAYER_INFO, frags, 16);
		}

		// player state fast (frequently updated)
		if (reader.readBit()) {
			READ_DELTA(FL_DELTA_CAT_PLAYER_STATE_FAST, button, 16);
			READ_DELTA(FL_DELTA_CAT_PLAYER_STATE_FAST, armorvalue, 16);
			READ_DELTA(FL_DELTA_CAT_PLAYER_STATE_FAST, punchangle[0], 16);
			READ_DELTA(FL_DELTA_CAT_PLAYER_STATE_FAST, punchangle[1], 16);
			READ_DELTA(FL_DELTA_CAT_PLAYER_STATE_FAST, punchangle[2], 16);
			READ_DELTA(FL_DELTA_CAT_PLAYER_STATE_FAST, playerFlags, 8);
		}

		// player state slow (infrequently updated)
		if (reader.readBit()) {
			READ_DELTA(FL_DELTA_CAT_PLAYER_STATE_SLOW, viewEnt, 16);
			READ_DELTA(FL_DELTA_CAT_PLAYER_STATE_SLOW, viewmodel, 16);
			READ_DELTA(FL_DELTA_CAT_PLAYER_STATE_SLOW, weaponmodel, 16);
			READ_DELTA(FL_DELTA_CAT_PLAYER_STATE_SLOW, view_ofs, 16);
			READ_DELTA(FL_DELTA_CAT_PLAYER_STATE_SLOW, fov, 8);
			READ_DELTA(FL_DELTA_CAT_PLAYER_STATE_SLOW, specTarget, 5);
			READ_DELTA(FL_DELTA_CAT_PLAYER_STATE_SLOW, specMode, 3);
			READ_DELTA(FL_DELTA_CAT_PLAYER_STATE_SLOW, weaponId, 8);
			READ_DELTA(FL_DELTA_CAT_PLAYER_STATE_SLOW, deadFlag, 4);
		}

		// player weapon state (frequently updated together)
		if (reader.readBit()) {
			READ_DELTA(FL_DELTA_CAT_PLAYER_WEAPON, clip, 16);
			READ_DELTA(FL_DELTA_CAT_PLAYER_WEAPON, clip2, 16);
			READ_DELTA(FL_DELTA_CAT_PLAYER_WEAPON, ammo, 16);
			READ_DELTA(FL_DELTA_CAT_PLAYER_WEAPON, ammo2, 16);
			READ_DELTA(FL_DELTA_CAT_PLAYER_WEAPON, weaponanim, 8);
			READ_DELTA(FL_DELTA_CAT_PLAYER_WEAPON, weaponState, 8);
		}
	}

	g_stats.entUpdateCount++;

	return reader.eom();
}

int netedict::writeDeltas(mstream& writer, netedict& old) {
	uint64_t startOffset = writer.tellBits();

	if (!old.etype) {
		// new entity created. Start from a fresh previous state.
		// some vars may have changed while we didn't send deltas but still memcpy()'d
		// to fileedicts as if the client knows the latest state.
		old.reset();
	}

	bool wroteAnyDeltas = false;

	// edflags must come first, so the reader can know to reset state when new ents are created
	// or if the entity type changes
	if (etype != old.etype) {
		wroteAnyDeltas = true;
		writer.writeBit(1);
		
		if (etype) {
			writer.writeBits(etype, 3);
		}
		else {
			writer.writeBits(0, 3); // 0 flags = deleted ent
		}

		g_stats.entDeltaCatSz[FL_DELTA_CAT_EDFLAG] += 4;
	}
	else {
		writer.writeBit(0);
	}

	// origin category
	if (etype != ETYPE_INVALID) {
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
	if (etype != ETYPE_INVALID) {
		uint32_t dx = angles[0] - old.angles[0];
		uint32_t dy = angles[1] - old.angles[1];
		uint32_t dz = angles[2] - old.angles[2];

		bool anglesChanged = dx || dy || dz;

		writer.writeBit(anglesChanged);
		if (anglesChanged) {
			wroteAnyDeltas = true;

			bool bigAngles = etype == ETYPE_BEAM;
			writer.writeBit(bigAngles);

			for (int i = 0; i < 3; i++) {
				if (bigAngles) {
					WRITE_DELTA_SINGLE(FL_DELTA_CAT_ANGLES, angles[i], 32);
				}
				else {
					WRITE_DELTA_SINGLE(FL_DELTA_CAT_ANGLES, angles[i], 8);
				}
			}
		}
	}

	// rendering category
	BEGIN_DELTA_CATEGORY_COND(FL_DELTA_CAT_DISPLAY, etype != ETYPE_INVALID)
		BEGIN_DELTA_CATEGORY(FL_DELTA_CAT_VISIBILITY)
			WRITE_DELTA(FL_DELTA_CAT_VISIBILITY, visibility[0], 8);
			WRITE_DELTA(FL_DELTA_CAT_VISIBILITY, visibility[1], 8);
			WRITE_DELTA(FL_DELTA_CAT_VISIBILITY, visibility[2], 8);
			WRITE_DELTA(FL_DELTA_CAT_VISIBILITY, visibility[3], 8);
		END_DELTA_CATEGROY_NESTED(FL_DELTA_CAT_VISIBILITY, FL_DELTA_CAT_DISPLAY)

		BEGIN_DELTA_CATEGORY(FL_DELTA_CAT_ANIM)
			WRITE_DELTA(FL_DELTA_CAT_ANIM, controller_lo, 16);
			WRITE_DELTA(FL_DELTA_CAT_ANIM, controller_hi, 16);
			WRITE_DELTA(FL_DELTA_CAT_ANIM, sequence, 8);
			WRITE_DELTA(FL_DELTA_CAT_ANIM, gait, 8);
			WRITE_DELTA(FL_DELTA_CAT_ANIM, blend, 8);
			WRITE_DELTA(FL_DELTA_CAT_ANIM, framerate, 8);
			WRITE_DELTA_COND(FL_DELTA_CAT_ANIM, frame, 8, old.frame != frame || forceNextFrame);
		END_DELTA_CATEGROY_NESTED(FL_DELTA_CAT_ANIM, FL_DELTA_CAT_DISPLAY)

		// rendering deltas category
		BEGIN_DELTA_CATEGORY(FL_DELTA_CAT_RENDER)
			WRITE_DELTA(FL_DELTA_CAT_RENDER, renderamt, 8);
			WRITE_DELTA(FL_DELTA_CAT_RENDER, rendercolor[0], 8);
			WRITE_DELTA(FL_DELTA_CAT_RENDER, rendercolor[1], 8);
			WRITE_DELTA(FL_DELTA_CAT_RENDER, rendercolor[2], 8);
			WRITE_DELTA(FL_DELTA_CAT_RENDER, rendermode, 3);
			WRITE_DELTA(FL_DELTA_CAT_RENDER, renderfx, 5);
			WRITE_DELTA(FL_DELTA_CAT_RENDER, effects, 16);
			WRITE_DELTA(FL_DELTA_CAT_RENDER, scale, 16);
		END_DELTA_CATEGROY_NESTED(FL_DELTA_CAT_RENDER, FL_DELTA_CAT_DISPLAY)
	END_DELTA_CATEGORY(FL_DELTA_CAT_DISPLAY)

	// misc category (infrequently updated)
	BEGIN_DELTA_CATEGORY_COND(FL_DELTA_CAT_MISC, etype != ETYPE_INVALID)
		WRITE_DELTA(FL_DELTA_CAT_MISC, classname, 12);
		WRITE_DELTA(FL_DELTA_CAT_MISC, skin, 8);
		WRITE_DELTA(FL_DELTA_CAT_MISC, body, 8);
		WRITE_DELTA(FL_DELTA_CAT_MISC, colormap, 8);
		WRITE_DELTA(FL_DELTA_CAT_MISC, aiment, 16);
		WRITE_DELTA(FL_DELTA_CAT_MISC, modelindex, MODEL_BITS);
	END_DELTA_CATEGORY(FL_DELTA_CAT_MISC)

	BEGIN_DELTA_CATEGORY_COND(FL_DELTA_CAT_INTERNAL, etype != ETYPE_INVALID)
		WRITE_DELTA(FL_DELTA_CAT_INTERNAL, monsterstate, 4);
		WRITE_DELTA(FL_DELTA_CAT_INTERNAL, schedule, 7);
		WRITE_DELTA(FL_DELTA_CAT_INTERNAL, task, 5);
		WRITE_DELTA(FL_DELTA_CAT_INTERNAL, conditions_lo, 8);
		WRITE_DELTA(FL_DELTA_CAT_INTERNAL, conditions_md, 8);
		WRITE_DELTA(FL_DELTA_CAT_INTERNAL, conditions_hi, 16);
		WRITE_DELTA(FL_DELTA_CAT_INTERNAL, memories, 12);
		bool healthChanged = old.health != health;
		writer.writeBit(healthChanged);
		if (healthChanged) {
			MARK_CATEGORY_FILLED(FL_DELTA_CAT_INTERNAL)
			if (health > 1023) {
				writer.writeBit(1);
				writer.writeBits(health, 32);
			}
			else {
				writer.writeBit(0);
				writer.writeBits(health, 10);
			}
		}
	END_DELTA_CATEGORY(FL_DELTA_CAT_INTERNAL)

	// player state
	BEGIN_DELTA_CATEGORY_COND(FL_DELTA_CAT_PLAYER, etype == ETYPE_PLAYER)
		// player info
		BEGIN_DELTA_CATEGORY(FL_DELTA_CAT_PLAYER_INFO)
			WRITE_DELTA(FL_DELTA_CAT_PLAYER_INFO, steamid64, 64);
			WRITE_DELTA_STR(FL_DELTA_CAT_PLAYER_INFO, name);
			WRITE_DELTA_STR(FL_DELTA_CAT_PLAYER_INFO, model);
			WRITE_DELTA(FL_DELTA_CAT_PLAYER_INFO, topColor, 8);
			WRITE_DELTA(FL_DELTA_CAT_PLAYER_INFO, bottomColor, 8);
			WRITE_DELTA(FL_DELTA_CAT_PLAYER_INFO, ping, 16);
			WRITE_DELTA(FL_DELTA_CAT_PLAYER_INFO, frags, 16);
		END_DELTA_CATEGROY_NESTED(FL_DELTA_CAT_PLAYER_INFO, FL_DELTA_CAT_PLAYER)

		// player state fast (frequently updated)
		BEGIN_DELTA_CATEGORY(FL_DELTA_CAT_PLAYER_STATE_FAST)
			WRITE_DELTA(FL_DELTA_CAT_PLAYER_STATE_FAST, button, 16);
			WRITE_DELTA(FL_DELTA_CAT_PLAYER_STATE_FAST, armorvalue, 16);
			WRITE_DELTA(FL_DELTA_CAT_PLAYER_STATE_FAST, punchangle[0], 16);
			WRITE_DELTA(FL_DELTA_CAT_PLAYER_STATE_FAST, punchangle[1], 16);
			WRITE_DELTA(FL_DELTA_CAT_PLAYER_STATE_FAST, punchangle[2], 16);
			WRITE_DELTA(FL_DELTA_CAT_PLAYER_STATE_FAST, playerFlags, 8);
		END_DELTA_CATEGROY_NESTED(FL_DELTA_CAT_PLAYER_STATE_FAST, FL_DELTA_CAT_PLAYER)

		// player state slow (infrequently updated)
		BEGIN_DELTA_CATEGORY(FL_DELTA_CAT_PLAYER_STATE_SLOW)
			WRITE_DELTA(FL_DELTA_CAT_PLAYER_STATE_SLOW, viewEnt, 16);
			WRITE_DELTA(FL_DELTA_CAT_PLAYER_STATE_SLOW, viewmodel, 16);
			WRITE_DELTA(FL_DELTA_CAT_PLAYER_STATE_SLOW, weaponmodel, 16);
			WRITE_DELTA(FL_DELTA_CAT_PLAYER_STATE_SLOW, view_ofs, 16);
			WRITE_DELTA(FL_DELTA_CAT_PLAYER_STATE_SLOW, fov, 8);
			WRITE_DELTA(FL_DELTA_CAT_PLAYER_STATE_SLOW, specTarget, 5);
			WRITE_DELTA(FL_DELTA_CAT_PLAYER_STATE_SLOW, specMode, 3);
			WRITE_DELTA(FL_DELTA_CAT_PLAYER_STATE_SLOW, weaponId, 8);
			WRITE_DELTA(FL_DELTA_CAT_PLAYER_STATE_SLOW, deadFlag, 4);
		END_DELTA_CATEGROY_NESTED(FL_DELTA_CAT_PLAYER_STATE_SLOW, FL_DELTA_CAT_PLAYER)

		// player weapon state (frequently updated together)
		BEGIN_DELTA_CATEGORY(FL_DELTA_CAT_PLAYER_WEAPON)
			WRITE_DELTA(FL_DELTA_CAT_PLAYER_WEAPON, clip, 16);
			WRITE_DELTA(FL_DELTA_CAT_PLAYER_WEAPON, clip2, 16);
			WRITE_DELTA(FL_DELTA_CAT_PLAYER_WEAPON, ammo, 16);
			WRITE_DELTA(FL_DELTA_CAT_PLAYER_WEAPON, ammo2, 16);
			WRITE_DELTA(FL_DELTA_CAT_PLAYER_WEAPON, weaponanim, 8);
			WRITE_DELTA(FL_DELTA_CAT_PLAYER_WEAPON, weaponState, 8);
		END_DELTA_CATEGROY_NESTED(FL_DELTA_CAT_PLAYER_WEAPON, FL_DELTA_CAT_PLAYER)
	END_DELTA_CATEGORY(FL_DELTA_CAT_PLAYER)


	if (writer.eom()) {
		return EDELTA_OVERFLOW;
	}

	if (!wroteAnyDeltas) {
		writer.seekBits(startOffset);
		return EDELTA_NONE;
	}

	return EDELTA_WRITE;
}
