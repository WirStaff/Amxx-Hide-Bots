#include "synthetic_players.h"

#include <cstdint>
#include <cstring>
#include <ctime>

#include <sdk/amxxmodule.h>

extern globalvars_t* gpGlobals;

namespace hide_bots {
namespace {

constexpr int kMaxSyntheticPlayers = 256;
constexpr std::size_t kSyntheticNameCapacity = 32;
constexpr int kMinSyntheticNameLength = 7;
constexpr int kMaxSyntheticNameLength = 10;

float g_SyntheticPlayerConnectedAt[kMaxSyntheticPlayers] = {};
char g_SyntheticPlayerNames[kMaxSyntheticPlayers][kSyntheticNameCapacity] = {};
float g_LastSyntheticServerTime = 0.0f;
uint32_t g_RandomState = 0;
const char kBase62Alphabet[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

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

int NormalizeSyntheticIndex(int playerIndex)
{
    if (playerIndex < 0) {
        return 0;
    }

    if (playerIndex >= kMaxSyntheticPlayers) {
        return playerIndex % kMaxSyntheticPlayers;
    }

    return playerIndex;
}

float UpdateSyntheticServerTime()
{
    const float now = GetSyntheticServerTime();
    if (now + 1.0f < g_LastSyntheticServerTime) {
        std::memset(g_SyntheticPlayerConnectedAt, 0, sizeof(g_SyntheticPlayerConnectedAt));
        std::memset(g_SyntheticPlayerNames, 0, sizeof(g_SyntheticPlayerNames));
    }

    g_LastSyntheticServerTime = now;
    return now;
}

float GenerateSyntheticDuration(int playerIndex)
{
    const uint32_t seconds = 30u + (NextRandom() % 5400u);
    const float fraction = static_cast<float>((playerIndex * 137) % 1000) / 1000.0f;
    return static_cast<float>(seconds) + fraction;
}

void AppendByte(char* buffer, std::size_t capacity, std::size_t& position, unsigned char value)
{
    if (position + 1 >= capacity) {
        return;
    }

    buffer[position++] = static_cast<char>(value);
}

void AppendUtf8CodePoint(char* buffer, std::size_t capacity, std::size_t& position, uint32_t codePoint)
{
    if (codePoint <= 0x7Fu) {
        AppendByte(buffer, capacity, position, static_cast<unsigned char>(codePoint));
    } else if (codePoint <= 0x7FFu) {
        AppendByte(buffer, capacity, position, static_cast<unsigned char>(0xC0u | (codePoint >> 6)));
        AppendByte(buffer, capacity, position, static_cast<unsigned char>(0x80u | (codePoint & 0x3Fu)));
    } else {
        AppendByte(buffer, capacity, position, static_cast<unsigned char>(0xE0u | (codePoint >> 12)));
        AppendByte(buffer, capacity, position, static_cast<unsigned char>(0x80u | ((codePoint >> 6) & 0x3Fu)));
        AppendByte(buffer, capacity, position, static_cast<unsigned char>(0x80u | (codePoint & 0x3Fu)));
    }
}

void AppendRandomNameChar(char* buffer, std::size_t capacity, std::size_t& position)
{
    const uint32_t kind = NextRandom() % 10u;
    if (kind < 4u) {
        const bool upper = (NextRandom() & 1u) != 0;
        const unsigned char base = upper ? static_cast<unsigned char>('A') : static_cast<unsigned char>('a');
        AppendByte(buffer, capacity, position, static_cast<unsigned char>(base + (NextRandom() % 26u)));
    } else if (kind < 8u) {
        const bool upper = (NextRandom() & 1u) != 0;
        const uint32_t base = upper ? 0x0410u : 0x0430u;
        AppendUtf8CodePoint(buffer, capacity, position, base + (NextRandom() % 32u));
    } else {
        AppendByte(buffer, capacity, position, static_cast<unsigned char>('0' + (NextRandom() % 10u)));
    }
}

void AppendUniqueNameSuffix(int syntheticIndex, char* buffer, std::size_t capacity, std::size_t& position)
{
    const std::size_t alphabetSize = sizeof(kBase62Alphabet) - 1;
    const std::size_t first = static_cast<std::size_t>(syntheticIndex) % alphabetSize;
    const std::size_t second = (static_cast<std::size_t>(syntheticIndex) / alphabetSize) % alphabetSize;

    AppendByte(buffer, capacity, position, static_cast<unsigned char>(kBase62Alphabet[first]));
    AppendByte(buffer, capacity, position, static_cast<unsigned char>(kBase62Alphabet[second]));
}

bool IsSyntheticNameUsed(const char* name, int syntheticIndex)
{
    for (int index = 0; index < kMaxSyntheticPlayers; index++) {
        if (index != syntheticIndex
            && g_SyntheticPlayerNames[index][0] != '\0'
            && std::strcmp(g_SyntheticPlayerNames[index], name) == 0) {
            return true;
        }
    }

    return false;
}

void BuildFallbackSyntheticName(int syntheticIndex, char* buffer, std::size_t capacity)
{
    const std::size_t alphabetSize = sizeof(kBase62Alphabet) - 1;
    uint32_t value = static_cast<uint32_t>(syntheticIndex + 1);
    std::size_t position = 0;

    for (int index = 0; index < kMinSyntheticNameLength && position + 1 < capacity; index++) {
        AppendByte(buffer, capacity, position, static_cast<unsigned char>(kBase62Alphabet[value % alphabetSize]));
        value /= static_cast<uint32_t>(alphabetSize);
    }

    buffer[position] = '\0';
}

void GenerateSyntheticName(int syntheticIndex, char* buffer, std::size_t capacity)
{
    for (int attempt = 0; attempt < 64; attempt++) {
        std::size_t position = 0;
        const int nameLength = kMinSyntheticNameLength + static_cast<int>(NextRandom() % (kMaxSyntheticNameLength - kMinSyntheticNameLength + 1));

        for (int index = 0; index < nameLength - 2; index++) {
            AppendRandomNameChar(buffer, capacity, position);
        }

        AppendUniqueNameSuffix(syntheticIndex, buffer, capacity, position);
        buffer[position] = '\0';

        if (!IsSyntheticNameUsed(buffer, syntheticIndex)) {
            return;
        }
    }

    BuildFallbackSyntheticName(syntheticIndex, buffer, capacity);
}

void CopySyntheticName(const char* source, char* buffer, std::size_t capacity)
{
    std::size_t length = std::strlen(source);
    if (length >= capacity) {
        length = capacity - 1;
    }

    std::memcpy(buffer, source, length);
    buffer[length] = '\0';
}

}

void BuildSyntheticPlayerName(int playerIndex, char* buffer, std::size_t capacity)
{
    if (!buffer || capacity == 0) {
        return;
    }

    UpdateSyntheticServerTime();

    const int syntheticIndex = NormalizeSyntheticIndex(playerIndex);
    char* syntheticName = g_SyntheticPlayerNames[syntheticIndex];
    if (syntheticName[0] == '\0') {
        GenerateSyntheticName(syntheticIndex, syntheticName, kSyntheticNameCapacity);
    }

    CopySyntheticName(syntheticName, buffer, capacity);
}

float GetSyntheticPlayerDuration(int playerIndex)
{
    const float now = UpdateSyntheticServerTime();
    const int syntheticIndex = NormalizeSyntheticIndex(playerIndex);

    float& connectedAt = g_SyntheticPlayerConnectedAt[syntheticIndex];
    if (connectedAt == 0.0f || connectedAt >= now) {
        connectedAt = now - GenerateSyntheticDuration(playerIndex);
    }

    const float duration = now - connectedAt;
    return duration > 0.0f ? duration : GenerateSyntheticDuration(playerIndex);
}

}
