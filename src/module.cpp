#include "a2s_query.h"
#include "sendto_hook.h"
#include "steam_bots.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <sdk/amxxmodule.h>

namespace {

constexpr char kHiddenRuleCvarPrefix[] = "yb_";
constexpr int kMaxInfoPairsToScan = 512;
constexpr int kMaxHiddenInfoKeysToRemove = 64;

bool HasHiddenRulePrefix(const char* text)
{
    return text
        && std::strncmp(text, kHiddenRuleCvarPrefix, sizeof(kHiddenRuleCvarPrefix) - 1) == 0;
}

bool IsHiddenRuleCvar(const cvar_t* variable)
{
    return variable
        && variable->name
        && HasHiddenRulePrefix(variable->name);
}

void HideRuleCvar(cvar_t* variable)
{
    if (IsHiddenRuleCvar(variable)) {
        variable->flags &= ~FCVAR_SERVER;
    }
}

void HideRuleCvarList(cvar_t* variable)
{
    for (int scanned = 0; variable && scanned < 4096; scanned++, variable = variable->next) {
        HideRuleCvar(variable);
    }
}

bool FindHiddenInfoKey(const char* infoBuffer, char* keyBuffer, std::size_t keyCapacity)
{
    if (!infoBuffer || !keyBuffer || keyCapacity == 0) {
        return false;
    }

    const char* cursor = infoBuffer;

    for (int scanned = 0; *cursor != '\0' && scanned < kMaxInfoPairsToScan; scanned++) {
        if (*cursor == '\\') {
            cursor++;
        }

        const char* keyStart = cursor;
        while (*cursor != '\0' && *cursor != '\\') {
            cursor++;
        }

        const std::size_t keyLength = static_cast<std::size_t>(cursor - keyStart);
        if (*cursor != '\\') {
            return false;
        }

        cursor++;
        while (*cursor != '\0' && *cursor != '\\') {
            cursor++;
        }

        if (keyLength >= sizeof(kHiddenRuleCvarPrefix) - 1
            && std::strncmp(keyStart, kHiddenRuleCvarPrefix, sizeof(kHiddenRuleCvarPrefix) - 1) == 0) {
            const std::size_t copiedLength = keyLength < keyCapacity - 1 ? keyLength : keyCapacity - 1;
            std::memcpy(keyBuffer, keyStart, copiedLength);
            keyBuffer[copiedLength] = '\0';
            return true;
        }
    }

    return false;
}

void RemoveHiddenServerInfoKeys()
{
    if (!g_engfuncs.pfnGetInfoKeyBuffer || !g_engfuncs.pfnInfo_RemoveKey) {
        return;
    }

    char* serverInfo = g_engfuncs.pfnGetInfoKeyBuffer(nullptr);
    if (!serverInfo) {
        return;
    }

    char key[128] = {};
    for (int removed = 0; removed < kMaxHiddenInfoKeysToRemove; removed++) {
        if (!FindHiddenInfoKey(serverInfo, key, sizeof(key))) {
            break;
        }

        g_engfuncs.pfnInfo_RemoveKey(serverInfo, key);
    }
}

void HideKnownRuleCvars()
{
    if (g_engfuncs.pfnCVarGetPointer) {
        HideRuleCvarList(g_engfuncs.pfnCVarGetPointer("yb_version"));
    }

    RemoveHiddenServerInfoKeys();
}

}

void StartFrame()
{
    hide_bots::InstallSendToHook();
    hide_bots::HideSteamBots();
    HideKnownRuleCvars();

    SET_META_RESULT(MRES_IGNORED);
}

void ClientPutInServer(edict_t* pEntity)
{
    hide_bots::MarkPlayerConnected(pEntity);

    RETURN_META(MRES_IGNORED);
}

void ClientDisconnect(edict_t* pEntity)
{
    hide_bots::MarkPlayerDisconnected(pEntity);

    RETURN_META(MRES_IGNORED);
}

int ConnectionlessPacket(const struct netadr_s* net_from, const char* args, char* response_buffer, int* response_buffer_size)
{
    static_cast<void>(net_from);

    uint32_t challenge = 0;
    if (!hide_bots::IsA2SPlayerQuery(args, challenge)) {
        RETURN_META_VALUE(MRES_IGNORED, 0);
    }

    if (!response_buffer || !response_buffer_size || *response_buffer_size <= 0) {
        RETURN_META_VALUE(MRES_SUPERCEDE, 0);
    }

    const int responseSize = hide_bots::IsA2SPlayerInitialChallenge(challenge)
        ? hide_bots::BuildA2SChallengeResponse(response_buffer, *response_buffer_size)
        : hide_bots::BuildA2SPlayersResponse(response_buffer, *response_buffer_size);

    *response_buffer_size = responseSize;

    RETURN_META_VALUE(MRES_SUPERCEDE, responseSize > 0 ? 1 : 0);
}

void CVarRegister(cvar_t* pCvar)
{
    HideRuleCvar(pCvar);

    RETURN_META(MRES_IGNORED);
}

void Cvar_RegisterVariable(cvar_t* variable)
{
    HideRuleCvar(variable);

    RETURN_META(MRES_IGNORED);
}

void CVarRegister_Post(cvar_t* pCvar)
{
    HideRuleCvar(pCvar);
    HideKnownRuleCvars();

    RETURN_META(MRES_IGNORED);
}

void Cvar_RegisterVariable_Post(cvar_t* variable)
{
    HideRuleCvar(variable);
    HideKnownRuleCvars();

    RETURN_META(MRES_IGNORED);
}

cell AMX_NATIVE_CALL NativeSetBotCount(AMX* amx, cell* params)
{
    static_cast<void>(amx);

    hide_bots::SetBotCount(params[1]);
    return 0;
}

AMX_NATIVE_INFO g_Natives[] =
{
    {"hb_set_bot_count", NativeSetBotCount},
    {nullptr, nullptr},
};

void OnAmxxAttach()
{
    MF_AddNatives(g_Natives);
    HideKnownRuleCvars();
}

void OnAmxxDetach()
{
    hide_bots::RestoreSendToHook();
}
