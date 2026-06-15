#include "a2s_query.h"
#include "sendto_hook.h"
#include "steam_bots.h"

#include <cstdint>

#include <sdk/amxxmodule.h>

void StartFrame()
{
    hide_bots::InstallSendToHook();
    hide_bots::HideSteamBots();

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
}

void OnAmxxDetach()
{
    hide_bots::RestoreSendToHook();
}
