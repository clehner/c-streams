#ifndef _TCPSTREAM_H
#define _TCPSTREAM_H

typedef UInt32 ip_addr;
typedef unsigned short tcp_port;

enum {
	tcpMissingDriverErr = -1,
	tcpSetupErr = -2,
	tcpOutOfMemoryErr = -3,
	tcpMissingStreamErr = -4,
	tcpStreamLimitErr = -5,
	tcpCreateStreamErr = -6,
	tcpConnectErr = -7,
	tcpTerminatedErr = -8,
	tcpInternalErr = -9,
};

#define IP_ADDR(a,b,c,d) ((a << 24) | (b << 16) | (c << 8) | d)

const char *sprint_ip_addr(ip_addr ip);

void ProvideTCPActiveStream(Stream *s, const char *host, tcp_port port);
void ProvideTCPPassiveStream(Stream *s, tcp_port port);

#endif
