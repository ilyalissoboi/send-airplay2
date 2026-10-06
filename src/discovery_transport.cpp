// SPDX-License-Identifier: Apache-2.0
#include "discovery_transport.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <iphlpapi.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <ifaddrs.h>
#include <net/if.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <unistd.h>
#ifdef __APPLE__
#include <net/if_dl.h>
#endif
#endif

namespace send_airplay2::detail {
namespace {
constexpr std::uint16_t mdns_port = 5353;
constexpr std::uint32_t mdns_ipv4_group = 0xe00000fbu; // 224.0.0.251 in host byte order.
constexpr std::size_t max_interfaces = 64;
#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket invalid_socket = INVALID_SOCKET;
struct NetworkRuntime {
    // Balance exactly one successful WSAStartup with WSACleanup, including when
    // later socket setup throws. Copying would make cleanup run twice.
    NetworkRuntime(const NetworkRuntime&) = delete;
    NetworkRuntime& operator=(const NetworkRuntime&) = delete;
    NetworkRuntime() {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data)) {
            throw std::runtime_error("WSAStartup failed");
        }
    }
    ~NetworkRuntime() noexcept {
        WSACleanup();
    }
};
int error_code() {
    return WSAGetLastError();
}

void close_socket(Socket s) {
    closesocket(s);
}
#else
using Socket = int;
constexpr Socket invalid_socket = -1;
struct NetworkRuntime {};
int error_code() {
    return errno;
}

void close_socket(Socket s) {
    close(s);
}
#endif
[[noreturn]] void fail(const char* action) {
    throw std::runtime_error(std::string(action) + " failed (socket error " +
                             std::to_string(error_code()) + ")");
}
struct SocketOwner {
    SocketOwner() = default;
    SocketOwner(const SocketOwner&) = delete;
    SocketOwner& operator=(const SocketOwner&) = delete;
    Socket value = invalid_socket;
    ~SocketOwner() noexcept {
        if (value != invalid_socket) {
            close_socket(value);
        }
    }
};
template <class T> void option(Socket s, int level, int key, const T& value) {
    if (setsockopt(s, level, key, reinterpret_cast<const char*>(&value), sizeof(value)) != 0) {
        fail("setsockopt");
    }
}

// Enumerate active IPv4 multicast interfaces. OS-owned linked lists stay alive
// until every interface has been copied into the returned value records.
std::vector<Interface> enumerate_interfaces() {
    std::vector<Interface> result;
#ifdef _WIN32
    ULONG length = 16384;
    std::vector<unsigned char> storage(length);
    ULONG status = ERROR_BUFFER_OVERFLOW;
    for (unsigned retry = 0; retry < 3 && status == ERROR_BUFFER_OVERFLOW; ++retry) {
        storage.resize(length);
        status = GetAdaptersAddresses(
            AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
            nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data()), &length);
    }
    if (status != NO_ERROR) {
        throw std::runtime_error("GetAdaptersAddresses failed: " + std::to_string(status));
    }
    for (auto* adapter = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data()); adapter;
         adapter = adapter->Next) {
        if (adapter->OperStatus != IfOperStatusUp || adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK ||
            (adapter->Flags & IP_ADAPTER_NO_MULTICAST) || !adapter->IfIndex) {
            continue;
        }
        for (auto* address_entry = adapter->FirstUnicastAddress; address_entry;
             address_entry = address_entry->Next) {
            if (address_entry->Address.lpSockaddr->sa_family != AF_INET ||
                address_entry->OnLinkPrefixLength > 32) {
                continue;
            }
            const auto ipv4_address =
                reinterpret_cast<const sockaddr_in*>(address_entry->Address.lpSockaddr)
                    ->sin_addr.s_addr;
            const auto prefix_length = address_entry->OnLinkPrefixLength;
            const auto netmask = prefix_length ? htonl(0xffffffffu << (32 - prefix_length)) : 0;
            if (ipv4_address) {
                result.push_back({adapter->IfIndex, ipv4_address, netmask});
            }
        }
    }
#else
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0) {
        fail("getifaddrs");
    }
    const auto owner = std::unique_ptr<ifaddrs, decltype(&freeifaddrs)>(list, freeifaddrs);
    for (auto* address_entry = list; address_entry; address_entry = address_entry->ifa_next) {
        if (!address_entry->ifa_addr || !address_entry->ifa_netmask ||
            address_entry->ifa_addr->sa_family != AF_INET || !(address_entry->ifa_flags & IFF_UP) ||
            !(address_entry->ifa_flags & IFF_MULTICAST) ||
            (address_entry->ifa_flags & IFF_LOOPBACK)) {
            continue;
        }
        const auto interface_index = if_nametoindex(address_entry->ifa_name);
        const auto ipv4_address =
            reinterpret_cast<const sockaddr_in*>(address_entry->ifa_addr)->sin_addr.s_addr;
        const auto netmask =
            reinterpret_cast<const sockaddr_in*>(address_entry->ifa_netmask)->sin_addr.s_addr;
        if (interface_index && ipv4_address) {
            result.push_back({interface_index, ipv4_address, netmask});
        }
    }
#endif
    std::sort(result.begin(), result.end(),
              [](const Interface& a, const Interface& b) { return a.index < b.index; });
    // One IPv4 address per interface; the receiving interface scopes DNS records.
    result.erase(
        std::unique(result.begin(), result.end(),
                    [](const Interface& a, const Interface& b) { return a.index == b.index; }),
        result.end());
    return result;
}

bool same_interface(const Interface& a, const Interface& b) {
    return a.index == b.index && a.ipv4 == b.ipv4 && a.netmask == b.netmask;
}

class NativeTransport final : public Transport {
    // Declaration order matters: members are destroyed in reverse order, so the
    // socket must close before the Winsock runtime reference is released.
    [[maybe_unused]] NetworkRuntime runtime_;
    SocketOwner socket_;
    std::vector<Interface> joined_;
#ifdef _WIN32
    LPFN_WSARECVMSG recv_message_ = nullptr;
#endif
    void membership(const Interface& nic, int command) {
        ip_mreq request{};
        request.imr_multiaddr.s_addr = htonl(mdns_ipv4_group);
        request.imr_interface.s_addr = nic.ipv4;
        option(socket_.value, IPPROTO_IP, command, request);
    }

public:
    NativeTransport() {
        socket_.value = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (socket_.value == invalid_socket) {
            fail("socket");
        }
#ifndef _WIN32
        if (socket_.value >= FD_SETSIZE) {
            throw std::runtime_error("Socket exceeds select descriptor limit");
        }
#endif
        const int enabled = 1;
        // Coexist with the system mDNS responder; all queries request multicast
        // answers because unicast replies might go only to the first listener.
        option(socket_.value, SOL_SOCKET, SO_REUSEADDR, enabled);
#if defined(SO_REUSEPORT) && !defined(_WIN32)
        option(socket_.value, SOL_SOCKET, SO_REUSEPORT, enabled);
#endif
        sockaddr_in local{};
        local.sin_family = AF_INET;
        local.sin_port = htons(mdns_port);
        if (bind(socket_.value, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) != 0) {
            fail("bind UDP 5353");
        }
#ifdef __APPLE__
        option(socket_.value, IPPROTO_IP, IP_RECVIF, enabled);
#else
        option(socket_.value, IPPROTO_IP, IP_PKTINFO, enabled);
#endif
        // Ancillary interface information is required: equal .local names on
        // distinct links must never share DNS records or address scope IDs.
#ifdef _WIN32
        const DWORD ttl = 255;
        option(socket_.value, IPPROTO_IP, IP_MULTICAST_TTL, ttl);
        GUID id = WSAID_WSARECVMSG;
        DWORD written = 0;
        if (WSAIoctl(socket_.value, SIO_GET_EXTENSION_FUNCTION_POINTER, &id, sizeof(id),
                     &recv_message_, sizeof(recv_message_), &written, nullptr, nullptr)) {
            fail("WSARecvMsg lookup");
        }
#else
        const unsigned char ttl = 255;
        option(socket_.value, IPPROTO_IP, IP_MULTICAST_TTL, ttl);
#endif
    }
    std::vector<Interface> refresh(std::vector<std::string>& warnings) override {
        auto active = enumerate_interfaces();
        if (active.size() > max_interfaces) {
            active.resize(max_interfaces);
            warnings.push_back("Interface limit reached (64)");
        }
        for (const auto& old : joined_) {
            if (std::none_of(active.begin(), active.end(),
                             [&](const Interface& i) { return same_interface(old, i); })) {
                try {
                    membership(old, IP_DROP_MEMBERSHIP);
                } catch (const std::runtime_error&) {
                    // The OS may already have removed membership with the interface.
                }
            }
        }
        std::vector<Interface> joined;
        for (const auto& i : active) {
            if (std::any_of(joined_.begin(), joined_.end(),
                            [&](const Interface& old) { return same_interface(old, i); })) {
                joined.push_back(i);
                continue;
            }
            try {
                membership(i, IP_ADD_MEMBERSHIP);
                joined.push_back(i);
            } catch (const std::runtime_error& e) {
                warnings.push_back("Interface " + std::to_string(i.index) + ": " + e.what());
            }
        }
        joined_ = std::move(joined);
        return joined_;
    }
    void send(const Bytes& bytes, const Interface& nic) override {
        in_addr local{};
        local.s_addr = nic.ipv4;
        option(socket_.value, IPPROTO_IP, IP_MULTICAST_IF, local);
        sockaddr_in destination{};
        destination.sin_family = AF_INET;
        destination.sin_port = htons(mdns_port);
        destination.sin_addr.s_addr = htonl(mdns_ipv4_group);
        if (sendto(socket_.value, reinterpret_cast<const char*>(bytes.data()),
                   static_cast<int>(bytes.size()), 0,
                   reinterpret_cast<const sockaddr*>(&destination), sizeof(destination)) < 0) {
            fail("sendto");
        }
    }
    bool receive(Datagram& packet, std::uint32_t wait_ms) override {
        fd_set ready;
        FD_ZERO(&ready);
        FD_SET(socket_.value, &ready);
        timeval timeout{};
        timeout.tv_sec = wait_ms / 1000;
        timeout.tv_usec = (wait_ms % 1000) * 1000;
#ifdef _WIN32
        const auto available = select(0, &ready, nullptr, nullptr, &timeout);
#else
        const auto available = select(socket_.value + 1, &ready, nullptr, nullptr, &timeout);
        if (available < 0 && errno == EINTR) {
            return false;
        }
#endif
        if (available < 0) {
            fail("select");
        }
        if (!available) {
            return false;
        }
        std::array<std::uint8_t, dns::max_packet_size> bytes{};
        // Ancillary headers require native alignment. Copy their payloads into
        // local structs instead of dereferencing possibly unaligned byte pointers.
        alignas(std::max_align_t) std::array<char, 256> control{};
        sockaddr_in source{};
        std::uint32_t interface_index = 0;
#ifdef _WIN32
        WSABUF buffer{static_cast<ULONG>(bytes.size()), reinterpret_cast<char*>(bytes.data())};
        WSAMSG message{};
        message.name = reinterpret_cast<sockaddr*>(&source);
        message.namelen = sizeof(source);
        message.lpBuffers = &buffer;
        message.dwBufferCount = 1;
        message.Control.buf = control.data();
        message.Control.len = static_cast<ULONG>(control.size());
        DWORD received_size = 0;
        if (recv_message_(socket_.value, &message, &received_size, nullptr, nullptr)) {
            if (WSAGetLastError() == WSAEMSGSIZE) {
                return false;
            }
            fail("WSARecvMsg");
        }
        if (message.dwFlags & (MSG_TRUNC | MSG_CTRUNC)) {
            return false;
        }
        for (auto* control_header = WSA_CMSG_FIRSTHDR(&message); control_header;
             control_header = WSA_CMSG_NXTHDR(&message, control_header)) {
            if (control_header->cmsg_level == IPPROTO_IP &&
                control_header->cmsg_type == IP_PKTINFO &&
                control_header->cmsg_len >= WSA_CMSG_LEN(sizeof(IN_PKTINFO))) {
                IN_PKTINFO packet_info{};
                std::memcpy(&packet_info, WSA_CMSG_DATA(control_header), sizeof(packet_info));
                interface_index = packet_info.ipi_ifindex;
            }
        }
#else
        iovec buffer{bytes.data(), bytes.size()};
        msghdr message{};
        message.msg_name = &source;
        message.msg_namelen = sizeof(source);
        message.msg_iov = &buffer;
        message.msg_iovlen = 1;
        message.msg_control = control.data();
        message.msg_controllen = control.size();
        const auto received_size = recvmsg(socket_.value, &message, 0);
        if (received_size < 0) {
            if (errno == EINTR) {
                return false;
            }
            fail("recvmsg");
        }
        if (message.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) {
            return false;
        }
        for (auto* control_header = CMSG_FIRSTHDR(&message); control_header;
             control_header = CMSG_NXTHDR(&message, control_header)) {
#ifdef __APPLE__
            if (control_header->cmsg_level == IPPROTO_IP &&
                control_header->cmsg_type == IP_RECVIF &&
                control_header->cmsg_len >= CMSG_LEN(sizeof(sockaddr_dl))) {
                sockaddr_dl packet_info{};
                std::memcpy(&packet_info, CMSG_DATA(control_header), sizeof(packet_info));
                interface_index = packet_info.sdl_index;
            }
#else
            if (control_header->cmsg_level == IPPROTO_IP &&
                control_header->cmsg_type == IP_PKTINFO &&
                control_header->cmsg_len >= CMSG_LEN(sizeof(in_pktinfo))) {
                in_pktinfo packet_info{};
                std::memcpy(&packet_info, CMSG_DATA(control_header), sizeof(packet_info));
                interface_index = static_cast<std::uint32_t>(packet_info.ipi_ifindex);
            }
#endif
        }
#endif
        // Admit only local-link mDNS responders on joined interfaces. This is
        // routing/filtering policy, not authentication of advertised metadata.
        const auto receiving_interface =
            std::find_if(joined_.begin(), joined_.end(), [interface_index](const Interface& i) {
                return i.index == interface_index;
            });
        if (receiving_interface == joined_.end() || source.sin_port != htons(mdns_port) ||
            (source.sin_addr.s_addr & receiving_interface->netmask) !=
                (receiving_interface->ipv4 & receiving_interface->netmask)) {
            return false;
        }
        if (received_size >= 3 && !(bytes[2] & 0x80)) {
            return false; // Queries/probes are not observations.
        }
        packet.interface_index = interface_index;
        packet.bytes.assign(bytes.begin(), bytes.begin() + received_size);
        return true;
    }
};
} // namespace
std::unique_ptr<Transport> native_transport() {
    return std::make_unique<NativeTransport>();
}
} // namespace send_airplay2::detail
