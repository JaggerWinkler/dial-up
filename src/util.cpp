//
// Created by Jagger Winkler on 11/16/26.
// disclosure note: I wrote this code for MacOS, and had Claude Opus 5.5 extend it to linux and windows
//

#include "util.h"

#include <cstring>

#if defined(_WIN32)
    #include <winsock2.h>
    #include <iphlpapi.h>
#else
    #include <ifaddrs.h>
    #include <net/if.h>
    #include <sys/socket.h>
    #if defined(__APPLE__)
        #include <net/if_dl.h>
    #else
        #include <netpacket/packet.h>
    #endif
#endif

#if defined(_WIN32)

// Windows: iface is the adapter's friendly name as shown in Settings /
// `ipconfig`, e.g. "Wi-Fi" or "Ethernet". Empty picks the adapter that is up
// and has a default gateway, i.e. the one actually connected to the network.
std::optional<MAC_Address> get_MAC(const std::string& iface) {
    constexpr ULONG flags = GAA_FLAG_INCLUDE_GATEWAYS;
    ULONG size = 16 * 1024;
    std::vector<uint8_t> buf(size);
    auto* list = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
    ULONG err = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, list, &size);
    if (err == ERROR_BUFFER_OVERFLOW) {
        buf.resize(size);   // size now holds the required length
        list = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
        err = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, list, &size);
    }
    if (err != NO_ERROR) return std::nullopt;

    const std::wstring wanted(iface.begin(), iface.end());   // adapter names are ASCII
    for (auto* a = list; a; a = a->Next) {
        if (a->PhysicalAddressLength != 6) continue;
        if (iface.empty()) {
            const bool physical = a->IfType == IF_TYPE_ETHERNET_CSMACD || a->IfType == IF_TYPE_IEEE80211;
            if (!physical || a->OperStatus != IfOperStatusUp || !a->FirstGatewayAddress) continue;
        } else if (wanted != a->FriendlyName) {
            continue;
        }
        MAC_Address mac{};
        std::memcpy(mac.data(), a->PhysicalAddress, 6);
        return mac;
    }
    return std::nullopt;   // buf frees itself; nothing else to release
}

#else

namespace {

// The MAC stored in a link-layer ifaddrs entry, or nullopt if this entry
// isn't one (each interface also has separate IPv4/IPv6 entries).
std::optional<MAC_Address> linkAddress(const ifaddrs* i) {
    MAC_Address mac{};
#if defined(__APPLE__)
    // macOS: the link-layer entry is AF_LINK, address is a sockaddr_dl.
    if (i->ifa_addr->sa_family != AF_LINK) return std::nullopt;
    auto* sdl = reinterpret_cast<const sockaddr_dl*>(i->ifa_addr);
    if (sdl->sdl_alen != 6) return std::nullopt;
    std::memcpy(mac.data(), LLADDR(sdl), 6);
#else
    // Linux: the link-layer entry is AF_PACKET, address is a sockaddr_ll.
    if (i->ifa_addr->sa_family != AF_PACKET) return std::nullopt;
    auto* sll = reinterpret_cast<const sockaddr_ll*>(i->ifa_addr);
    if (sll->sll_halen != 6) return std::nullopt;
    std::memcpy(mac.data(), sll->sll_addr, 6);
#endif
    return mac;
}

// Containers, VMs, VPNs and Apple's peer-to-peer links, never the main NIC.
bool isVirtual(const std::string& name) {
    for (const char* prefix : {"docker", "br-", "veth", "virbr", "vmnet", "vboxnet",
                               "tun", "tap", "utun", "bridge", "awdl", "llw"})
        if (name.starts_with(prefix)) return true;
    return false;
}

// Name of the interface the machine is most likely using: up, running, not
// loopback or virtual, and holding an IPv4 address (so it's actually connected).
std::string defaultInterface(const ifaddrs* list) {
    for (const ifaddrs* i = list; i; i = i->ifa_next) {
        if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET) continue;
        if (i->ifa_flags & IFF_LOOPBACK) continue;
        if (!(i->ifa_flags & IFF_UP) || !(i->ifa_flags & IFF_RUNNING)) continue;
        if (isVirtual(i->ifa_name)) continue;
        return i->ifa_name;
    }
    return "";
}

}  // namespace

std::optional<MAC_Address> get_MAC(const std::string& iface) {
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0) return std::nullopt;

    const std::string name = iface.empty() ? defaultInterface(list) : iface;

    std::optional<MAC_Address> result;
    for (const ifaddrs* i = list; i && !result && !name.empty(); i = i->ifa_next) {
        if (i->ifa_addr && name == i->ifa_name) result = linkAddress(i);
    }

    freeifaddrs(list);   // getifaddrs allocates the list; it must be freed
    return result;
}

#endif
