#pragma once

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
