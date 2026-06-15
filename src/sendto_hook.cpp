#include "sendto_hook.h"

#include "a2s_query.h"

#include <cstdint>
#include <cstring>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#else
#include <sys/socket.h>
#include <unistd.h>
#endif

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-value"
#pragma GCC diagnostic ignored "-Wextra"
#endif
#include <chooker.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace hide_bots {
namespace {

#ifdef _WIN32
using SocketHandle = SOCKET;
using SendToFn = int (WSAAPI*)(SOCKET, const char*, int, int, const sockaddr*, int);
constexpr int kSendToError = SOCKET_ERROR;
#else
using SocketHandle = int;
using SendToFn = ssize_t (*)(int, const void*, size_t, int, const struct sockaddr*, socklen_t);
constexpr ssize_t kSendToError = -1;
#endif

struct SendToHook {
    const char* moduleName = nullptr;
    SendToFn original = nullptr;
    CFunc* hook = nullptr;
};

CHooker g_Hooker;
bool g_SendToHookInstalled = false;
bool g_SendToHookCallingOriginal = false;

#ifdef _WIN32
SendToHook g_SendToHooks[] = {
    {"ws2_32.dll"},
    {"wsock32.dll"},
};
#else
SendToHook g_SendToHook;
#endif

void* FollowJump(void* address)
{
    auto* code = reinterpret_cast<unsigned char*>(address);

    for (int index = 0; index < 8 && code; index++) {
        if (code[0] == 0xE9) {
            const int32_t offset = *reinterpret_cast<int32_t*>(code + 1);
            code = code + 5 + offset;
            continue;
        }

        if (code[0] == 0xEB) {
            const int8_t offset = *reinterpret_cast<int8_t*>(code + 1);
            code = code + 2 + offset;
            continue;
        }

        if (code[0] == 0xFF && code[1] == 0x25) {
#ifdef _WIN32
            code = *reinterpret_cast<unsigned char**>(*reinterpret_cast<uintptr_t*>(code + 2));
#else
            code = **reinterpret_cast<unsigned char***>(code + 2);
#endif
            continue;
        }

        break;
    }

    return code;
}

#ifdef _WIN32
SendToFn ResolveWindowsSendTo(const char* moduleName)
{
    HMODULE module = GetModuleHandleA(moduleName);
    if (!module) {
        module = LoadLibraryA(moduleName);
    }

    if (!module) {
        return nullptr;
    }

    return reinterpret_cast<SendToFn>(FollowJump(reinterpret_cast<void*>(GetProcAddress(module, "sendto"))));
}
#else
SendToFn ResolveLinuxSendTo()
{
    return reinterpret_cast<SendToFn>(FollowJump(reinterpret_cast<void*>(&sendto)));
}
#endif

bool IsDuplicateOriginal(const SendToHook* current, SendToFn original)
{
#ifdef _WIN32
    for (const SendToHook& hook : g_SendToHooks) {
        if (&hook != current && hook.hook && hook.original == original) {
            return true;
        }
    }
#else
    static_cast<void>(current);
    static_cast<void>(original);
#endif

    return false;
}

bool InstallSendToHook(SendToHook& hook, void* replacement)
{
    if (hook.hook) {
        return true;
    }

#ifdef _WIN32
    hook.original = ResolveWindowsSendTo(hook.moduleName);
#else
    hook.original = ResolveLinuxSendTo();
#endif

    if (!hook.original || IsDuplicateOriginal(&hook, hook.original)) {
        return false;
    }

    hook.hook = g_Hooker.CreateHook(hook.original, replacement, TRUE);
    if (!hook.hook || !hook.hook->Patch()) {
        hook.hook = nullptr;
        return false;
    }

    return true;
}

void RestoreSendToHook(SendToHook& hook)
{
    if (hook.hook) {
        hook.hook->Restore();
        hook.hook = nullptr;
    }
}

template <typename Result, typename Buffer, typename Length, typename To, typename ToLength>
Result CallOriginalSendTo(
    SendToHook& hook,
    SocketHandle socket,
    Buffer buffer,
    Length length,
    int flags,
    To to,
    ToLength toLength)
{
    if (!hook.hook || !hook.original) {
        return static_cast<Result>(kSendToError);
    }

    Result result = static_cast<Result>(kSendToError);
    if (hook.hook->Restore()) {
        g_SendToHookCallingOriginal = true;
        result = hook.original(socket, buffer, length, flags, to, toLength);
        g_SendToHookCallingOriginal = false;
        hook.hook->Patch();
    }

    return result;
}

#ifdef _WIN32
int DispatchHookedSendTo(
    SendToHook& hook,
    SOCKET socket,
    const char* buffer,
    int length,
    int flags,
    const sockaddr* to,
    int toLength)
{
    if (!g_SendToHookCallingOriginal && buffer && length > 0) {
        char rewrittenPacket[4096];
        int rewrittenLength = 0;

        if (RewriteA2SPlayersIndexes(buffer, length, rewrittenPacket, sizeof(rewrittenPacket), rewrittenLength)) {
            return CallOriginalSendTo<int>(
                hook,
                socket,
                rewrittenPacket,
                rewrittenLength,
                flags,
                to,
                toLength);
        }
    }

    return CallOriginalSendTo<int>(hook, socket, buffer, length, flags, to, toLength);
}

int WSAAPI HookedSendToWs2(
    SOCKET socket,
    const char* buffer,
    int length,
    int flags,
    const sockaddr* to,
    int toLength)
{
    return DispatchHookedSendTo(g_SendToHooks[0], socket, buffer, length, flags, to, toLength);
}

int WSAAPI HookedSendToWsock(
    SOCKET socket,
    const char* buffer,
    int length,
    int flags,
    const sockaddr* to,
    int toLength)
{
    return DispatchHookedSendTo(g_SendToHooks[1], socket, buffer, length, flags, to, toLength);
}

bool InstallPlatformSendToHooks()
{
    const bool hookedWs2 = InstallSendToHook(g_SendToHooks[0], reinterpret_cast<void*>(&HookedSendToWs2));
    const bool hookedWsock = InstallSendToHook(g_SendToHooks[1], reinterpret_cast<void*>(&HookedSendToWsock));
    return hookedWs2 || hookedWsock || g_SendToHooks[0].hook || g_SendToHooks[1].hook;
}

void RestorePlatformSendToHooks()
{
    RestoreSendToHook(g_SendToHooks[0]);
    RestoreSendToHook(g_SendToHooks[1]);
}
#else
ssize_t HookedSendTo(
    int socket,
    const void* buffer,
    size_t length,
    int flags,
    const struct sockaddr* to,
    socklen_t toLength)
{
    if (!g_SendToHookCallingOriginal && buffer && length > 0) {
        char rewrittenPacket[4096];
        int rewrittenLength = 0;

        if (length <= static_cast<size_t>(INT32_MAX)
            && RewriteA2SPlayersIndexes(
                reinterpret_cast<const char*>(buffer),
                static_cast<int>(length),
                rewrittenPacket,
                sizeof(rewrittenPacket),
                rewrittenLength)) {
            return CallOriginalSendTo<ssize_t>(
                g_SendToHook,
                socket,
                rewrittenPacket,
                static_cast<size_t>(rewrittenLength),
                flags,
                to,
                toLength);
        }
    }

    return CallOriginalSendTo<ssize_t>(g_SendToHook, socket, buffer, length, flags, to, toLength);
}

bool InstallPlatformSendToHooks()
{
    return InstallSendToHook(g_SendToHook, reinterpret_cast<void*>(&HookedSendTo));
}

void RestorePlatformSendToHooks()
{
    RestoreSendToHook(g_SendToHook);
}
#endif

}

void InstallSendToHook()
{
    if (g_SendToHookInstalled) {
        return;
    }

    g_SendToHookInstalled = InstallPlatformSendToHooks();
}

void RestoreSendToHook()
{
    RestorePlatformSendToHooks();
    g_SendToHookInstalled = false;
}

}
