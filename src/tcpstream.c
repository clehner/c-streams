#include <stdlib.h>
#include <stdio.h>
#include <Files.h>
#include <Devices.h>
#include <Processes.h>
#include <MacTCP.h>
#include "stream.h"
#include "tcpstream.h"

typedef struct MyTCPiopb MyTCPiopb;

typedef struct {
	Stream *stream;
	StreamPtr tcpStream;
	char recvBuf[8192];
	ip_addr remoteHost;
	tcp_port remotePort;
	MyTCPiopb *completedPBsHead;
	MyTCPiopb *completedPBsTail;
} TCPData;

struct MyTCPiopb {
	TCPiopb pb;
	TCPData *tcpData;
	struct MyTCPiopb *nextCompleted;
};

TCPData *NewTCPData(Stream *s);
void TCPStreamOpen(Stream *s, void *providerData);
void TCPStreamClose(Stream *s, void *providerData);
void TCPStreamWrite(Stream *s, void *providerData, char *data, short len);
void TCPStreamPoll(Stream *s, void *providerData);

//void TCPStreamRead(TCPData *fileData, TCPiopb *pb);
void TCPIOComplete(TCPiopb *thePB);
void TCPStreamCompleted(Stream *s, MyTCPiopb *pb);

pascal void TCPNotifyProc(StreamPtr tcpStream, TCPEventCode eventCode,
	Ptr userDataPtr, TCPTerminationReason terminReason,
	struct ICMPReport *icmpMsg);

static StreamProvider tcpStreamProvider = {
	.open = TCPStreamOpen,
	.close = TCPStreamClose,
	.write = TCPStreamWrite,
	.poll = TCPStreamPoll,
};

// id of the open MacTCP driver
short tcpRefNum;

// utility function for printing an IP address.
// returns a static buffer (expect to be overridden upon next call)
const char *sprint_ip_addr(ip_addr ip)
{
	static char addr_str[16];
	char i, len = 0;
	for (i = 0; i < 4; i++) {
		// i'm too lazy to convert hex to string manually
		char byte = (ip << i) & 0xff;
		len += snprintf(addr_str+len, sizeof(addr_str)-len, "%hhu", byte);
		addr_str[len++] = '.';
	}
	addr_str[len] = '\0';
	return addr_str;
}

// make a tcp param block
// on success, returns pointer to param block
// on failure, returns NULL and and sends on error
TCPiopb *NewTCPPB(TCPData *tcpData)
{
	Stream *stream = tcpData->stream;
	if (!stream) {
		printf("missing stream in newtcppb\n");
		StreamErrored(stream, tcpMissingStreamErr);
		return NULL;
	}
	MyTCPiopb *pb = calloc(1, sizeof(MyTCPiopb));
	if (!pb) {
		StreamErrored(stream, tcpOutOfMemoryErr);
		return NULL;
	}
	pb->tcpData = tcpData;
	pb->pb.tcpStream = tcpData->tcpStream;
	if (!tcpRefNum) {
		// open the MacTCP driver
		OSErr err = OpenDriver("\p.IPP", &tcpRefNum);
		if (err != noErr) {
			if (err == fnfErr) {
				StreamErrored(stream, tcpMissingDriverErr);
			} else {
				StreamErrored(stream, tcpSetupErr);
			}
			free(pb);
			return NULL;
		}
	}
	pb->pb.ioCRefNum = tcpRefNum;
	return (TCPiopb *)pb;
}

// provide a tcp stream
void ProvideTCPActiveStream(Stream *s, ip_addr ip, tcp_port port)
{
	if (!s) return;
	TCPData *tcpData = NewTCPData(s);
	StreamProvide(s, &tcpStreamProvider, tcpData);
	if (!tcpData) {
		// NewTCPData will have sent an error
		return;
	}
	tcpData->remoteHost = ip;
	tcpData->remotePort = port;
}

// make a data object for a tcp stream
// on success, returns pointer to tcp data
// on failure, returns null and sends error
TCPData *NewTCPData(Stream *s)
{
	TCPData *tcpData = malloc(sizeof(TCPData));
	if (!tcpData) {
		StreamErrored(s, tcpOutOfMemoryErr);
		return NULL;
	}
	tcpData->stream = s;
	tcpData->completedPBsTail = NULL;
	tcpData->completedPBsHead = NULL;
	TCPiopb *pb = NewTCPPB(tcpData);
	if (!pb) {
		free(tcpData);
		return NULL;
	}
	pb->csCode = TCPCreate;
	pb->csParam.create.rcvBuff = tcpData->recvBuf;
	pb->csParam.create.rcvBuffLen = sizeof(tcpData->recvBuf);
	pb->csParam.create.notifyProc = TCPNotifyProc;
	pb->csParam.create.userDataPtr = (Ptr)tcpData;
	// we'll do this one synchronously
	// since it should be pretty quick
	PBControlSync((ParmBlkPtr)pb);
	tcpData->tcpStream = pb->tcpStream;
	switch (pb->ioResult) {
		case noErr:
			return tcpData;
		case insufficientResources:
			StreamErrored(s, tcpStreamLimitErr);
		default:
			StreamErrored(s, tcpCreateStreamErr);
	}
	return NULL;
}

void TCPStreamOpen(Stream *stream, void *providerData)
{
	TCPData *tcpData = (TCPData *)providerData;
	if (!tcpData) {
		printf("missing stream in open\n");
		StreamErrored(stream, tcpMissingStreamErr);
		return;
	}
	TCPiopb *pb = NewTCPPB(tcpData);
	if (!pb) {
		return;
	}

	TCPOpenPB *openPb = &pb->csParam.open;
	pb->csCode = TCPActiveOpen;
	pb->ioCompletion = TCPIOComplete;
	openPb->remoteHost = tcpData->remoteHost;
	openPb->remotePort = tcpData->remotePort;
	openPb->userDataPtr = providerData;

	printf("port: %hu. stream: %p\n", openPb->remotePort, pb->tcpStream);
	puts("press enter?\n");
	getchar();

	PBControlAsync((ParmBlkPtr)pb);
	printf("open done. result: %hd\n", pb->ioResult);
	// TODO: check for completion here in case it was synchronous
}

void TCPStreamClose(Stream *s, void *providerData)
{
}

void TCPStreamWrite(Stream *s, void *providerData, char *data, short len)
{
}

void TCPStreamPoll(Stream *stream, void *providerData)
{
	TCPData *tcpData = (TCPData *)providerData;
	MyTCPiopb *pb, *pbNext;
	// Handle each completed operation
	for (pb = tcpData->completedPBsHead; pb; pb = pbNext) {
		TCPStreamCompleted(stream, pb);
		pbNext = (MyTCPiopb *)pb->nextCompleted;
	}
	tcpData->completedPBsHead = NULL;
	tcpData->completedPBsTail = NULL;
}

// handle completed IO operation, not in interrupt
void TCPStreamCompleted(Stream *stream, MyTCPiopb *pb)
{
	switch (pb->pb.csCode) {
		case TCPActiveOpen:
			switch (pb->pb.ioResult) {
				case noErr:
					StreamOpened(stream);
					break;
				case connectionTerminated:
					StreamErrored(stream, tcpConnectErr);
					break;
				default:
					printf("open error: %hd\n", pb->pb.ioResult);
					StreamErrored(stream, tcpConnectErr);
			}
			free(pb);
			break;
		case TCPPassiveOpen:
		case TCPClose:
		case TCPAbort:
		case TCPSend:
		case TCPNoCopyRcv:
		default:
			printf("unhandled TCP operation completed\n");
	}
}

// asynchronous notification routine
pascal void TCPNotifyProc(
	StreamPtr tcpStream,
	TCPEventCode eventCode,
	Ptr userDataPtr,
	TCPTerminationReason terminReason,
	struct ICMPReport *icmpMsg)
	/*
	 * Register A1 contains a pointer to the Internet Control Message Protocol
	 * (ICMP) report structure if the event code in D0 is ICMP received, A2
	 * contains the user data pointer, A5 is already set up to point to
	 * application
	 * globals, D0 (word) contains an event code, and D1 contains a reason for
	 * termination.
	 */
{
	switch (eventCode) {
		case TCPClosing:
			printf("tcp connection closing\n");
		break;
		case TCPULPTimeout:
			printf("tcp timeout\n");
		break;
		case TCPTerminate:
			printf("tcp connection terminated\n");
		break;
		case TCPDataArrival:
			printf("tcp data arrival\n");
		break;
		case TCPUrgent:
		break;
		case TCPICMPReceived:
		break;
		default:
			printf("unhandled tcp notification\n");
		break;
	}
}

// Asynchronous IO completion function for PBControl calls
// May be executed in interrupt.
#pragma parameter TCPIOComplete(__A0)
void TCPIOComplete(TCPiopb *thePB)
{
	MyTCPiopb *pb = (MyTCPiopb *)thePB;
	TCPData *tcpData = pb->tcpData;
	// Put the PB on our queue of completed operations.
	// Then we can retrieve it in TCPStreamPoll.
	// Make use of the linked-list pointer which is not used after completion.
	if (tcpData->completedPBsTail) {
		tcpData->completedPBsTail->nextCompleted = pb;
	} else {
		tcpData->completedPBsHead = pb;
	}
	tcpData->completedPBsTail = pb;

	// notify the stream manager that we have data to poll for
	StreamWait(pb->tcpData->stream);
}
