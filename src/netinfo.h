#pragma once

#include <windows.h>
#include <string>
#include <vector>

struct NicEntry {
    std::wstring name;
    std::wstring ipAddress;
};

// Get all network adapters with IPv4 addresses
std::vector<NicEntry> GetNetworkAdapters();

// Get IP address for configured NIC. "auto" picks first non-loopback.
std::wstring GetIPAddress(const std::wstring& nicName);

// Get machine hostname
std::wstring GetHostname();
