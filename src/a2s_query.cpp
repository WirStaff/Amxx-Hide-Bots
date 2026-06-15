#include "a2s_query.h"
#include "synthetic_players.h"

#include <cstdint>
#include <cstring>

extern enginefuncs_t g_engfuncs;
extern globalvars_t* gpGlobals;

namespace hide_bots {
namespace {

constexpr unsigned char kConnectionlessHeader = 0xFF;
constexpr unsigned char kA2SPlayer = 0x55;
constexpr unsigned char kS2APlayer = 0x44;
constexpr unsigned char kS2CChallenge = 0x41;
constexpr uint32_t kNoChallenge = 0xFFFFFFFFu;
constexpr uint32_t kA2SPlayerChallenge = 0x4842504Cu;
constexpr int kMaxTrackedPlayers = 32;

float g_PlayerConnectedAt[kMaxTrackedPlayers + 1] = {};

struct PlayerRecord {
    unsigned char index = 0;
    const char* name = "";
    int32_t score = 0;
    float duration = 0.0f;
};

class ResponseWriter {
public:
    ResponseWriter(char* buffer, int capacity)
        : buffer_(reinterpret_cast<unsigned char*>(buffer)), capacity_(capacity)
    {
    }

    int Position() const
    {
        return position_;
    }

    int Remaining() const
    {
        return capacity_ - position_;
    }

    bool WriteByte(unsigned char value)
    {
        if (Remaining() < 1) {
            return false;
        }

        buffer_[position_++] = value;
        return true;
    }

    bool WriteBytes(const void* data, int size)
    {
        if (size < 0 || Remaining() < size) {
            return false;
        }

        std::memcpy(buffer_ + position_, data, static_cast<size_t>(size));
        position_ += size;
        return true;
    }

    bool WriteUInt32(uint32_t value)
    {
        const unsigned char bytes[4] = {
            static_cast<unsigned char>(value & 0xFFu),
            static_cast<unsigned char>((value >> 8) & 0xFFu),
            static_cast<unsigned char>((value >> 16) & 0xFFu),
            static_cast<unsigned char>((value >> 24) & 0xFFu),
        };

        return WriteBytes(bytes, sizeof(bytes));
    }

    bool WriteInt32(int32_t value)
    {
        return WriteUInt32(static_cast<uint32_t>(value));
    }

    bool WriteFloat(float value)
    {
        uint32_t bits = 0;
        static_assert(sizeof(bits) == sizeof(value), "Unexpected float size");
        std::memcpy(&bits, &value, sizeof(bits));
        return WriteUInt32(bits);
    }

    void SetByte(int position, unsigned char value)
    {
        if (position >= 0 && position < position_) {
            buffer_[position] = value;
        }
    }

private:
    unsigned char* buffer_ = nullptr;
    int capacity_ = 0;
    int position_ = 0;
};

uint32_t ReadUInt32(const unsigned char* data)
{
    return static_cast<uint32_t>(data[0])
        | (static_cast<uint32_t>(data[1]) << 8)
        | (static_cast<uint32_t>(data[2]) << 16)
        | (static_cast<uint32_t>(data[3]) << 24);
}

float ReadFloat(const unsigned char* data)
{
    const uint32_t bits = ReadUInt32(data);
    float value = 0.0f;
    static_assert(sizeof(bits) == sizeof(value), "Unexpected float size");
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

bool WriteConnectionlessHeader(ResponseWriter& writer)
{
    return writer.WriteByte(kConnectionlessHeader)
        && writer.WriteByte(kConnectionlessHeader)
        && writer.WriteByte(kConnectionlessHeader)
        && writer.WriteByte(kConnectionlessHeader);
}

int GetMaxPlayers()
{
    if (!gpGlobals || gpGlobals->maxClients <= 0) {
        return 0;
    }

    return gpGlobals->maxClients > kMaxTrackedPlayers ? kMaxTrackedPlayers : gpGlobals->maxClients;
}

const char* GetEngineString(string_t value)
{
    if (!value || !gpGlobals || !gpGlobals->pStringBase) {
        return "";
    }

    return gpGlobals->pStringBase + value;
}

bool IsActivePlayer(const edict_t* entity)
{
    if (!entity || entity->free) {
        return false;
    }

    const char* name = GetEngineString(entity->v.netname);
    return name[0] != '\0';
}

bool IsFakeClient(const edict_t* entity)
{
    return entity && (entity->v.flags & FL_FAKECLIENT) != 0;
}

float GetPlayerDuration(int slot)
{
    if (!gpGlobals || slot < 1 || slot > kMaxTrackedPlayers) {
        return 0.0f;
    }

    const float connectedAt = g_PlayerConnectedAt[slot];
    if (connectedAt <= 0.0f || gpGlobals->time <= connectedAt) {
        return 0.0f;
    }

    return gpGlobals->time - connectedAt;
}

bool WritePlayerRecord(ResponseWriter& writer, unsigned char index, const char* name, int32_t score, float duration)
{
    const size_t nameLength = std::strlen(name);
    const size_t recordSize = 1 + nameLength + 1 + sizeof(int32_t) + sizeof(float);

    if (recordSize > static_cast<size_t>(writer.Remaining())) {
        return false;
    }

    return writer.WriteByte(index)
        && writer.WriteBytes(name, static_cast<int>(nameLength + 1))
        && writer.WriteInt32(score)
        && writer.WriteFloat(duration);
}

bool WritePlayer(ResponseWriter& writer, unsigned char index, const edict_t* entity, int slot)
{
    char syntheticName[64] = {};
    const bool fakeClient = IsFakeClient(entity);
    const char* name = GetEngineString(entity->v.netname);
    float duration = GetPlayerDuration(slot);

    if (fakeClient) {
        BuildSyntheticPlayerName(index, syntheticName, sizeof(syntheticName));
        name = syntheticName;
        duration = GetSyntheticPlayerDuration(index);
    }

    return WritePlayerRecord(writer, index, name, static_cast<int32_t>(entity->v.frags), duration);
}

bool ReadPlayerRecord(const unsigned char* packetBytes, int packetLength, int& cursor, PlayerRecord& record)
{
    if (cursor >= packetLength) {
        return false;
    }

    record.index = packetBytes[cursor++];
    const int namePosition = cursor;

    while (cursor < packetLength && packetBytes[cursor] != '\0') {
        ++cursor;
    }

    if (cursor >= packetLength) {
        return false;
    }

    record.name = reinterpret_cast<const char*>(packetBytes + namePosition);
    ++cursor;

    if (packetLength - cursor < static_cast<int>(sizeof(int32_t) + sizeof(float))) {
        return false;
    }

    record.score = static_cast<int32_t>(ReadUInt32(packetBytes + cursor));
    cursor += static_cast<int>(sizeof(int32_t));
    record.duration = ReadFloat(packetBytes + cursor);
    cursor += static_cast<int>(sizeof(float));

    return true;
}

int CountZeroIndexes(const unsigned char* packetBytes, int packetLength, int cursor, int originalCount)
{
    int zeroIndexes = 0;
    int parsedCount = 0;

    while (parsedCount < originalCount && cursor < packetLength) {
        PlayerRecord record;
        if (!ReadPlayerRecord(packetBytes, packetLength, cursor, record)) {
            break;
        }

        if (record.index == 0) {
            ++zeroIndexes;
        }

        ++parsedCount;
    }

    return zeroIndexes;
}

}

bool IsA2SPlayerQuery(const char* args, uint32_t& challenge)
{
    if (!args) {
        return false;
    }

    const unsigned char* packet = reinterpret_cast<const unsigned char*>(args);
    if (packet[0] == kConnectionlessHeader
        && packet[1] == kConnectionlessHeader
        && packet[2] == kConnectionlessHeader
        && packet[3] == kConnectionlessHeader) {
        packet += 4;
    }

    if (packet[0] != kA2SPlayer) {
        return false;
    }

    challenge = ReadUInt32(packet + 1);
    return true;
}

bool IsA2SPlayerInitialChallenge(uint32_t challenge)
{
    return challenge == kNoChallenge;
}

int BuildA2SChallengeResponse(char* responseBuffer, int responseBufferSize)
{
    ResponseWriter writer(responseBuffer, responseBufferSize);
    if (!WriteConnectionlessHeader(writer)
        || !writer.WriteByte(kS2CChallenge)
        || !writer.WriteUInt32(kA2SPlayerChallenge)) {
        return 0;
    }

    return writer.Position();
}

int BuildA2SPlayersResponse(char* responseBuffer, int responseBufferSize)
{
    ResponseWriter writer(responseBuffer, responseBufferSize);
    if (!WriteConnectionlessHeader(writer) || !writer.WriteByte(kS2APlayer)) {
        return 0;
    }

    const int countPosition = writer.Position();
    if (!writer.WriteByte(0)) {
        return 0;
    }

    unsigned char playerCount = 0;
    const int maxPlayers = GetMaxPlayers();

    for (int slot = 1; slot <= maxPlayers && playerCount < static_cast<unsigned char>(maxPlayers); slot++) {
        edict_t* player = g_engfuncs.pfnPEntityOfEntIndex ? g_engfuncs.pfnPEntityOfEntIndex(slot) : nullptr;
        if (!IsActivePlayer(player)) {
            continue;
        }

        if (!WritePlayer(writer, playerCount, player, slot)) {
            break;
        }

        playerCount++;
    }

    writer.SetByte(countPosition, playerCount);
    return writer.Position();
}

bool RewriteA2SPlayersIndexes(
    const char* packet,
    int packetLength,
    char* rewrittenPacket,
    int rewrittenCapacity,
    int& rewrittenLength)
{
    if (!packet || !rewrittenPacket || packetLength <= 0 || packetLength > rewrittenCapacity) {
        return false;
    }

    const unsigned char* packetBytes = reinterpret_cast<const unsigned char*>(packet);
    int payloadOffset = 0;

    if (packetLength >= 5
        && packetBytes[0] == kConnectionlessHeader
        && packetBytes[1] == kConnectionlessHeader
        && packetBytes[2] == kConnectionlessHeader
        && packetBytes[3] == kConnectionlessHeader) {
        payloadOffset = 4;
    }

    if (packetLength < payloadOffset + 2 || packetBytes[payloadOffset] != kS2APlayer) {
        return false;
    }

    const int countPosition = payloadOffset + 1;
    const int originalCount = packetBytes[countPosition];
    const int maxPlayers = GetMaxPlayers();
    const int wantedCount = maxPlayers > 0 && originalCount > maxPlayers ? maxPlayers : originalCount;
    const int recordsPosition = payloadOffset + 2;
    const int zeroIndexes = CountZeroIndexes(packetBytes, packetLength, recordsPosition, originalCount);
    const bool rewriteSyntheticBots = zeroIndexes > 1;

    ResponseWriter writer(rewrittenPacket, rewrittenCapacity);
    if (payloadOffset == 4 && !WriteConnectionlessHeader(writer)) {
        return false;
    }

    if (!writer.WriteByte(kS2APlayer)) {
        return false;
    }

    const int rewrittenCountPosition = writer.Position();
    if (!writer.WriteByte(0)) {
        return false;
    }

    int cursor = recordsPosition;
    int parsedCount = 0;
    int visibleCount = 0;

    while (parsedCount < originalCount && cursor < packetLength) {
        PlayerRecord record;
        if (!ReadPlayerRecord(packetBytes, packetLength, cursor, record)) {
            break;
        }

        if (visibleCount < wantedCount) {
            char syntheticName[64] = {};
            const char* name = record.name;
            float duration = record.duration;

            if (rewriteSyntheticBots && record.index == 0) {
                BuildSyntheticPlayerName(visibleCount, syntheticName, sizeof(syntheticName));
                name = syntheticName;
                duration = GetSyntheticPlayerDuration(visibleCount);
            }

            if (!WritePlayerRecord(
                    writer,
                    static_cast<unsigned char>(visibleCount),
                    name,
                    record.score,
                    duration)) {
                break;
            }

            ++visibleCount;
        }

        ++parsedCount;
    }

    if (originalCount > 0 && visibleCount == 0) {
        return false;
    }

    writer.SetByte(rewrittenCountPosition, static_cast<unsigned char>(visibleCount));
    rewrittenLength = writer.Position();

    return visibleCount > 0 || originalCount != 0;
}

int GetPlayerSlot(const edict_t* entity)
{
    if (!entity || !g_engfuncs.pfnIndexOfEdict) {
        return 0;
    }

    const int slot = g_engfuncs.pfnIndexOfEdict(entity);
    if (slot < 1 || slot > kMaxTrackedPlayers) {
        return 0;
    }

    return slot;
}

void MarkPlayerConnected(edict_t* entity)
{
    const int slot = GetPlayerSlot(entity);
    if (slot > 0) {
        g_PlayerConnectedAt[slot] = gpGlobals ? gpGlobals->time : 0.0f;
    }
}

void MarkPlayerDisconnected(edict_t* entity)
{
    const int slot = GetPlayerSlot(entity);
    if (slot > 0) {
        g_PlayerConnectedAt[slot] = 0.0f;
    }
}

}
