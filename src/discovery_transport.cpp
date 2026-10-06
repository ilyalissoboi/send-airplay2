// SPDX-License-Identifier: Apache-2.0
#include "discovery_transport.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include <string>
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
#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket invalid_socket = INVALID_SOCKET;
struct NetworkRuntime {
    NetworkRuntime() { WSADATA data{}; if (WSAStartup(MAKEWORD(2, 2), &data)) throw std::runtime_error("WSAStartup failed"); }
    ~NetworkRuntime() { WSACleanup(); }
};
int error_code() { return WSAGetLastError(); }
void close_socket(Socket s) { closesocket(s); }
#else
using Socket = int;
constexpr Socket invalid_socket = -1;
struct NetworkRuntime {};
int error_code() { return errno; }
void close_socket(Socket s) { close(s); }
#endif
[[noreturn]] void fail(const char* action) {
    throw std::runtime_error(std::string(action) + " failed (socket error " + std::to_string(error_code()) + ")");
}
struct SocketOwner {
    Socket value = invalid_socket;
    ~SocketOwner() { if (value != invalid_socket) close_socket(value); }
};
template<class T> void option(Socket s, int level, int key, const T& value) {
    if (setsockopt(s, level, key, reinterpret_cast<const char*>(&value), sizeof(value)) != 0) fail("setsockopt");
}
std::vector<Interface> interfaces() {
    std::vector<Interface> result;
#ifdef _WIN32
    ULONG length = 16384;
    std::vector<unsigned char> storage(length);
    ULONG status = ERROR_BUFFER_OVERFLOW;
    for (unsigned retry = 0; retry < 3 && status == ERROR_BUFFER_OVERFLOW; ++retry) {
        storage.resize(length);
        status = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST
            | GAA_FLAG_SKIP_DNS_SERVER, nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data()), &length);
    }
    if (status != NO_ERROR) throw std::runtime_error("GetAdaptersAddresses failed: " + std::to_string(status));
    for (auto* adapter = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data()); adapter; adapter = adapter->Next) {
        if (adapter->OperStatus != IfOperStatusUp || adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK
            || (adapter->Flags & IP_ADAPTER_NO_MULTICAST) || !adapter->IfIndex) continue;
        for (auto* a = adapter->FirstUnicastAddress; a; a = a->Next) {
            if (a->Address.lpSockaddr->sa_family != AF_INET || a->OnLinkPrefixLength > 32) continue;
            const auto addr = reinterpret_cast<const sockaddr_in*>(a->Address.lpSockaddr)->sin_addr.s_addr;
            const auto bits = a->OnLinkPrefixLength;
            const auto mask = bits ? htonl(0xffffffffu << (32 - bits)) : 0;
            if (addr) result.push_back({adapter->IfIndex, addr, mask});
        }
    }
#else
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0) fail("getifaddrs");
    const auto owner = std::unique_ptr<ifaddrs, decltype(&freeifaddrs)>(list, freeifaddrs);
    for (auto* a = list; a; a = a->ifa_next) {
        if (!a->ifa_addr || !a->ifa_netmask || a->ifa_addr->sa_family != AF_INET
            || !(a->ifa_flags & IFF_UP) || !(a->ifa_flags & IFF_MULTICAST) || (a->ifa_flags & IFF_LOOPBACK)) continue;
        const auto index = if_nametoindex(a->ifa_name);
        const auto addr = reinterpret_cast<const sockaddr_in*>(a->ifa_addr)->sin_addr.s_addr;
        const auto mask = reinterpret_cast<const sockaddr_in*>(a->ifa_netmask)->sin_addr.s_addr;
        if (index && addr) result.push_back({index, addr, mask});
    }
#endif
    std::sort(result.begin(), result.end(), [](const Interface& a, const Interface& b) { return a.index < b.index; });
    // One IPv4 address per interface; the receiving interface scopes DNS records.
    result.erase(std::unique(result.begin(), result.end(), [](const Interface& a, const Interface& b) {
        return a.index == b.index;
    }), result.end());
    return result;
}
bool same_interface(const Interface& a, const Interface& b) { return a.index == b.index && a.ipv4 == b.ipv4 && a.netmask == b.netmask; }
class NativeTransport final : public Transport {
    [[maybe_unused]] NetworkRuntime runtime_;
    SocketOwner socket_;
    std::vector<Interface> joined_;
#ifdef _WIN32
    LPFN_WSARECVMSG recv_message_ = nullptr;
#endif
    void membership(const Interface& nic, int command) {
        ip_mreq request{};
        request.imr_multiaddr.s_addr = htonl(0xe00000fbu); // 224.0.0.251
        request.imr_interface.s_addr = nic.ipv4;
        option(socket_.value, IPPROTO_IP, command, request);
    }
public:
    NativeTransport() {
        socket_.value = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (socket_.value == invalid_socket) fail("socket");
#ifndef _WIN32
        if (socket_.value >= FD_SETSIZE) throw std::runtime_error("Socket exceeds select descriptor limit");
#endif
        const int enabled = 1;
        option(socket_.value, SOL_SOCKET, SO_REUSEADDR, enabled);
#if defined(SO_REUSEPORT) && !defined(_WIN32)
        option(socket_.value, SOL_SOCKET, SO_REUSEPORT, enabled);
#endif
        sockaddr_in local{}; local.sin_family = AF_INET; local.sin_port = htons(5353);
        if (bind(socket_.value, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) != 0) fail("bind UDP 5353");
#ifdef __APPLE__
        option(socket_.value, IPPROTO_IP, IP_RECVIF, enabled);
#else
        option(socket_.value, IPPROTO_IP, IP_PKTINFO, enabled);
#endif
#ifdef _WIN32
        const DWORD ttl = 255;
        option(socket_.value, IPPROTO_IP, IP_MULTICAST_TTL, ttl);
        GUID id = WSAID_WSARECVMSG; DWORD written = 0;
        if (WSAIoctl(socket_.value, SIO_GET_EXTENSION_FUNCTION_POINTER, &id, sizeof(id),
            &recv_message_, sizeof(recv_message_), &written, nullptr, nullptr)) fail("WSARecvMsg lookup");
#else
        const unsigned char ttl = 255;
        option(socket_.value, IPPROTO_IP, IP_MULTICAST_TTL, ttl);
#endif
    }
    std::vector<Interface> refresh(std::vector<std::string>& warnings) override {
        auto active = interfaces();
        if (active.size() > 64) { active.resize(64); warnings.push_back("Interface limit reached (64)"); }
        for (const auto& old : joined_) if (std::none_of(active.begin(), active.end(), [&](const Interface& i) {
            return same_interface(old, i);
        })) {
            try { membership(old, IP_DROP_MEMBERSHIP); } catch (const std::runtime_error&) { /* Removed interface. */ }
        }
        std::vector<Interface> joined;
        for (const auto& i : active) {
            if (std::any_of(joined_.begin(), joined_.end(), [&](const Interface& old) { return same_interface(old, i); })) {
                joined.push_back(i); continue;
            }
            try { membership(i, IP_ADD_MEMBERSHIP); joined.push_back(i); }
            catch (const std::runtime_error& e) { warnings.push_back("Interface " + std::to_string(i.index) + ": " + e.what()); }
        }
        joined_ = std::move(joined);
        return joined_;
    }
    void send(const Bytes& bytes, const Interface& nic) override {
        in_addr local{}; local.s_addr = nic.ipv4;
        option(socket_.value, IPPROTO_IP, IP_MULTICAST_IF, local);
        sockaddr_in destination{}; destination.sin_family = AF_INET; destination.sin_port = htons(5353);
        destination.sin_addr.s_addr = htonl(0xe00000fbu);
        if (sendto(socket_.value, reinterpret_cast<const char*>(bytes.data()), static_cast<int>(bytes.size()), 0,
            reinterpret_cast<const sockaddr*>(&destination), sizeof(destination)) < 0) fail("sendto");
    }
    bool receive(Datagram& packet, std::uint32_t wait_ms) override {
        fd_set ready; FD_ZERO(&ready); FD_SET(socket_.value, &ready);
        timeval timeout{}; timeout.tv_sec = wait_ms / 1000; timeout.tv_usec = (wait_ms % 1000) * 1000;
#ifdef _WIN32
        const auto available = select(0, &ready, nullptr, nullptr, &timeout);
#else
        const auto available = select(socket_.value + 1, &ready, nullptr, nullptr, &timeout);
        if (available < 0 && errno == EINTR) return false;
#endif
        if (available < 0) fail("select");
        if (!available) return false;
        std::array<std::uint8_t, 9000> bytes{};
        alignas(std::max_align_t) std::array<char, 256> control{};
        sockaddr_in source{};
        std::uint32_t index = 0;
#ifdef _WIN32
        WSABUF buffer{static_cast<ULONG>(bytes.size()), reinterpret_cast<char*>(bytes.data())};
        WSAMSG message{}; message.name = reinterpret_cast<sockaddr*>(&source); message.namelen = sizeof(source);
        message.lpBuffers = &buffer; message.dwBufferCount = 1;
        message.Control.buf = control.data(); message.Control.len = static_cast<ULONG>(control.size());
        DWORD length = 0;
        if (recv_message_(socket_.value, &message, &length, nullptr, nullptr)) {
            if (WSAGetLastError() == WSAEMSGSIZE) return false;
            fail("WSARecvMsg");
        }
        if (message.dwFlags & (MSG_TRUNC | MSG_CTRUNC)) return false;
        for (auto* c = WSA_CMSG_FIRSTHDR(&message); c; c = WSA_CMSG_NXTHDR(&message, c)) {
            if (c->cmsg_level == IPPROTO_IP && c->cmsg_type == IP_PKTINFO && c->cmsg_len >= WSA_CMSG_LEN(sizeof(IN_PKTINFO))) {
                IN_PKTINFO info{}; std::memcpy(&info, WSA_CMSG_DATA(c), sizeof(info)); index = info.ipi_ifindex;
            }
        }
#else
        iovec buffer{bytes.data(), bytes.size()};
        msghdr message{}; message.msg_name = &source; message.msg_namelen = sizeof(source);
        message.msg_iov = &buffer; message.msg_iovlen = 1;
        message.msg_control = control.data(); message.msg_controllen = control.size();
        const auto length = recvmsg(socket_.value, &message, 0);
        if (length < 0) { if (errno == EINTR) return false; fail("recvmsg"); }
        if (message.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) return false;
        for (auto* c = CMSG_FIRSTHDR(&message); c; c = CMSG_NXTHDR(&message, c)) {
#ifdef __APPLE__
            if (c->cmsg_level == IPPROTO_IP && c->cmsg_type == IP_RECVIF && c->cmsg_len >= CMSG_LEN(sizeof(sockaddr_dl))) {
                sockaddr_dl info{}; std::memcpy(&info, CMSG_DATA(c), sizeof(info)); index = info.sdl_index;
            }
#else
            if (c->cmsg_level == IPPROTO_IP && c->cmsg_type == IP_PKTINFO && c->cmsg_len >= CMSG_LEN(sizeof(in_pktinfo))) {
                in_pktinfo info{}; std::memcpy(&info, CMSG_DATA(c), sizeof(info)); index = static_cast<std::uint32_t>(info.ipi_ifindex);
            }
#endif
        }
#endif
        const auto nic = std::find_if(joined_.begin(), joined_.end(), [index](const Interface& i) { return i.index == index; });
        if (nic == joined_.end() || source.sin_port != htons(5353)
            || (source.sin_addr.s_addr & nic->netmask) != (nic->ipv4 & nic->netmask)) return false;
        if (length >= 3 && !(bytes[2] & 0x80)) return false; // Queries/probes are not observations.
        packet.interface_index = index;
        packet.bytes.assign(bytes.begin(), bytes.begin() + length);
        return true;
    }
};
}
std::unique_ptr<Transport> native_transport() { return std::make_unique<NativeTransport>(); }
}
