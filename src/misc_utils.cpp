#include "misc_utils.h"
#include "zita-resampler/resampler.h"

string getFileExtension(string fpath) {
	int dot = fpath.find_last_of(".");
	if (dot != -1 && dot < fpath.size()-1) {
		return fpath.substr(dot + 1);
	}

	return "";
}

string getPlayerUniqueId(edict_t* plr) {
	if (plr == NULL) {
		return "STEAM_ID_NULL";
	}

	string steamId = (*g_engfuncs.pfnGetPlayerAuthId)(plr);

	if (steamId == "STEAM_ID_LAN" || steamId == "BOT") {
		steamId = STRING(plr->v.netname);
	}

	return steamId;
}

string replaceString(string subject, string search, string replace)
{
	size_t pos = 0;
	while ((pos = subject.find(search, pos)) != string::npos)
	{
		subject.replace(pos, search.length(), replace);
		pos += replace.length();
	}
	return subject;
}

edict_t* getPlayerByUniqueId(string id) {
	for (int i = 1; i <= gpGlobals->maxClients; i++) {
		edict_t* ent = INDEXENT(i);

		if (!ent || (ent->v.flags & FL_CLIENT) == 0) {
			continue;
		}

		if (id == getPlayerUniqueId(ent)) {
			return ent;
		}
	}

	return NULL;
}

edict_t* getPlayerByUserId(int id) {
	for (int i = 1; i <= gpGlobals->maxClients; i++) {
		edict_t* ent = INDEXENT(i);

		if (!isValidPlayer(ent)) {
			continue;
		}

		if (id == (*g_engfuncs.pfnGetPlayerUserId)(ent)) {
			return ent;
		}
	}

	return NULL;
}

bool isValidPlayer(edict_t* plr) {
	return plr && (plr->v.flags & FL_CLIENT) != 0;
}

void clientCommand(edict_t* plr, string cmd, int destType) {
	MESSAGE_BEGIN(destType, 9, NULL, plr);
	WRITE_STRING(UTIL_VarArgs(";%s;", cmd.c_str()));
	MESSAGE_END();
}

string trimSpaces(string s)
{
	// Remove white space indents
	int lineStart = s.find_first_not_of(" \t\n\r");
	if (lineStart == string::npos)
		return "";

	// Remove spaces after the last character
	int lineEnd = s.find_last_not_of(" \t\n\r");
	if (lineEnd != string::npos && lineEnd < s.length() - 1)
		s = s.substr(lineStart, (lineEnd + 1) - lineStart);
	else
		s = s.substr(lineStart);

	return s;
}

bool cgetline(FILE* file, string& output) {
	static char buffer[4096];

	if (fgets(buffer, sizeof(buffer), file)) {
		output = string(buffer);
		if (output[output.length() - 1] == '\n') {
			output = output.substr(0, output.length() - 1);
		}
		return true;
	}

	return false;
}

string formatTime(int totalSeconds) {
	int hours = totalSeconds / (60 * 60);
	int minutes = (totalSeconds / 60) - hours * 60;
	int seconds = totalSeconds % 60;

	if (hours > 0) {
		return UTIL_VarArgs("%d:%02d:%02d", hours, minutes, seconds);
	}
	else {
		return UTIL_VarArgs("%d:%02d", minutes, seconds);
	}
}

vector<string> splitString(string str, const char* delimitters)
{
	vector<string> split;
	size_t start = 0;
	size_t end = str.find_first_of(delimitters);

	while (end != std::string::npos)
	{
		split.push_back(str.substr(start, end - start));
		start = end + 1;
		end = str.find_first_of(delimitters, start);
	}

	split.push_back(str.substr(start));

	return split;
}

uint32_t getFileSize(FILE* file) {
	fseek(file, 0, SEEK_END); // seek to end of file
	uint32_t size = ftell(file); // get current file pointer
	fseek(file, 0, SEEK_SET);
	return size;
}

float clampf(float val, float min, float max) {
	if (val > max) {
		return max;
	}
	else if (val < min) {
		return min;
	}
	return val;
}

int clamp(int val, int min, int max) {
	if (val > max) {
		return max;
	}
	else if (val < min) {
		return min;
	}
	return val;
}

bool fileExists(const std::string& name) {
	if (FILE* file = fopen(name.c_str(), "r")) {
		fclose(file);
		return true;
	}
	else {
		return false;
	}
}

// mixes samples in-place without a new array
int mixStereoToMono(int16_t* pcm, int numSamples) {

	for (int i = 0; i < numSamples / 2; i++) {
		float left = ((float)pcm[i * 2] / 32768.0f);
		float right = ((float)pcm[i * 2 + 1] / 32768.0f);
		pcm[i] = clampf(left + right, -1.0f, 1.0f) * 32767;
	}

	return numSamples / 2;
}

int resamplePcm(int16_t* pcm_old, int16_t* pcm_new, int oldRate, int newRate, int numSamples) {
	float samplesPerStep = (float)oldRate / newRate;
	int numSamplesNew = (float)numSamples / samplesPerStep;
	float t = 0;

	for (int i = 0; i < numSamplesNew; i++) {
		int newIdx = t;
		pcm_new[i] = pcm_old[newIdx];
		t += samplesPerStep;
	}

	return numSamplesNew;
}

uint64_t steamid_to_steamid64(string steamid) {
	uint64_t X = atoi(steamid.substr(8, 1).c_str());
	uint64_t Y = atoi(steamid.substr(10).c_str());

	uint64_t steam64id = 76561197960265728;
	steam64id += Y * 2;
	if (X == 1) {
		steam64id += 1;
	}

	return steam64id;
}

// Normalizes any number to an arbitrary range 
// by assuming the range wraps around when going below min or above max
// https://stackoverflow.com/questions/1628386/normalise-orientation-between-0-and-360
float normalizeRangef(const float value, const float start, const float end)
{
	const float width = end - start;
	const float offsetValue = value - start;   // value relative to 0

	return (offsetValue - (floor(offsetValue / width) * width)) + start;
	// + start to reset back to start of original range
}
