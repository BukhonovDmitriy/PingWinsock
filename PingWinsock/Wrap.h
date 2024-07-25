#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>
#include <exception>
#include <sstream>
#include <vector>

namespace WsaWrap {
    class WsaData {
        ::WSAData data;

    public:
        WsaData() {
            if (WSAStartup(MAKEWORD(2, 2), &data)) {
                int err_code = WSAGetLastError();
                std::stringstream msg;
                msg << "WSAStartup failed with error" << err_code;
                throw std::runtime_error(msg.str().c_str());
            }
        }

        const ::WSAData &get() const noexcept {
            return data;
        }

        ~WsaData() {
            WSACleanup();
        }
    };

    class AddrInfo {
        addrinfo *addr_info;

    public:
        //resolving name
        AddrInfo(const char *addr, const char *port, int af, int type, int proto) {
            addrinfo hints;
            addr_info = NULL;
            int rc;

            memset(&hints, 0, sizeof(hints));
            hints.ai_flags = ((addr) ? 0 : AI_PASSIVE);
            hints.ai_family = af;
            hints.ai_socktype = type;
            hints.ai_protocol = proto;

            rc = getaddrinfo(addr, port, &hints, &addr_info);

            if (rc != 0) {
                int err_code = WSAGetLastError();
                std::stringstream msg;
                if (addr) msg << "Bad name: " << addr;
                else msg << "Unable to obtain local address '";
                msg << "'. error code: " << err_code;
                throw std::runtime_error(msg.str().c_str());
            }
        }

        addrinfo &info() noexcept {
            return *addr_info;
        }

        const addrinfo &info() const noexcept {
            return *addr_info;
        }

        ~AddrInfo() {
            if (addr_info)
                freeaddrinfo(addr_info);
        }
    };

    class Socket {
        const SOCKET sock;

    public:
        class TimeoutException : public std::runtime_error {
        public:
            TimeoutException(const char *msg = "Timeout exception!")
                : std::runtime_error(msg) {}
        };

        Socket(
            int af,
            int type,
            int proto,
            LPWSAPROTOCOL_INFOW lpProtocolInfo,
            GROUP g,
            DWORD dwFlags
        ) : sock(WSASocket(af, type, proto, lpProtocolInfo, g, dwFlags)) {
            if (sock == INVALID_SOCKET) {
                int err_code = WSAGetLastError();
                std::stringstream msg;
                msg << "WSASocket (Socket constructor) error: " << err_code;
                throw std::runtime_error(msg.str().c_str());
            }
        }

        int sendto(const std::vector<char> &buf, int flags, const AddrInfo &dest) {
            int bsent = ::sendto(
                sock,
                &buf[0],
                buf.size(),
                flags,
                dest.info().ai_addr,
                dest.info().ai_addrlen
            );

            if (bsent == SOCKET_ERROR) {
                int err_code = WSAGetLastError();

                if (err_code == WSAETIMEDOUT)
                    throw TimeoutException();

                std::stringstream msg;
                msg << "sendto (in Socket.sendto) failed with error" << err_code;
                throw std::runtime_error(msg.str().c_str());
            }

            return bsent;
        }

        int recvfrom(std::vector<char> &buf, int flags, AddrInfo &local) {
            int bread = ::recvfrom(
                sock,
                &buf[0],
                buf.size(),
                flags,
                local.info().ai_addr,
                reinterpret_cast<int *>(&local.info().ai_addrlen)
            );

            if (bread == SOCKET_ERROR) {
                int err_code = WSAGetLastError();

                if (err_code == WSAETIMEDOUT)
                    throw TimeoutException();

                std::stringstream msg;
                msg << "sendto (in Socket.sendto) failed with error" << err_code;
                throw std::runtime_error(msg.str().c_str());
            }

            return bread;
        }

        void setopt(int level, int optname, const char *optval, int optlen) {
            if (setsockopt(sock, level, optname, optval, optlen)) {
                int err_code = WSAGetLastError();
                std::stringstream msg;
                msg << "setsockopt (in Socket.setopt) failed with error" << err_code;
                throw std::runtime_error(msg.str().c_str());
            }
        }

        ~Socket() {
            if (sock != INVALID_SOCKET)
                closesocket(sock);
        }
    };
};
