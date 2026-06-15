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
#include <dlfcn.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace hide_bots {

#ifdef _WIN32
namespace {

struct InlineHook {
    void* exportedFunction = nullptr;
    void* target = nullptr;
    unsigned char original[5] = {};
    bool installed = false;
};

InlineHook g_SendToHooks[2] = {};
bool g_SendToHookInstalled = false;
bool g_SendToHookCallingOriginal = false;
using SendToFn = int (WSAAPI*)(SOCKET, const char*, int, int, const sockaddr*, int);

void* FollowJump(void* address)
{
    auto* code = reinterpret_cast<unsigned char*>(address);

    for (int i = 0; i < 8 && code; ++i) {
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
            code = *reinterpret_cast<unsigned char**>(*reinterpret_cast<uintptr_t*>(code + 2));
            continue;
        }

        break;
    }

    return code;
}

bool InstallInlineHook(InlineHook& hook, void* exportedFunction, void* replacement);
void RestoreInlineHook(InlineHook& hook);
int WSAAPI HookedSendToWs2(SOCKET socket, const char* buffer, int length, int flags, const sockaddr* to, int toLength);
int WSAAPI HookedSendToWsock(SOCKET socket, const char* buffer, int length, int flags, const sockaddr* to, int toLength);

int CallOriginalSendTo(InlineHook& activeHook, SOCKET socket, const char* buffer, int length, int flags, const sockaddr* to, int toLength)
{
    if (!activeHook.exportedFunction) {
        return SOCKET_ERROR;
    }

    RestoreInlineHook(g_SendToHooks[0]);
    RestoreInlineHook(g_SendToHooks[1]);

    g_SendToHookCallingOriginal = true;
    const int result = reinterpret_cast<SendToFn>(activeHook.exportedFunction)(socket, buffer, length, flags, to, toLength);
    g_SendToHookCallingOriginal = false;

    InstallInlineHook(g_SendToHooks[0], g_SendToHooks[0].exportedFunction, reinterpret_cast<void*>(&HookedSendToWs2));
    InstallInlineHook(g_SendToHooks[1], g_SendToHooks[1].exportedFunction, reinterpret_cast<void*>(&HookedSendToWsock));

    return result;
}

int DispatchHookedSendTo(InlineHook& activeHook, SOCKET socket, const char* buffer, int length, int flags, const sockaddr* to, int toLength)
{
    if (!g_SendToHookCallingOriginal && buffer && length > 0) {
        char rewrittenPacket[4096];
        int rewrittenLength = 0;

        if (RewriteA2SPlayersIndexes(buffer, length, rewrittenPacket, sizeof(rewrittenPacket), rewrittenLength)) {
            return CallOriginalSendTo(activeHook, socket, rewrittenPacket, rewrittenLength, flags, to, toLength);
        }
    }

    return CallOriginalSendTo(activeHook, socket, buffer, length, flags, to, toLength);
}

int WSAAPI HookedSendToWs2(SOCKET socket, const char* buffer, int length, int flags, const sockaddr* to, int toLength)
{
    return DispatchHookedSendTo(g_SendToHooks[0], socket, buffer, length, flags, to, toLength);
}

int WSAAPI HookedSendToWsock(SOCKET socket, const char* buffer, int length, int flags, const sockaddr* to, int toLength)
{
    return DispatchHookedSendTo(g_SendToHooks[1], socket, buffer, length, flags, to, toLength);
}

bool InstallInlineHook(InlineHook& hook, void* exportedFunction, void* replacement)
{
    if (hook.installed || !exportedFunction || !replacement) {
        return false;
    }

    auto* target = reinterpret_cast<unsigned char*>(FollowJump(exportedFunction));
    if (!target) {
        return false;
    }

    const intptr_t relativeJump = reinterpret_cast<unsigned char*>(replacement) - (target + 5);
    if (relativeJump < INT32_MIN || relativeJump > INT32_MAX) {
        return false;
    }

    DWORD oldProtect = 0;
    if (!VirtualProtect(target, sizeof(hook.original), PAGE_EXECUTE_READWRITE, &oldProtect)) {
        return false;
    }

    std::memcpy(hook.original, target, sizeof(hook.original));

    target[0] = 0xE9;
    *reinterpret_cast<int32_t*>(target + 1) = static_cast<int32_t>(relativeJump);

    DWORD ignoredProtect = 0;
    VirtualProtect(target, sizeof(hook.original), oldProtect, &ignoredProtect);
    FlushInstructionCache(GetCurrentProcess(), target, sizeof(hook.original));

    hook.exportedFunction = exportedFunction;
    hook.target = target;
    hook.installed = true;
    return true;
}

void RestoreInlineHook(InlineHook& hook)
{
    if (!hook.installed || !hook.target) {
        return;
    }

    DWORD oldProtect = 0;
    if (!VirtualProtect(hook.target, sizeof(hook.original), PAGE_EXECUTE_READWRITE, &oldProtect)) {
        return;
    }

    std::memcpy(hook.target, hook.original, sizeof(hook.original));

    DWORD ignoredProtect = 0;
    VirtualProtect(hook.target, sizeof(hook.original), oldProtect, &ignoredProtect);
    FlushInstructionCache(GetCurrentProcess(), hook.target, sizeof(hook.original));

    hook.target = nullptr;
    hook.installed = false;
}

bool InstallSendToHookFromModule(InlineHook& hook, const char* moduleName)
{
    HMODULE winsock = GetModuleHandleA(moduleName);
    if (!winsock) {
        winsock = LoadLibraryA(moduleName);
    }

    if (!winsock) {
        return false;
    }

    return InstallInlineHook(
        hook,
        reinterpret_cast<void*>(GetProcAddress(winsock, "sendto")),
        &hook == &g_SendToHooks[0]
            ? reinterpret_cast<void*>(&HookedSendToWs2)
            : reinterpret_cast<void*>(&HookedSendToWsock));
}

}
#else
namespace {

struct InlineHook {
    void* exportedFunction = nullptr;
    void* target = nullptr;
    unsigned char original[5] = {};
    bool installed = false;
};

InlineHook g_SendToHook = {};
bool g_SendToHookInstalled = false;
bool g_SendToHookCallingOriginal = false;
using SendToFn = ssize_t (*)(int, const void*, size_t, int, const struct sockaddr*, socklen_t);

void* FollowJump(void* address)
{
    auto* code = reinterpret_cast<unsigned char*>(address);

    for (int i = 0; i < 8 && code; ++i) {
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
            code = *reinterpret_cast<unsigned char**>(*reinterpret_cast<uintptr_t*>(code + 2));
            continue;
        }

        break;
    }

    return code;
}

bool ChangeProtection(void* address, size_t size, int protection)
{
    const long pageSize = sysconf(_SC_PAGESIZE);
    if (pageSize <= 0) {
        return false;
    }

    const uintptr_t rawAddress = reinterpret_cast<uintptr_t>(address);
    const uintptr_t pageStart = rawAddress & ~(static_cast<uintptr_t>(pageSize) - 1);
    const uintptr_t pageEnd = (rawAddress + size + static_cast<uintptr_t>(pageSize) - 1)
        & ~(static_cast<uintptr_t>(pageSize) - 1);

    return mprotect(reinterpret_cast<void*>(pageStart), pageEnd - pageStart, protection) == 0;
}

bool InstallInlineHook(InlineHook& hook, void* exportedFunction, void* replacement);
void RestoreInlineHook(InlineHook& hook);
ssize_t HookedSendTo(int socket, const void* buffer, size_t length, int flags, const struct sockaddr* to, socklen_t toLength);

ssize_t CallOriginalSendTo(
    InlineHook& activeHook,
    int socket,
    const void* buffer,
    size_t length,
    int flags,
    const struct sockaddr* to,
    socklen_t toLength)
{
    if (!activeHook.exportedFunction) {
        return -1;
    }

    RestoreInlineHook(g_SendToHook);

    g_SendToHookCallingOriginal = true;
    const ssize_t result = reinterpret_cast<SendToFn>(activeHook.exportedFunction)(
        socket,
        buffer,
        length,
        flags,
        to,
        toLength);
    g_SendToHookCallingOriginal = false;

    InstallInlineHook(g_SendToHook, g_SendToHook.exportedFunction, reinterpret_cast<void*>(&HookedSendTo));

    return result;
}

ssize_t HookedSendTo(int socket, const void* buffer, size_t length, int flags, const struct sockaddr* to, socklen_t toLength)
{
    if (!g_SendToHookCallingOriginal && buffer && length > 0 && length <= static_cast<size_t>(INT32_MAX)) {
        char rewrittenPacket[4096];
        int rewrittenLength = 0;

        if (RewriteA2SPlayersIndexes(
                reinterpret_cast<const char*>(buffer),
                static_cast<int>(length),
                rewrittenPacket,
                sizeof(rewrittenPacket),
                rewrittenLength)) {
            return CallOriginalSendTo(
                g_SendToHook,
                socket,
                rewrittenPacket,
                static_cast<size_t>(rewrittenLength),
                flags,
                to,
                toLength);
        }
    }

    return CallOriginalSendTo(g_SendToHook, socket, buffer, length, flags, to, toLength);
}

bool InstallInlineHook(InlineHook& hook, void* exportedFunction, void* replacement)
{
    if (hook.installed || !exportedFunction || !replacement) {
        return false;
    }

    auto* target = reinterpret_cast<unsigned char*>(FollowJump(exportedFunction));
    if (!target) {
        return false;
    }

    const intptr_t relativeJump = reinterpret_cast<unsigned char*>(replacement) - (target + 5);
    if (relativeJump < INT32_MIN || relativeJump > INT32_MAX) {
        return false;
    }

    if (!ChangeProtection(target, sizeof(hook.original), PROT_READ | PROT_WRITE | PROT_EXEC)) {
        return false;
    }

    std::memcpy(hook.original, target, sizeof(hook.original));

    target[0] = 0xE9;
    *reinterpret_cast<int32_t*>(target + 1) = static_cast<int32_t>(relativeJump);

    __builtin___clear_cache(reinterpret_cast<char*>(target), reinterpret_cast<char*>(target + sizeof(hook.original)));
    ChangeProtection(target, sizeof(hook.original), PROT_READ | PROT_EXEC);

    hook.exportedFunction = exportedFunction;
    hook.target = target;
    hook.installed = true;
    return true;
}

void RestoreInlineHook(InlineHook& hook)
{
    if (!hook.installed || !hook.target) {
        return;
    }

    if (!ChangeProtection(hook.target, sizeof(hook.original), PROT_READ | PROT_WRITE | PROT_EXEC)) {
        return;
    }

    std::memcpy(hook.target, hook.original, sizeof(hook.original));

    __builtin___clear_cache(
        reinterpret_cast<char*>(hook.target),
        reinterpret_cast<char*>(reinterpret_cast<unsigned char*>(hook.target) + sizeof(hook.original)));
    ChangeProtection(hook.target, sizeof(hook.original), PROT_READ | PROT_EXEC);

    hook.target = nullptr;
    hook.installed = false;
}

}
#endif

void InstallSendToHook()
{
#ifdef _WIN32
    if (g_SendToHookInstalled) {
        return;
    }

    const bool hookedWs2 = InstallSendToHookFromModule(g_SendToHooks[0], "ws2_32.dll");
    const bool hookedWsock = InstallSendToHookFromModule(g_SendToHooks[1], "wsock32.dll");
    g_SendToHookInstalled = hookedWs2 || hookedWsock;
#else
    if (g_SendToHookInstalled) {
        return;
    }

    void* sendtoFunction = dlsym(RTLD_NEXT, "sendto");
    if (!sendtoFunction) {
        sendtoFunction = dlsym(RTLD_DEFAULT, "sendto");
    }

    if (!sendtoFunction) {
        sendtoFunction = reinterpret_cast<void*>(&sendto);
    }

    g_SendToHookInstalled = InstallInlineHook(
        g_SendToHook,
        sendtoFunction,
        reinterpret_cast<void*>(&HookedSendTo));
#endif
}

void RestoreSendToHook()
{
#ifdef _WIN32
    RestoreInlineHook(g_SendToHooks[0]);
    RestoreInlineHook(g_SendToHooks[1]);
    g_SendToHookInstalled = false;
#else
    RestoreInlineHook(g_SendToHook);
    g_SendToHookInstalled = false;
#endif
}

}
