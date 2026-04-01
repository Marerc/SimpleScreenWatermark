#include "netinfo.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

std::vector<NicEntry> GetNetworkAdapters() {
    std::vector<NicEntry> result;

    ULONG bufLen = 15000;
    PIP_ADAPTER_ADDRESSES addresses = nullptr;
    ULONG ret;

    do {
        addresses = (PIP_ADAPTER_ADDRESSES)malloc(bufLen);
        if (!addresses) return result;
        ret = GetAdaptersAddresses(AF_INET,
            GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
            nullptr, addresses, &bufLen);
        if (ret == ERROR_BUFFER_OVERFLOW) {
            free(addresses);
            addresses = nullptr;
        }
    } while (ret == ERROR_BUFFER_OVERFLOW);

    if (ret != NO_ERROR) {
        if (addresses) free(addresses);
        return result;
    }

    for (auto addr = addresses; addr; addr = addr->Next) {
        if (addr->OperStatus != IfOperStatusUp) continue;
        if (addr->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;

        for (auto ua = addr->FirstUnicastAddress; ua; ua = ua->Next) {
            if (ua->Address.lpSockaddr->sa_family != AF_INET) continue;

            sockaddr_in* sa = (sockaddr_in*)ua->Address.lpSockaddr;
            wchar_t ipBuf[64];
            InetNtopW(AF_INET, &sa->sin_addr, ipBuf, _countof(ipBuf));

            NicEntry entry;
            entry.name = addr->FriendlyName;
            entry.ipAddress = ipBuf;
            result.push_back(entry);
        }
    }

    free(addresses);
    return result;
}

std::wstring GetIPAddress(const std::wstring& nicName) {
    auto adapters = GetNetworkAdapters();
    if (adapters.empty()) return L"N/A";

    if (nicName == L"auto" || nicName.empty()) {
        return adapters[0].ipAddress;
    }

    for (const auto& nic : adapters) {
        if (nic.name == nicName) {
            return nic.ipAddress;
        }
    }

    // Fallback to first adapter
    return adapters[0].ipAddress;
}

std::wstring GetHostname() {
    wchar_t buf[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD size = _countof(buf);
    if (GetComputerNameW(buf, &size)) {
        return buf;
    }
    return L"UNKNOWN";
}
