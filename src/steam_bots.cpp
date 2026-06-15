#include "steam_bots.h"

#include <isteamgameserver.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace hide_bots {
namespace {

using SteamGameServerFn = ISteamGameServer* (S_CALLTYPE*)();

ISteamGameServer* g_SteamGameServer = nullptr;
int g_BotsCount = 0;
constexpr int kMaxSteamBotCount = 255;

int NormalizeBotCount(int count)
{
    if (count < 0) {
        return 0;
    }

    if (count > kMaxSteamBotCount) {
        return kMaxSteamBotCount;
    }

    return count;
}

#ifdef _WIN32
using SteamApiLibrary = HMODULE;

struct SteamApiLibraryRef {
    SteamApiLibrary library;
    bool shouldClose;
};

SteamApiLibraryRef OpenSteamApiLibrary()
{
    if (HMODULE library = GetModuleHandleA("steam_api.dll")) {
        return { library, false };
    }

    if (HMODULE library = LoadLibraryA("steam_api.dll")) {
        return { library, true };
    }

    return { nullptr, false };
}

void* GetSteamSymbol(SteamApiLibrary library, const char* symbol)
{
    return reinterpret_cast<void*>(GetProcAddress(library, symbol));
}

void CloseSteamApiLibrary(const SteamApiLibraryRef& libraryRef)
{
    if (libraryRef.library && libraryRef.shouldClose) {
        FreeLibrary(libraryRef.library);
    }
}
#else
using SteamApiLibrary = void*;

struct SteamApiLibraryRef {
    SteamApiLibrary library;
    bool shouldClose;
};

SteamApiLibraryRef OpenSteamApiLibrary()
{
#ifdef RTLD_NOLOAD
    if (SteamApiLibrary library = dlopen("libsteam_api.so", RTLD_NOW | RTLD_NOLOAD)) {
        return { library, true };
    }
#endif

    if (SteamApiLibrary library = dlopen("libsteam_api.so", RTLD_NOW | RTLD_LOCAL)) {
        return { library, true };
    }

    return { nullptr, false };
}

void* GetSteamSymbol(SteamApiLibrary library, const char* symbol)
{
    return dlsym(library, symbol);
}

void CloseSteamApiLibrary(const SteamApiLibraryRef& libraryRef)
{
    if (libraryRef.library && libraryRef.shouldClose) {
        dlclose(libraryRef.library);
    }
}
#endif

ISteamGameServer* GetExistingSteamGameServer()
{
    SteamApiLibraryRef libraryRef = OpenSteamApiLibrary();
    if (!libraryRef.library) {
        return nullptr;
    }

    ISteamGameServer* gameServer = nullptr;

    auto steamGameServer = reinterpret_cast<SteamGameServerFn>(
        GetSteamSymbol(libraryRef.library, "SteamGameServer"));
    if (steamGameServer) {
        gameServer = steamGameServer();
    }

    if (!gameServer) {
        auto steamApiGameServer = reinterpret_cast<SteamGameServerFn>(
            GetSteamSymbol(libraryRef.library, "SteamAPI_SteamGameServer_v015"));
        if (steamApiGameServer) {
            gameServer = steamApiGameServer();
        }
    }

    CloseSteamApiLibrary(libraryRef);
    return gameServer;
}

}

void SetBotCount(int count)
{
    g_BotsCount = NormalizeBotCount(count);
}

void HideSteamBots()
{
    if (!g_SteamGameServer) {
        g_SteamGameServer = GetExistingSteamGameServer();
    }

    if (g_SteamGameServer) {
        g_SteamGameServer->SetBotPlayerCount(g_BotsCount);
    }
}

}
