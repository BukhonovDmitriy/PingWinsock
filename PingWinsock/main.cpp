#define _WINSOCK_DEPRECATED_NO_WARNINGS
//#define DEBUG

#include <winsock2.h>
#include <ws2tcpip.h>
#include <chrono>
#include <iostream>
#include <exception>
#include <vector>
#include <cstdlib>
#include <cctype>

#include "Headers.h"
#include "Wrap.h"

#pragma comment(lib, "Ws2_32.lib")
#pragma pack(1)

#define ICMP_ECHO 8
#define ICMP_ECHO_RESPONSE 0

const size_t DEFAULT_DATA_SIZE = 32;
const size_t DEFAULT_ITERATIONS = 15;
const DWORD DEFAULT_TIMEOUT = 1500; //ms
const DWORD DEFAULT_SLEEPTIME = 1000;

struct PingParams {
    const char *progname;
    const char *destname;
    size_t datasize = DEFAULT_DATA_SIZE;
    DWORD timeout = DEFAULT_TIMEOUT;
    DWORD sleeptime = DEFAULT_SLEEPTIME;
    size_t iter_count = DEFAULT_ITERATIONS;
    bool help = false;

    PingParams(int argc, char **argv) {
        if (argc < 2)
            throw std::invalid_argument("Invalid command line args!");

        progname = argv[0];
        destname = argv[1];

        for (int i = 2; i < argc; ++i) {
            if (strcmp(argv[i], "-i") == 0) {
                if (++i == argc)
                    throw std::invalid_argument("Invalid command line args!");
                iter_count = str_to_sizet(argv[i]);
            }
            else if (strcmp(argv[i], "-t") == 0) {
                if (++i == argc)
                    throw std::invalid_argument("Invalid command line args!");
                timeout = str_to_sizet(argv[i]);
            }
            else if (strcmp(argv[i], "-s") == 0) {
                if (++i == argc)
                    throw std::invalid_argument("Invalid command line args!");
                sleeptime = str_to_sizet(argv[i]);
            }
            else if (strcmp(argv[i], "-d") == 0) {
                if (++i == argc)
                    throw std::invalid_argument("Invalid command line args!");
                datasize = str_to_sizet(argv[i]);
            }
            else if (strcmp(argv[i], "-h") == 0) {
                help = true;
            }
            else
                throw std::invalid_argument("Invalid command line args!");
        }
    }

private:
    size_t str_to_sizet(const char *str) {
        size_t res = 0;

        for (const char *c = str; *c; ++c) {
            if (!isdigit(*c))
                throw std::invalid_argument("Invalid command line args!");
            else
                res = res * 10 + (*c - '0');
        }

        return res;
    }
};

enum class ResponseState {
    good,
    wrong_id,
    invalid_checksum,
    is_not_echo_response,
    timed_out
};

void ping(const PingParams &params);

void usage(const char *progname);

unsigned short eval_checksum(const unsigned short *buffer, int size);
void config_icmp_hdr(char *icmp_data, int datasize); //also fills in checksum so fill your data before

ResponseState validate_response(const std::vector<char> &response);

int main(int argc, char **argv) {
#ifdef DEBUG
    ping(PingParams(argc, argv));
#else
    try {
        ping(PingParams(argc, argv));
    }
    catch (const std::runtime_error &e) {
        std::cout << e.what() << std::endl;
        std::cerr << e.what() << std::endl;
        throw;
    }
    catch (const std::invalid_argument&) {
        usage(argv[0]);
        throw;
    }
#endif
}

void ping(const PingParams &params) {
    if (params.help) {
        usage(params.progname);
        return;
    }

    std::cout << "Starting ping with dest name \'" << params.destname << "\'" << std::endl;

    WsaWrap::WsaData wsadata;

    WsaWrap::AddrInfo dest(params.destname, "0", AF_INET, SOCK_RAW, IPPROTO_ICMP);
    WsaWrap::AddrInfo local(nullptr, "0", AF_INET, SOCK_RAW, IPPROTO_ICMP);

    std::cout << "Dest IP: " << inet_ntoa(((sockaddr_in *)dest.info().ai_addr)->sin_addr) << std::endl;

    WsaWrap::Socket sock_raw(AF_INET, SOCK_RAW, IPPROTO_ICMP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    sock_raw.setopt(SOL_SOCKET, SO_RCVTIMEO, (char *)&params.timeout, sizeof(params.timeout));

    for (size_t i = 0; i != params.iter_count; ++i) {
        int bytes_wrote, bytes_read;

        ResponseState st;

        std::vector<char> buf_send(sizeof(IcmpHeader) + params.datasize, 'e');
        std::vector<char> buf_recv(sizeof(IpHeader) + sizeof(IcmpHeader) + params.datasize);

        config_icmp_hdr(&buf_send[0], params.datasize);

        try {
            bytes_wrote = sock_raw.sendto(buf_send, 0, dest);
        }
        catch (WsaWrap::Socket::TimeoutException&) {
            std::cout << "Timed out" << std::endl;
            continue;
        }

        auto start_recv{ std::chrono::steady_clock::now() };

        for (;;) {
            try {
                bytes_read = sock_raw.recvfrom(buf_recv, 0, local);
            }
            catch (WsaWrap::Socket::TimeoutException&) {
                st = ResponseState::timed_out;
                break;
            }

            st = validate_response(buf_recv);

            if (st != ResponseState::wrong_id)
                break;
        }

        auto end_recv{ std::chrono::steady_clock::now() };
        std::chrono::duration<double> elapsed_seconds = end_recv - start_recv;

        switch (st) {
        case ResponseState::good:
            std::cout << "Got response in " << elapsed_seconds.count() * 1000 << " ms" << std::endl;
            break;

        case ResponseState::invalid_checksum:
            std::cout << "Invalid checksum!" << std::endl;
            break;

        case ResponseState::is_not_echo_response:
        {
            IcmpHeader *hdr = reinterpret_cast<IcmpHeader *>(&buf_recv[sizeof(IpHeader)]);
            std::cout << "Got packet is not echo response: type=" << hdr->type << ", code=" << hdr->code << std::endl;
            break;
        }
        case ResponseState::timed_out:
            std::cout << "Timed out!" << std::endl;
            break;
        }

        if (i + 1 != params.iter_count)
            Sleep(params.sleeptime);
    }
}

void usage(const char *progname) {
    std::cout << "usage: " << progname << " <IP or hostname> [options]" << std::endl
              << "option [-i <number>] sets iterations count" << std::endl
              << "option [-t <number>] sets timeout" << std::endl
              << "option [-d <number>] sets size of sent data" << std::endl
              << "option [-s <number>] sets time between requests" << std::endl
              << "option [-h] prints this message" << std::endl;
}

unsigned short eval_checksum(const unsigned short *buffer, int size) {
    unsigned long cksum = 0;

    while (size > 1) {
        cksum += *buffer++;
        size -= sizeof(unsigned short);
    }

    if (size)
        cksum += *(UCHAR *)buffer;

    cksum = (cksum >> 16) + (cksum & 0xffff);
    cksum += (cksum >> 16);

    return (unsigned short)(~cksum);
}

void config_icmp_hdr(char *icmp_data, int datasize) {
    IcmpHeader *hdr = (IcmpHeader *)icmp_data;

    hdr->type = ICMP_ECHO;
    hdr->code = 0;
    hdr->id = (unsigned short)GetCurrentProcessId();
    hdr->checksum = 0;
    hdr->seq = 0;

    hdr->checksum = eval_checksum((USHORT *)icmp_data, sizeof(IcmpHeader) + datasize);
}

ResponseState validate_response(const std::vector<char> &response) {
//    const IpHeader *ip_hdr = (const IpHeader *)&response[0];
    const IcmpHeader *icmp_hdr = (const IcmpHeader *)&response[sizeof(IpHeader)];

    if (eval_checksum((const USHORT *)icmp_hdr, response.size() - sizeof(IpHeader)))
        return ResponseState::invalid_checksum;

    if (icmp_hdr->id != (USHORT)GetCurrentProcessId())
        return ResponseState::wrong_id;

    if (icmp_hdr->type != ICMP_ECHO_RESPONSE)
        return ResponseState::is_not_echo_response;

    return ResponseState::good;
}
