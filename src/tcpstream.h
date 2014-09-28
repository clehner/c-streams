#ifndef _TCPSTREAM_H
#define _TCPSTREAM_H

typedef UInt32 ip_addr;
typedef unsigned short tcp_port;

#define IP_ADDR(a,b,c,d) ((a << 24) | (b << 16) | (c << 8) | d)

const char *sprint_ip_addr(ip_addr ip);

void ProvideTCPActiveStream(Stream *s, ip_addr ip, tcp_port port);
void ProvideTCPPassiveStream(Stream *s, tcp_port port);

#endif
