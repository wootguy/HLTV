#pragma once
#include "main.h"

typedef struct WAV_HEADER {
	/* RIFF Chunk Descriptor */
	uint8_t RIFF[4] = { 'R', 'I', 'F', 'F' }; // RIFF Header Magic header
	uint32_t ChunkSize;                     // RIFF Chunk Size
	uint8_t WAVE[4] = { 'W', 'A', 'V', 'E' }; // WAVE Header
	/* "fmt" sub-chunk */
	uint8_t fmt[4] = { 'f', 'm', 't', ' ' }; // FMT header
	uint32_t Subchunk1Size = 16;           // Size of the fmt chunk
	uint16_t AudioFormat = 1; // Audio format 1=PCM,6=mulaw,7=alaw,     257=IBM
								// Mu-Law, 258=IBM A-Law, 259=ADPCM
	uint16_t NumOfChan = 1;   // Number of channels 1=Mono 2=Sterio
	uint32_t SamplesPerSec = 12000;   // Sampling Frequency in Hz
	uint32_t bytesPerSec = 12000 * 2; // bytes per second
	uint16_t blockAlign = 2;          // 2=16-bit mono, 4=16-bit stereo
	uint16_t bitsPerSample = 16;      // Number of bits per sample
	/* "data" sub-chunk */
	uint8_t Subchunk2ID[4] = { 'd', 'a', 't', 'a' }; // "data"  string
	uint32_t Subchunk2Size;                        // Sampled data length
} wav_hdr;

string replaceString(string subject, string search, string replace);

edict_t* getPlayerByUniqueId(string id);

// user IDs change every time a user connects to the server
edict_t* getPlayerByUserId(int id);

string getPlayerUniqueId(edict_t* plr);

bool isValidPlayer(edict_t* plr);

void clientCommand(edict_t* plr, string cmd, int destType = MSG_ONE);

string trimSpaces(string s);

bool cgetline(FILE* file, string& output);

string formatTime(int totalSeconds);

vector<string> splitString(string str, const char* delimitters);

uint32_t getFileSize(FILE* file);

float clampf(float val, float min, float max);

int clamp(int val, int min, int max);

string getFileExtension(string fpath);

bool fileExists(const std::string& name);

// mixes samples in-place without a new array
int mixStereoToMono(int16_t* pcm, int numSamples);

// fast resample without pitch correction
int resamplePcm(int16_t* pcm_old, int16_t* pcm_new, int oldRate, int newRate, int numSamples);

uint64_t steamid_to_steamid64(string steamid);

float normalizeRangef(const float value, const float start, const float end);
