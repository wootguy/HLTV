#include "NetClient.h"
#include "mstream.h"
#include "SvenTV.h"

netedict::netedict() {
	memset(&isValid, 0, sizeof(netedict));
}

bool netedict::matches(netedict& other) {
	if (isValid != other.isValid) {
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
	return true;
}

void netedict::load(const edict_t& ed) {
	entvars_t vars = ed.v;

	isValid = !ed.free && ed.pvPrivateData && (vars.effects & EF_NODRAW) == 0 && vars.modelindex;

	if (!isValid) {
		memset(&isValid, 0, sizeof(netedict));
		return; // no need to update other values. Only the isFree var will be sent from now on
	}

	memcpy(&origin, ed.v.origin, 3 * sizeof(float));
	angles[0] = normalizeRangef(vars.angles.x, 0, 360) * (65535.0f/360.0f);
	angles[1] = normalizeRangef(vars.angles.y, 0, 360) * (65535.0f/360.0f);
	angles[2] = normalizeRangef(vars.angles.z, 0, 360) * (65535.0f/360.0f);

	if (ed.v.flags & FL_CLIENT) {
		angles[0] = normalizeRangef(vars.v_angle.x, 0, 360) * (65535.0f / 360.0f);
		angles[1] = normalizeRangef(vars.v_angle.y, 0, 360) * (65535.0f / 360.0f);
		angles[2] = normalizeRangef(vars.v_angle.z, 0, 360) * (65535.0f / 360.0f);
	}

	modelindex = vars.modelindex;
	skin = vars.skin;
	body = vars.body;
	effects = vars.effects;
	sequence = vars.sequence;
	gaitsequence = vars.gaitsequence;
	frame = vars.frame;
	animtime = vars.animtime;
	framerate = vars.framerate;
	memcpy(controller, vars.controller, 6); // copy controller AND blending bytes
	scale = vars.scale;
	rendermode = vars.rendermode;
	renderamt = vars.renderamt;
	memcpy(rendercolor, vars.rendercolor, 3);
	renderfx = vars.renderfx;
	aiment = vars.aiment ? ENTINDEX(vars.aiment) : 0;
	colormap = vars.colormap;
	health = vars.health;
}

void netedict::apply(edict_t* ed, vector<EHandle>& simEnts) {
	entvars_t& vars = ed->v;

	if (!isValid) {
		vars.effects |= EF_NODRAW;
		return; // no need to update other values. Only the isFree var will be sent from now on
	}

	// calculate instantaneous velocity for gait calculations
	vars.velocity[0] = origin[0] - vars.origin[0];
	vars.velocity[1] = origin[1] - vars.origin[1];
	vars.velocity[2] = origin[2] - vars.origin[2];

	memcpy(&vars.origin, origin, 3 * sizeof(float));
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
	vars.framerate = framerate;
	memcpy(vars.controller, controller, 6); // copy controller AND blending bytes
	vars.scale = scale;
	vars.rendermode = rendermode;
	vars.renderamt = renderamt;
	memcpy(vars.rendercolor, rendercolor, 3);
	vars.renderfx = renderfx;
	vars.colormap = colormap;
	vars.health = health;

	vars.movetype = vars.aiment ? MOVETYPE_FOLLOW : MOVETYPE_NOCLIP;

	if (aiment) {
		if (aiment >= simEnts.size()) {
			println("Invalid aiment %d / %d", aiment, (int)simEnts.size());
			vars.movetype = MOVETYPE_NOCLIP;
			return;
		}
		else {
			vars.aiment = simEnts[aiment];
		}
	}
}

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
	uint32_t deltaBits;
	uint16_t updateId; // ID of the global update which this packet belongs
	uint16_t baselineId; // ID of the update we should be delta-ing from
	uint16_t fragmentId; // ID of this packet within the update, unique only to this update

	reader.read(&packetType, 1);
	reader.read(&updateId, 2);
	reader.read(&baselineId, 2);
	reader.read(&fragmentId, 2);

	if (debugMode)
		println("Apply delta %d frag %d", updateId, fragmentId);

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
			println("ERROR: Invalid delta packet wants to update edict %d at %d", (int)fullIndex, loop);
			println("TODO: rollback changes made so far, because now client and server are desynced");
			return lastSuccessEdict;
		}

		uint64_t startPos = reader.tell();

		netedict& ed = baselines[fullIndex];

		reader.read(&deltaBits, 4);

		if (deltaBits == 0) {
			ed.isValid = false;
			//println("Skip free %d", fullIndex);
			continue;
		}
		ed.isValid = true;

		if (deltaBits & FL_DELTA_ORIGIN_X) {
			reader.read((void*)&ed.origin[0], 4);
		}
		if (deltaBits & FL_DELTA_ORIGIN_Y) {
			reader.read((void*)&ed.origin[1], 4);
		}
		if (deltaBits & FL_DELTA_ORIGIN_Z) {
			reader.read((void*)&ed.origin[2], 4);
		}
		if (deltaBits & FL_DELTA_ANGLES_X) {
			reader.read((void*)&ed.angles[0], 2);
		}
		if (deltaBits & FL_DELTA_ANGLES_Y) {
			reader.read((void*)&ed.angles[1], 2);
		}
		if (deltaBits & FL_DELTA_ANGLES_Z) {
			reader.read((void*)&ed.angles[2], 2);
		}
		if (deltaBits & FL_DELTA_MODELINDEX) {
			reader.read((void*)&ed.modelindex, 2);
		}
		if (deltaBits & FL_DELTA_SKIN) {
			reader.read((void*)&ed.skin, 1);
		}
		if (deltaBits & FL_DELTA_BODY) {
			reader.read((void*)&ed.body, 1);
		}
		if (deltaBits & FL_DELTA_EFFECTS) {
			reader.read((void*)&ed.effects, 1);
		}
		if (deltaBits & FL_DELTA_SEQUENCE) {
			reader.read((void*)&ed.sequence, 1);
		}
		if (deltaBits & FL_DELTA_GAITSEQUENCE) {
			reader.read((void*)&ed.gaitsequence, 1);
		}
		if (deltaBits & FL_DELTA_FRAME) {
			reader.read((void*)&ed.frame, 1);
		}
		if (deltaBits & FL_DELTA_ANIMTIME) {
			reader.read((void*)&ed.animtime, 1);
		}
		if (deltaBits & FL_DELTA_FRAMERATE) {
			reader.read((void*)&ed.framerate, 1);
		}
		if (deltaBits & FL_DELTA_CONTROLLER_0) {
			reader.read((void*)&ed.controller[0], 1);
		}
		if (deltaBits & FL_DELTA_CONTROLLER_1) {
			reader.read((void*)&ed.controller[1], 1);
		}
		if (deltaBits & FL_DELTA_CONTROLLER_2) {
			reader.read((void*)&ed.controller[2], 1);
		}
		if (deltaBits & FL_DELTA_CONTROLLER_3) {
			reader.read((void*)&ed.controller[3], 1);
		}
		if (deltaBits & FL_DELTA_BLENDING_0) {
			reader.read((void*)&ed.blending[0], 1);
		}
		if (deltaBits & FL_DELTA_BLENDING_1) {
			reader.read((void*)&ed.blending[1], 1);
		}
		if (deltaBits & FL_DELTA_SCALE) {
			reader.read((void*)&ed.scale, 1);
		}
		if (deltaBits & FL_DELTA_RENDERMODE) {
			reader.read((void*)&ed.rendermode, 1);
		}
		if (deltaBits & FL_DELTA_RENDERAMT) {
			reader.read((void*)&ed.renderamt, 1);
		}
		if (deltaBits & FL_DELTA_RENDERCOLOR_0) {
			reader.read((void*)&ed.rendercolor[0], 1);
		}
		if (deltaBits & FL_DELTA_RENDERCOLOR_1) {
			reader.read((void*)&ed.rendercolor[1], 1);
		}
		if (deltaBits & FL_DELTA_RENDERCOLOR_2) {
			reader.read((void*)&ed.rendercolor[2], 1);
		}
		if (deltaBits & FL_DELTA_RENDERFX) {
			reader.read((void*)&ed.renderfx, 1);
		}
		if (deltaBits & FL_DELTA_AIMENT) {
			reader.read((void*)&ed.aiment, 1);
		}

		if (debugMode)
			println("Read index %d (%d bytes)", (int)fullIndex, (int)(reader.tell() - startPos));

		if (reader.eom()) {
			println("ERROR: Invalid delta hit unexpected eom at %d", loop);
			println("TODO: rollback changes made so far, because now client and server are desynced");
			return lastSuccessEdict;
		}

		lastSuccessEdict = fullIndex;
	}

	return -1;
}