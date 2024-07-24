#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <winsock2.h>
#include <ws2tcpip.h>
#include <chrono>
#include <iostream>
#include <vector>

#pragma comment(lib, "Ws2_32.lib")
#pragma pack(1)

#define ICMP_ECHO 8
#define ICMP_ECHO_RESPONSE 0

const size_t DEFAULT_DATA_SIZE = 32;

const int DEFAULT_ITERATIONS = 15;
const DWORD DEFAULT_TIMEOUT = 1500; //ms
const DWORD DEFAULT_SLEEPTIME = 1000;

struct IpHeader {
	unsigned char  verlen;
	unsigned char  tos;
	unsigned short totallength;
	unsigned short id;
	unsigned short offset;
	unsigned char  ttl;
	unsigned char  protocol;
	unsigned short checksum;
	unsigned int   srcaddr;
	unsigned int   destaddr;
};

struct IcmpHeader {
	unsigned char  type;
	unsigned char  code;
	unsigned short checksum;
	unsigned short id;
	unsigned short seq;
};

enum class ResponseState {
	good,
	wrong_id,
	invalid_checksum,
	is_not_echo_response,
	timed_out
};

void usage(const char *progname);
int validate_args(int argc, char **argv);
addrinfo *resolve_address(const char *addr, const char *port, int af, int type, int proto);

unsigned short eval_checksum(const unsigned short *buffer, int size);
void config_icmp_hdr(char *icmp_data, int datasize);

inline int sendto(SOCKET s, const std::vector<char> &buf, int flags, const sockaddr *to, int tolen);
inline int recvfrom(SOCKET s, std::vector<char> &buf, int flags, sockaddr *from, int *fromlen);

ResponseState validate_response(const std::vector<char> &response);

int main(int argc, char **argv) {
	WSADATA wsaData;
	SOCKET sock_raw = INVALID_SOCKET;

	addrinfo *dest = nullptr, *local = nullptr;

	size_t datasize = DEFAULT_DATA_SIZE;
	DWORD timeout = DEFAULT_TIMEOUT;
	DWORD sleeptime = DEFAULT_SLEEPTIME;
	int iter_count = DEFAULT_ITERATIONS;

	int status = 0;

	if (WSAStartup(MAKEWORD(2, 2), &wsaData)) {
		status = WSAGetLastError();
		std::cout << "error with WSAStartup: " << status << std::endl;
		return status;
	}

	if (validate_args(argc, argv)) {
		usage(argv[0]);
		status = -2;
		goto CLEANUP;
	}

	dest = resolve_address(argv[1], "0", AF_INET, SOCK_RAW, IPPROTO_ICMP);

	if (dest == nullptr) {
		std::cout << "bad name: " << argv[1] << std::endl;
		status = -1;
		goto CLEANUP;
	}

	std::cout << inet_ntoa(((sockaddr_in *)(dest->ai_addr))->sin_addr) << std::endl;

	local = resolve_address(NULL, "0", AF_INET, SOCK_RAW, IPPROTO_ICMP);

	if (local == nullptr) {
		std::cout << "Unable to obtain the bind address!" << std::endl;
		status = -1;
		goto CLEANUP;
	}

	std::cout << inet_ntoa(((sockaddr_in *)(local->ai_addr))->sin_addr) << std::endl;

	sock_raw = WSASocketW(AF_INET, SOCK_RAW, IPPROTO_ICMP, nullptr, 0, WSA_FLAG_OVERLAPPED);

	if (sock_raw == INVALID_SOCKET) {
		status = WSAGetLastError();
		std::cout << "error with WSASocket: " << status << std::endl;
		goto CLEANUP;
	}

	if (setsockopt(sock_raw, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout))) {
		status = WSAGetLastError();
		std::cout << "error with setsockopt: " << status << std::endl;
		goto CLEANUP;
	}


	for (int i = 0; i != iter_count; ++i) {
		int bytes_wrote, bytes_read;

		ResponseState st;

		std::vector<char> buf_send(sizeof(IcmpHeader) + datasize, 'e');
		std::vector<char> buf_recv(sizeof(IpHeader) + sizeof(IcmpHeader) + datasize);

		config_icmp_hdr(&buf_send[0], datasize);

		bytes_wrote = sendto(sock_raw, buf_send, 0, dest->ai_addr, dest->ai_addrlen);

		if (bytes_wrote == SOCKET_ERROR) {
			int err_code = WSAGetLastError();
			if (err_code == WSAETIMEDOUT) {
				std::cout << "Timed out" << std::endl;
				continue;
			}
			status = err_code;
			std::cout << "error with sendto: " << status << std::endl;
			goto CLEANUP;
		}

		auto start_recv{ std::chrono::steady_clock::now() };

		for (;;) {
			bytes_read = recvfrom(sock_raw, buf_recv, 0, local->ai_addr, (int *)&local->ai_addrlen);

			if (bytes_read == SOCKET_ERROR) {
				int err_code = WSAGetLastError();
				if (err_code == WSAETIMEDOUT) {
					st = ResponseState::timed_out;
					break;
				}
				status = err_code;
				std::cout << "error with recvfrom: " << status << std::endl;
				goto CLEANUP;
			}

			st = validate_response(buf_recv);

			if (st != ResponseState::wrong_id)
				break;

			std::cout << '.';
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

		if (i + 1 != iter_count)
			Sleep(sleeptime);
	}

CLEANUP:
	if (dest)
		freeaddrinfo(dest);

	if (local)
		freeaddrinfo(local);

	if (sock_raw != INVALID_SOCKET)
		closesocket(sock_raw);

	WSACleanup();

	return status;
}

int validate_args(int argc, char **argv) {
	if (argc == 2)
		return 0; //Good
	else
		return 1; //An error occured
}

void usage(const char *progname) {
	std::cout << "usage: " << progname << "<IP>" << std::endl;
}

addrinfo *resolve_address(const char *addr, const char *port, int af, int type, int proto) {
	struct addrinfo hints, *res = NULL;
	int rc;

	memset(&hints, 0, sizeof(hints));
	hints.ai_flags = ((addr) ? 0 : AI_PASSIVE);
	hints.ai_family = af;
	hints.ai_socktype = type;
	hints.ai_protocol = proto;

	rc = getaddrinfo(addr, port, &hints, &res);

	if (rc != 0)
		return nullptr;

	return res;
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

inline int sendto(SOCKET s, const std::vector<char> &buf, int flags, const sockaddr *to, int tolen) {
	return sendto(s, &buf[0], buf.size(), flags, to, tolen);
}

inline int recvfrom(SOCKET s, std::vector<char> &buf, int flags, sockaddr *from, int *fromlen) {
	return recvfrom(s, &buf[0], buf.size(), flags, from, fromlen);
}

ResponseState validate_response(const std::vector<char> &response) {
	const IpHeader *ip_hdr = reinterpret_cast<const IpHeader *>(&response[0]);
	const IcmpHeader *icmp_hdr = reinterpret_cast<const IcmpHeader *>(&response[sizeof(IpHeader)]);

	if (eval_checksum(reinterpret_cast<const USHORT *>(icmp_hdr), response.size() - sizeof(IpHeader)))
		return ResponseState::invalid_checksum;

	if (icmp_hdr->id != (USHORT)GetCurrentProcessId())
		return ResponseState::wrong_id;

	if (icmp_hdr->type != ICMP_ECHO_RESPONSE)
		return ResponseState::is_not_echo_response;

	return ResponseState::good;
}
