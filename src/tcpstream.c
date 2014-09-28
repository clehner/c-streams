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

TCPiopb *NewTCPPB(TCPData *tcpData)
{
	MyTCPiopb *pb = calloc(1, sizeof(MyTCPiopb));
	if (!pb) return NULL;
	pb->tcpData = tcpData;
	pb->pb.tcpStream = tcpData->tcpStream;
	if (!tcpRefNum) {
		// open the driver
		OSErr err = OpenDriver("\p.IPP", &tcpRefNum);
		if (err) {
			StreamErrored(tcpData->stream, -98);
			StreamErrored(tcpData->stream, err);
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
		StreamErrored(s, -1);
		return;
	}
	tcpData->remoteHost = ip;
	tcpData->remotePort = port;
}

TCPData *NewTCPData(Stream *s)
{
	TCPData *tcpData = malloc(sizeof(TCPData));
	TCPiopb *pb;
	if (!tcpData) {
		StreamErrored(s, -1);
		return NULL;
	}
	if (!(pb = NewTCPPB(tcpData))) {
		free(tcpData);
		StreamErrored(s, -2);
		return NULL;
	}
	tcpData->stream = s;
	tcpData->completedPBsTail = NULL;
	tcpData->completedPBsHead = NULL;
	pb->csCode = TCPCreate;
	pb->csParam.create.rcvBuff = tcpData->recvBuf;
	pb->csParam.create.rcvBuffLen = sizeof(tcpData->recvBuf);
	pb->csParam.create.notifyProc = TCPNotifyProc;
	pb->csParam.create.userDataPtr = (Ptr)tcpData;
	// we'll do this one synchronously
	// since it should be pretty quick
	PBControlSync((ParmBlkPtr)pb);
	tcpData->tcpStream = pb->tcpStream;
	if (pb->ioResult != noErr) {
		StreamErrored(s, -99);
		StreamErrored(s, pb->ioResult);
	}
	return tcpData;
}

void TCPStreamOpen(Stream *s, void *providerData)
{
	TCPData *tcpData = (TCPData *)providerData;
	TCPOpenPB *openPb;
	if (!tcpData) {
		StreamErrored(s, -3);
		return;
	}
	TCPiopb *pb = NewTCPPB(tcpData);
	if (!pb) {
		StreamErrored(s, -4);
		return;
	}

	openPb = &pb->csParam.open;
	pb->csCode = TCPActiveOpen;
	pb->ioCompletion = TCPIOComplete;
	openPb->remoteHost = tcpData->remoteHost;
	openPb->remotePort = tcpData->remotePort;
	openPb->userDataPtr = providerData;

	PBControlAsync((ParmBlkPtr)pb);
	printf("open done. result: %d\n", pb->ioResult);
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
void TCPStreamCompleted(Stream *s, MyTCPiopb *pb)
{
	printf("tcp something completed\n");
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
		break;
		case TCPULPTimeout:
		break;
		case TCPTerminate:
		break;
		case TCPDataArrival:
		break;
		case TCPUrgent:
		break;
		case TCPICMPReceived:
		break;
		default:
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
