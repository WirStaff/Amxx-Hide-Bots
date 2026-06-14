#include <isteamgameserver.h>

#include <sdk/amxxmodule.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

using SteamGameServerFn = ISteamGameServer* (S_CALLTYPE*)();

static ISteamGameServer* g_SteamGameServer = nullptr;
static int g_BotsCount = 0;

#ifdef _WIN32
using SteamApiLibrary = HMODULE;

struct SteamApiLibraryRef {
    SteamApiLibrary library;
    bool shouldClose;
};

static SteamApiLibraryRef OpenSteamApiLibrary()
{
    if (HMODULE library = GetModuleHandleA("steam_api.dll")) {
        return { library, false };
    }

    if (HMODULE library = LoadLibraryA("steam_api.dll")) {
        return { library, true };
    }

    return { nullptr, false };
}

static void* GetSteamSymbol(SteamApiLibrary library, const char* symbol)
{
    return reinterpret_cast<void*>(GetProcAddress(library, symbol));
}

static void CloseSteamApiLibrary(const SteamApiLibraryRef& libraryRef)
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

static SteamApiLibraryRef OpenSteamApiLibrary()
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

static void* GetSteamSymbol(SteamApiLibrary library, const char* symbol)
{
    return dlsym(library, symbol);
}

static void CloseSteamApiLibrary(const SteamApiLibraryRef& libraryRef)
{
    if (libraryRef.library && libraryRef.shouldClose) {
        dlclose(libraryRef.library);
    }
}
#endif

static ISteamGameServer* GetExistingSteamGameServer()
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

static void HideSteamBots()
{
    if (!g_SteamGameServer) {
        g_SteamGameServer = GetExistingSteamGameServer();
    }

    if (g_SteamGameServer) {
        g_SteamGameServer->SetBotPlayerCount(g_BotsCount);
    }
}

void StartFrame()
{
    HideSteamBots();

    SET_META_RESULT(MRES_IGNORED);
}

cell AMX_NATIVE_CALL NativeSetBotCount(AMX* amx, cell* params)
{
    g_BotsCount = params[1];
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
