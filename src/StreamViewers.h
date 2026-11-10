#pragma once
#include "main.h"

// Shows the people watching the live stream to players and to each other.
//
// Viewers who logged in with a website token get a camera entity in the game, placed where they're
// looking from. The demo stream always includes the cameras, so viewers can see each other.
// Players only see them after turning them on with .showspecs.
//
// The scoreboard of players lists the viewers in empty player slots, under a "Web Spectators" team.
// When there aren't enough slots, the last one says how many more viewers there are. Viewers who
// used the stream password aren't shown anywhere, since their names aren't known.
//
// When a viewer starts or stops watching, the game chat and the website's chat say so
// (hltv.stream_announce). Viewers reconnect after every map change, so they only count as stopped
// once they've been gone for a while.

#define SPEC_CAMERA_CLASS "hltv_camera"
#define SPEC_CAMERA_MODEL "models/hlcoop_v2/camera_v2.mdl"

// true for camera entities, which are recorded even though they're hidden from most players
bool isSpecCamera(const edict_t& ed);

#ifdef HLCOOP_BUILD
void streamViewersMapInit(); // precaches the camera model
void streamViewersMapEnd(); // the entities are about to be removed
void updateStreamViewers(); // call every frame
bool cmd_showspecs(CBasePlayer* plr, const CommandArgs& args);
#endif
