#include "synthetic_players.h"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <ctime>

#include <sdk/amxxmodule.h>

extern globalvars_t* gpGlobals;

namespace hide_bots {
namespace {

constexpr int kMaxSyntheticPlayers = 256;

float g_SyntheticPlayerConnectedAt[kMaxSyntheticPlayers] = {};
float g_LastSyntheticServerTime = 0.0f;
uint32_t g_RandomState = 0;

const char* const kSyntheticNames[] = {
    "Alex",
    "Mason",
    "Brandon",
    "Tyler",
    "Logan",
    "Carter",
    "Hunter",
    "Connor",
    "Dylan",
    "Austin",
    "Blake",
    "Chase",
    "Ryder",
    "Nolan",
    "Ethan",
    "Aaron",
    "Parker",
    "Corey",
    "Justin",
    "Kevin",
    "Travis",
    "Spencer",
    "Wesley",
    "Warren",
    "Morgan",
    "Dalton",
    "Darren",
    "Garry",
    "Simon",
    "Martin",
    "Robin",
    "Victor",
};

float GetSyntheticServerTime()
{
    if (gpGlobals) {
        return gpGlobals->time;
    }

    return static_cast<float>(std::time(nullptr) & 0xFFFF);
}

uint32_t NextRandom()
{
    if (g_RandomState == 0) {
        const uint32_t serverTime = gpGlobals ? static_cast<uint32_t>(gpGlobals->time * 1000.0f) : 0;
        const uint32_t wallTime = static_cast<uint32_t>(std::time(nullptr));
        const uint32_t address = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&g_RandomState));
        g_RandomState = wallTime ^ serverTime ^ address ^ 0x9E3779B9u;
    }

    g_RandomState = g_RandomState * 1664525u + 1013904223u;
    return g_RandomState;
}

float GenerateSyntheticDuration(int playerIndex)
{
    const uint32_t seconds = 30u + (NextRandom() % 5400u);
    const float fraction = static_cast<float>((playerIndex * 137) % 1000) / 1000.0f;
    return static_cast<float>(seconds) + fraction;
}

}

void BuildSyntheticPlayerName(int playerIndex, char* buffer, std::size_t capacity)
{
    if (!buffer || capacity == 0) {
        return;
    }

    const std::size_t nameCount = sizeof(kSyntheticNames) / sizeof(kSyntheticNames[0]);
    const char* baseName = kSyntheticNames[static_cast<size_t>(playerIndex) % nameCount];

    if (static_cast<size_t>(playerIndex) < nameCount) {
        std::snprintf(buffer, capacity, "%s", baseName);
    } else {
        std::snprintf(buffer, capacity, "%s%d", baseName, playerIndex + 1);
    }

    buffer[capacity - 1] = '\0';
}

float GetSyntheticPlayerDuration(int playerIndex)
{
    const float now = GetSyntheticServerTime();
    if (now + 1.0f < g_LastSyntheticServerTime) {
        std::memset(g_SyntheticPlayerConnectedAt, 0, sizeof(g_SyntheticPlayerConnectedAt));
    }

    g_LastSyntheticServerTime = now;

    int syntheticIndex = playerIndex;
    if (syntheticIndex < 0) {
        syntheticIndex = 0;
    } else if (syntheticIndex >= kMaxSyntheticPlayers) {
        syntheticIndex %= kMaxSyntheticPlayers;
    }

    float& connectedAt = g_SyntheticPlayerConnectedAt[syntheticIndex];
    if (connectedAt == 0.0f || connectedAt >= now) {
        connectedAt = now - GenerateSyntheticDuration(playerIndex);
    }

    const float duration = now - connectedAt;
    return duration > 0.0f ? duration : GenerateSyntheticDuration(playerIndex);
}

}
