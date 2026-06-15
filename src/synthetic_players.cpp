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

struct NamePart {
    const char* text = "";
    int chars = 0;
};

const NamePart kEnglishPrefixes[] = {
    {"x", 1},
    {"Mr", 2},
    {"No", 2},
    {"Neo", 3},
    {"Pro", 3},
    {"Top", 3},
    {"Old", 3},
    {"Red", 3},
    {"Sky", 3},
    {"Ice", 3},
};

const NamePart kRussianPrefixes[] = {
    {"\xD0\xBD\xD0\xB5\xD0\xBE", 3},
    {"\xD0\xBF\xD1\x80\xD0\xBE", 3},
    {"\xD1\x82\xD0\xBE\xD0\xBF", 3},
    {"\xD0\xBC\xD0\xB0\xD0\xBA\xD1\x81", 4},
    {"\xD0\xBB\xD0\xB0\xD0\xB9\xD1\x82", 4},
    {"\xD1\x80\xD0\xB5\xD0\xB9\xD0\xB4", 4},
};

const NamePart kEnglishCores[] = {
    {"Neon", 4},
    {"Ghost", 5},
    {"Cyber", 5},
    {"Storm", 5},
    {"Flash", 5},
    {"Frost", 5},
    {"Nitro", 5},
    {"Drift", 5},
    {"Pixel", 5},
    {"Pulse", 5},
    {"Orbit", 5},
    {"Mirage", 6},
    {"Vector", 6},
    {"Sniper", 6},
    {"Rocket", 6},
    {"Fusion", 6},
};

const NamePart kRussianCores[] = {
    {"\xD0\xBD\xD0\xB5\xD0\xBE\xD0\xBD", 4},
    {"\xD1\x82\xD0\xB5\xD0\xBD\xD1\x8C", 4},
    {"\xD0\xB3\xD1\x80\xD0\xBE\xD0\xBC", 4},
    {"\xD1\x84\xD0\xBE\xD1\x80\xD1\x81", 4},
    {"\xD1\x84\xD0\xBB\xD0\xB5\xD1\x88", 4},
    {"\xD0\xBA\xD0\xB8\xD0\xB1\xD0\xB5\xD1\x80", 5},
    {"\xD0\xBA\xD0\xB2\xD0\xB5\xD1\x81\xD1\x82", 5},
    {"\xD0\xBC\xD0\xB8\xD1\x80\xD0\xB0\xD0\xB6", 5},
    {"\xD1\x81\xD0\xBD\xD0\xB0\xD0\xB9\xD0\xBF", 5},
    {"\xD1\x88\xD1\x82\xD0\xBE\xD1\x80\xD0\xBC", 5},
    {"\xD0\xBF\xD1\x83\xD0\xBB\xD1\x8C\xD1\x81", 5},
    {"\xD1\x80\xD0\xB0\xD0\xB4\xD0\xB0\xD1\x80", 5},
    {"\xD0\xBD\xD0\xB8\xD1\x82\xD1\x80\xD0\xBE", 5},
    {"\xD0\xB4\xD1\x80\xD0\xB8\xD1\x84\xD1\x82", 5},
    {"\xD1\x81\xD0\xBA\xD0\xB8\xD0\xBB\xD0\xBB", 5},
    {"\xD0\xB2\xD0\xB5\xD0\xBA\xD1\x82\xD0\xBE\xD1\x80", 6},
};

const NamePart kEnglishSuffixes[] = {
    {"X", 1},
    {"Z", 1},
    {"GG", 2},
    {"Ok", 2},
    {"Er", 2},
    {"Aim", 3},
    {"Run", 3},
    {"Win", 3},
    {"One", 3},
};

const NamePart kRussianSuffixes[] = {
    {"\xD0\xBE\xD0\xBA", 2},
    {"\xD1\x8B\xD1\x87", 2},
    {"\xD0\xB5\xD1\x80", 2},
    {"\xD0\xB8\xD0\xBA", 2},
    {"\xD1\x87\xD0\xB8\xD0\xBA", 3},
    {"\xD0\xBF\xD1\x80\xD0\xBE", 3},
    {"\xD1\x82\xD0\xBE\xD0\xBF", 3},
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

bool AppendText(char* buffer, std::size_t capacity, std::size_t& position, const char* text)
{
    const std::size_t bytes = std::strlen(text);
    if (position + bytes >= capacity) {
        return false;
    }

    std::memcpy(buffer + position, text, bytes);
    position += bytes;
    return true;
}

bool AppendPart(char* buffer, std::size_t capacity, std::size_t& position, int& chars, const NamePart& part)
{
    if (!AppendText(buffer, capacity, position, part.text)) {
        return false;
    }

    chars += part.chars;
    return true;
}

template <std::size_t Count>
const NamePart* PickFittingPart(const NamePart (&parts)[Count], int maxChars)
{
    if (maxChars <= 0) {
        return nullptr;
    }

    const std::size_t start = static_cast<std::size_t>(NextRandom() % Count);

    for (std::size_t offset = 0; offset < Count; offset++) {
        const NamePart& part = parts[(start + offset) % Count];
        if (part.chars <= maxChars) {
            return &part;
        }
    }

    return nullptr;
}

const NamePart* PickPrefix(int maxChars)
{
    if ((NextRandom() & 1u) != 0) {
        return PickFittingPart(kEnglishPrefixes, maxChars);
    }

    return PickFittingPart(kRussianPrefixes, maxChars);
}

const NamePart* PickCore(int maxChars)
{
    if ((NextRandom() & 1u) != 0) {
        return PickFittingPart(kEnglishCores, maxChars);
    }

    return PickFittingPart(kRussianCores, maxChars);
}

const NamePart* PickSuffix(int maxChars)
{
    if ((NextRandom() & 1u) != 0) {
        return PickFittingPart(kEnglishSuffixes, maxChars);
    }

    return PickFittingPart(kRussianSuffixes, maxChars);
}

void AppendDigit(char* buffer, std::size_t capacity, std::size_t& position, int& chars)
{
    AppendByte(buffer, capacity, position, static_cast<unsigned char>('0' + (NextRandom() % 10u)));
    chars++;
}

void AppendUniqueNameSuffix(int syntheticIndex, char* buffer, std::size_t capacity, std::size_t& position, int& chars)
{
    const std::size_t alphabetSize = sizeof(kBase62Alphabet) - 1;
    const std::size_t first = static_cast<std::size_t>(syntheticIndex) % alphabetSize;
    const std::size_t second = (static_cast<std::size_t>(syntheticIndex) / alphabetSize) % alphabetSize;

    AppendByte(buffer, capacity, position, static_cast<unsigned char>(kBase62Alphabet[first]));
    AppendByte(buffer, capacity, position, static_cast<unsigned char>(kBase62Alphabet[second]));
    chars += 2;
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
    int chars = 0;

    for (int index = 0; index < kMinSyntheticNameLength && position + 1 < capacity; index++) {
        AppendByte(buffer, capacity, position, static_cast<unsigned char>(kBase62Alphabet[value % alphabetSize]));
        value /= static_cast<uint32_t>(alphabetSize);
        chars++;
    }

    while (chars < kMinSyntheticNameLength) {
        AppendDigit(buffer, capacity, position, chars);
    }

    buffer[position] = '\0';
}

void GenerateSyntheticName(int syntheticIndex, char* buffer, std::size_t capacity)
{
    for (int attempt = 0; attempt < 64; attempt++) {
        std::size_t position = 0;
        int chars = 0;
        const int targetLength = kMinSyntheticNameLength + static_cast<int>(NextRandom() % (kMaxSyntheticNameLength - kMinSyntheticNameLength + 1));
        const int bodyLength = targetLength - 2;

        if (bodyLength >= 7 && (NextRandom() % 3u) != 0) {
            const NamePart* prefix = PickPrefix(bodyLength - 4);
            if (prefix) {
                AppendPart(buffer, capacity, position, chars, *prefix);
            }
        }

        const NamePart* core = PickCore(bodyLength - chars);
        if (core) {
            AppendPart(buffer, capacity, position, chars, *core);
        }

        while (chars < bodyLength) {
            const int remaining = bodyLength - chars;
            const NamePart* suffix = remaining >= 2 && (NextRandom() % 3u) != 0 ? PickSuffix(remaining) : nullptr;
            if (suffix) {
                AppendPart(buffer, capacity, position, chars, *suffix);
            } else {
                AppendDigit(buffer, capacity, position, chars);
            }
        }

        AppendUniqueNameSuffix(syntheticIndex, buffer, capacity, position, chars);
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
