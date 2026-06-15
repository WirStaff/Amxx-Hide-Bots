#pragma once

#include <cstdint>

#include <sdk/amxxmodule.h>

namespace hide_bots {

bool IsA2SPlayerQuery(const char* args, uint32_t& challenge);
bool IsA2SPlayerInitialChallenge(uint32_t challenge);

int BuildA2SChallengeResponse(char* responseBuffer, int responseBufferSize);
int BuildA2SPlayersResponse(char* responseBuffer, int responseBufferSize);

bool RewriteA2SPlayersIndexes(
    const char* packet,
    int packetLength,
    char* rewrittenPacket,
    int rewrittenCapacity,
    int& rewrittenLength);

int GetPlayerSlot(const edict_t* entity);
void MarkPlayerConnected(edict_t* entity);
void MarkPlayerDisconnected(edict_t* entity);

}
