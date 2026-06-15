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
#include <cstdio>
#include <dlfcn.h>
#include <enums.h>
#include <netadr.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/uio.h>
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

    for (int i = 0; i < 8 && code; i++) {
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

enum class HookKind {
    SendTo,
    Send,
    SendMsg,
    NetSendPacket
};

struct InlineHook {
    const char* symbol = nullptr;
    HookKind kind = HookKind::SendTo;
    void* replacement = nullptr;
    void* exportedFunction = nullptr;
    void* target = nullptr;
    unsigned char original[5] = {};
    bool installed = false;
};

ssize_t HookedSendTo(int socket, const void* buffer, size_t length, int flags, const struct sockaddr* to, socklen_t toLength);
ssize_t HookedSend(int socket, const void* buffer, size_t length, int flags);
ssize_t HookedSendMsg(int socket, const struct msghdr* message, int flags);
void HookedNetSendPacket(netsrc_t socket, int length, void* data, netadr_t to);

InlineHook g_SendHooks[] = {
    {"sendto", HookKind::SendTo, reinterpret_cast<void*>(&HookedSendTo)},
    {"__sendto", HookKind::SendTo, reinterpret_cast<void*>(&HookedSendTo)},
    {"__libc_sendto", HookKind::SendTo, reinterpret_cast<void*>(&HookedSendTo)},
    {"send", HookKind::Send, reinterpret_cast<void*>(&HookedSend)},
    {"__send", HookKind::Send, reinterpret_cast<void*>(&HookedSend)},
    {"__libc_send", HookKind::Send, reinterpret_cast<void*>(&HookedSend)},
    {"sendmsg", HookKind::SendMsg, reinterpret_cast<void*>(&HookedSendMsg)},
    {"__sendmsg", HookKind::SendMsg, reinterpret_cast<void*>(&HookedSendMsg)},
    {"__libc_sendmsg", HookKind::SendMsg, reinterpret_cast<void*>(&HookedSendMsg)},
    {"NET_SendPacket", HookKind::NetSendPacket, reinterpret_cast<void*>(&HookedNetSendPacket)},
    {"_Z14NET_SendPacket8netsrc_siPv8netadr_s", HookKind::NetSendPacket, reinterpret_cast<void*>(&HookedNetSendPacket)},
    {"_Z14NET_SendPacket8netsrc_tiPv8netadr_t", HookKind::NetSendPacket, reinterpret_cast<void*>(&HookedNetSendPacket)},
    {"NET_SendPacket__F8netsrc_siPv8netadr_s", HookKind::NetSendPacket, reinterpret_cast<void*>(&HookedNetSendPacket)},
    {"NET_SendPacket__F8netsrc_tiPv8netadr_t", HookKind::NetSendPacket, reinterpret_cast<void*>(&HookedNetSendPacket)},
};

bool g_SendHookInstalled = false;
bool g_SendHookCallingOriginal = false;
using SendToFn = ssize_t (*)(int, const void*, size_t, int, const struct sockaddr*, socklen_t);
using SendFn = ssize_t (*)(int, const void*, size_t, int);
using SendMsgFn = ssize_t (*)(int, const struct msghdr*, int);
using NetSendPacketFn = void (*)(netsrc_t, int, void*, netadr_t);

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

void* ResolveSymbolInLoadedModule(const char* moduleName, const char* symbol)
{
#ifdef RTLD_NOLOAD
    void* module = dlopen(moduleName, RTLD_NOW | RTLD_NOLOAD);
    if (!module) {
        return nullptr;
    }

    void* resolved = dlsym(module, symbol);
    dlclose(module);
    return resolved;
#else
    static_cast<void>(moduleName);
    static_cast<void>(symbol);
    return nullptr;
#endif
}

void* ResolveSymbolInMappedModule(const char* moduleName, const char* symbol)
{
    FILE* maps = std::fopen("/proc/self/maps", "r");
    if (!maps) {
        return nullptr;
    }

    char line[1024];
    void* resolved = nullptr;

    while (!resolved && std::fgets(line, sizeof(line), maps)) {
        if (!std::strstr(line, moduleName)) {
            continue;
        }

        char* path = std::strchr(line, '/');
        if (!path) {
            continue;
        }

        char* end = std::strchr(path, '\n');
        if (end) {
            *end = '\0';
        }

        resolved = ResolveSymbolInLoadedModule(path, symbol);
    }

    std::fclose(maps);
    return resolved;
}

void* ResolveEngineSymbol(const char* symbol)
{
    const char* modules[] = {
        "engine_i486.so",
        "engine_i686.so",
        "engine_amd.so",
        "engine.so",
        "swds.so",
        "hw.so",
    };

    for (const char* moduleName : modules) {
        if (void* resolved = ResolveSymbolInLoadedModule(moduleName, symbol)) {
            return resolved;
        }

        if (void* resolved = ResolveSymbolInMappedModule(moduleName, symbol)) {
            return resolved;
        }
    }

    return nullptr;
}

void* ResolveSymbol(const char* symbol)
{
    void* resolved = dlsym(RTLD_NEXT, symbol);
    if (!resolved) {
        resolved = dlsym(RTLD_DEFAULT, symbol);
    }

    if (!resolved && std::strcmp(symbol, "sendto") == 0) {
        resolved = reinterpret_cast<void*>(&sendto);
    } else if (!resolved && std::strcmp(symbol, "send") == 0) {
        resolved = reinterpret_cast<void*>(&send);
    } else if (!resolved && std::strcmp(symbol, "sendmsg") == 0) {
        resolved = reinterpret_cast<void*>(&sendmsg);
    } else if (!resolved && std::strstr(symbol, "NET_SendPacket")) {
        resolved = ResolveEngineSymbol(symbol);
    }

    return resolved;
}

bool HasInstalledHook()
{
    for (const InlineHook& hook : g_SendHooks) {
        if (hook.installed) {
            return true;
        }
    }

    return false;
}

bool IsTargetHookedByAnother(const InlineHook& hook, void* target)
{
    for (const InlineHook& installedHook : g_SendHooks) {
        if (&installedHook != &hook && installedHook.installed && installedHook.target == target) {
            return true;
        }
    }

    return false;
}

InlineHook& FindHook(HookKind kind, int preferredIndex)
{
    if (g_SendHooks[preferredIndex].kind == kind && g_SendHooks[preferredIndex].exportedFunction) {
        return g_SendHooks[preferredIndex];
    }

    for (InlineHook& hook : g_SendHooks) {
        if (hook.kind == kind && hook.exportedFunction) {
            return hook;
        }
    }

    return g_SendHooks[preferredIndex];
}

bool InstallAllInlineHooks()
{
    bool installed = false;

    for (InlineHook& hook : g_SendHooks) {
        if (!hook.exportedFunction) {
            hook.exportedFunction = ResolveSymbol(hook.symbol);
        }

        installed = InstallInlineHook(hook, hook.exportedFunction, hook.replacement) || installed;
    }

    return installed || HasInstalledHook();
}

void RestoreAllInlineHooks()
{
    for (InlineHook& hook : g_SendHooks) {
        RestoreInlineHook(hook);
    }
}

bool TryRewritePacket(const void* packet, size_t packetLength, char* rewrittenPacket, int rewrittenCapacity, int& rewrittenLength)
{
    if (!packet || packetLength == 0 || packetLength > static_cast<size_t>(rewrittenCapacity)) {
        return false;
    }

    if (packetLength > static_cast<size_t>(INT32_MAX)) {
        return false;
    }

    return RewriteA2SPlayersIndexes(
        reinterpret_cast<const char*>(packet),
        static_cast<int>(packetLength),
        rewrittenPacket,
        rewrittenCapacity,
        rewrittenLength);
}

bool CopyMessagePayload(const struct msghdr* message, char* packet, int packetCapacity, size_t& packetLength)
{
    if (!message || !message->msg_iov || message->msg_iovlen <= 0) {
        return false;
    }

    packetLength = 0;

    for (size_t index = 0; index < static_cast<size_t>(message->msg_iovlen); index++) {
        const iovec& vector = message->msg_iov[index];
        if (!vector.iov_base && vector.iov_len > 0) {
            return false;
        }

        if (vector.iov_len > static_cast<size_t>(packetCapacity) - packetLength) {
            return false;
        }

        std::memcpy(packet + packetLength, vector.iov_base, vector.iov_len);
        packetLength += vector.iov_len;
    }

    return packetLength > 0;
}

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

    RestoreAllInlineHooks();

    g_SendHookCallingOriginal = true;
    const ssize_t result = reinterpret_cast<SendToFn>(activeHook.exportedFunction)(
        socket,
        buffer,
        length,
        flags,
        to,
        toLength);
    g_SendHookCallingOriginal = false;

    InstallAllInlineHooks();

    return result;
}

ssize_t CallOriginalSend(InlineHook& activeHook, int socket, const void* buffer, size_t length, int flags)
{
    if (!activeHook.exportedFunction) {
        return -1;
    }

    RestoreAllInlineHooks();

    g_SendHookCallingOriginal = true;
    const ssize_t result = reinterpret_cast<SendFn>(activeHook.exportedFunction)(socket, buffer, length, flags);
    g_SendHookCallingOriginal = false;

    InstallAllInlineHooks();

    return result;
}

ssize_t CallOriginalSendMsg(InlineHook& activeHook, int socket, const struct msghdr* message, int flags)
{
    if (!activeHook.exportedFunction) {
        return -1;
    }

    RestoreAllInlineHooks();

    g_SendHookCallingOriginal = true;
    const ssize_t result = reinterpret_cast<SendMsgFn>(activeHook.exportedFunction)(socket, message, flags);
    g_SendHookCallingOriginal = false;

    InstallAllInlineHooks();

    return result;
}

void CallOriginalNetSendPacket(InlineHook& activeHook, netsrc_t socket, int length, void* data, netadr_t to)
{
    if (!activeHook.exportedFunction) {
        return;
    }

    RestoreAllInlineHooks();

    g_SendHookCallingOriginal = true;
    reinterpret_cast<NetSendPacketFn>(activeHook.exportedFunction)(socket, length, data, to);
    g_SendHookCallingOriginal = false;

    InstallAllInlineHooks();
}

ssize_t HookedSendTo(int socket, const void* buffer, size_t length, int flags, const struct sockaddr* to, socklen_t toLength)
{
    InlineHook& activeHook = FindHook(HookKind::SendTo, 0);

    if (!g_SendHookCallingOriginal) {
        char rewrittenPacket[4096];
        int rewrittenLength = 0;

        if (TryRewritePacket(buffer, length, rewrittenPacket, sizeof(rewrittenPacket), rewrittenLength)) {
            return CallOriginalSendTo(
                activeHook,
                socket,
                rewrittenPacket,
                static_cast<size_t>(rewrittenLength),
                flags,
                to,
                toLength);
        }
    }

    return CallOriginalSendTo(activeHook, socket, buffer, length, flags, to, toLength);
}

ssize_t HookedSend(int socket, const void* buffer, size_t length, int flags)
{
    InlineHook& activeHook = FindHook(HookKind::Send, 3);

    if (!g_SendHookCallingOriginal) {
        char rewrittenPacket[4096];
        int rewrittenLength = 0;

        if (TryRewritePacket(buffer, length, rewrittenPacket, sizeof(rewrittenPacket), rewrittenLength)) {
            return CallOriginalSend(activeHook, socket, rewrittenPacket, static_cast<size_t>(rewrittenLength), flags);
        }
    }

    return CallOriginalSend(activeHook, socket, buffer, length, flags);
}

ssize_t HookedSendMsg(int socket, const struct msghdr* message, int flags)
{
    InlineHook& activeHook = FindHook(HookKind::SendMsg, 6);

    if (!g_SendHookCallingOriginal) {
        char packet[4096];
        char rewrittenPacket[4096];
        size_t packetLength = 0;
        int rewrittenLength = 0;

        if (CopyMessagePayload(message, packet, sizeof(packet), packetLength)
            && TryRewritePacket(packet, packetLength, rewrittenPacket, sizeof(rewrittenPacket), rewrittenLength)) {
            iovec rewrittenVector = {};
            rewrittenVector.iov_base = rewrittenPacket;
            rewrittenVector.iov_len = static_cast<size_t>(rewrittenLength);

            msghdr rewrittenMessage = *message;
            rewrittenMessage.msg_iov = &rewrittenVector;
            rewrittenMessage.msg_iovlen = 1;

            return CallOriginalSendMsg(activeHook, socket, &rewrittenMessage, flags);
        }
    }

    return CallOriginalSendMsg(activeHook, socket, message, flags);
}

void HookedNetSendPacket(netsrc_t socket, int length, void* data, netadr_t to)
{
    InlineHook& activeHook = FindHook(HookKind::NetSendPacket, 9);

    if (!g_SendHookCallingOriginal && length > 0) {
        char rewrittenPacket[4096];
        int rewrittenLength = 0;

        if (TryRewritePacket(data, static_cast<size_t>(length), rewrittenPacket, sizeof(rewrittenPacket), rewrittenLength)) {
            CallOriginalNetSendPacket(activeHook, socket, rewrittenLength, rewrittenPacket, to);
            return;
        }
    }

    CallOriginalNetSendPacket(activeHook, socket, length, data, to);
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

    if (IsTargetHookedByAnother(hook, target)) {
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
    if (g_SendHookInstalled) {
        return;
    }

    g_SendHookInstalled = InstallAllInlineHooks();
#endif
}

void RestoreSendToHook()
{
#ifdef _WIN32
    RestoreInlineHook(g_SendToHooks[0]);
    RestoreInlineHook(g_SendToHooks[1]);
    g_SendToHookInstalled = false;
#else
    RestoreAllInlineHooks();
    g_SendHookInstalled = false;
#endif
}

}
