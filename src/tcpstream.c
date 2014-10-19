#include <Files.h>
#include <Devices.h>
#include <Processes.h>
#include <MacTCP.h>
#include <AddressXlation.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "stream.h"
#include "tcpstream.h"

#define ARRAYSIZE(array) (sizeof(array) / sizeof((array)[0]))

typedef struct MyTCPiopb MyTCPiopb;

typedef struct {
	QHdr completedPBs;
	Stream *stream;
	StreamPtr tcpStream;
	char recvBuf[4096];
	rdsEntry rds[32];

	struct {
		char name[256];
		ip_addr addr;
		tcp_port port;
		struct hostInfo *resolveInfo;
	} remoteHost;

	enum {
		notResolved,
		resolveInProgress,
		resolveCompleted,
		resolveFinished
	} resolveState;
} TCPData;

struct MyTCPiopb {
	TCPiopb pb;
	TCPData *tcpData;
};

TCPData *NewTCPData(Stream *s);
void TCPStreamResolve(TCPData *tcpData);
void TCPStreamResolveCompleted(TCPData *tcpData);
void TCPStreamOpen(Stream *s, void *providerData);
void TCPStreamClose(Stream *s, void *providerData);
void TCPStreamWrite(Stream *s, void *pData, char *data, unsigned short len);
void TCPStreamPoll(Stream *s, void *providerData);

void TCPStreamReceive(TCPData *tcpData, MyTCPiopb *pb);
void TCPStreamCompleted(Stream *s, MyTCPiopb *pb);
void TCPStreamRelease(Stream *stream,  TCPData *tcpData);
void TCPStreamClosed(TCPData *tcpData);

void TCPIOComplete(TCPiopb *thePB);
pascal void TCPNotifyProc(StreamPtr tcpStream, TCPEventCode eventCode,
	Ptr userDataPtr, TCPTerminationReason terminReason,
	struct ICMPReport *icmpMsg);
pascal void StrToAddrProc(struct hostInfo *hostInfoPtr, char *userDataPtr);

static StreamProvider tcpStreamProvider = {
	.open = TCPStreamOpen,
	.close = TCPStreamClose,
	.write = TCPStreamWrite,
	.poll = TCPStreamPoll,
};

// id of the open MacTCP driver
short tcpRefNum;
bool resolverOpen;

// utility function for printing an IP address.
// returns a static buffer (expect to be overridden upon next call)
const char *sprint_ip_addr(ip_addr ip)
{
	static char addr_str[16];
	char i, len = 0;
	for (i = 0; i < 4; i++) {
		unsigned char byte = ((unsigned char *)&ip)[i];
		len += snprintf(addr_str+len, sizeof(addr_str)-len, "%hhu.", byte);
	}
	addr_str[len-1] = '\0';
	return addr_str;
}

// resolve the hostname so that Open may proceed
void TCPStreamResolve(TCPData *tcpData)
{
	OSErr err;

	if (tcpData->resolveState != notResolved) {
		// already resolving or resolved
		return;
	}

	tcpData->resolveState = resolveInProgress;
	if (!resolverOpen) {
		if ((err = OpenResolver(NULL))) {
			alertf("Unable to open DNS resolver: %hu", err);
			return;
		}
		resolverOpen = true;
	}

	tcpData->remoteHost.resolveInfo = malloc(sizeof(struct hostInfo));
	if (!tcpData->remoteHost.resolveInfo) {
		alertf("Out of memory");
		return;
	}

	err = StrToAddr(tcpData->remoteHost.name,
			tcpData->remoteHost.resolveInfo,
			StrToAddrProc, (Ptr)tcpData);
	if (err == noErr) {
		// completed already?
		if (tcpData->resolveState == resolveInProgress) {
			TCPStreamResolveCompleted(tcpData);
		}
	}
}

void TCPStreamResolveCompleted(TCPData *tcpData)
{
	char *s;
	struct hostInfo *resolveInfo = tcpData->remoteHost.resolveInfo;
	tcpData->resolveState = resolveFinished;

	switch (resolveInfo->rtnCode) {
		case noErr:
			s = NULL;
			break;
		case nameSyntaxErr:
			s = "Syntax error in name";
			break;
		case noResultProc:
			s = "No result procedure";
			break;
		case noNameServer:
			s = "No name server found";
			break;
		case authNameErr:
			s = "Host does not exist";
			break;
		case noAnsErr:
			s = "No name servers responding";
			break;
		case dnrErr:
			s = "Name server returned an error";
			break;
		case outOfMemory:
			s = "Not enough memory to resolve name";
			break;
		case notOpenErr:
			s = "Driver not open";
			break;
		default:
			s = "Unknown";
	}
	if (s) alertf("Resolver: %s: %.80s", s, tcpData->remoteHost.name);

	// proceed with opening the stream
	tcpData->remoteHost.addr = resolveInfo->addr[0];
	tcpData->remoteHost.resolveInfo = NULL;
	free(resolveInfo);
	TCPStreamOpen(tcpData->stream, tcpData);
}

// make a tcp param block
// on success, returns pointer to param block
// on failure, returns NULL and and sends on error
TCPiopb *NewTCPPB(TCPData *tcpData, short csCode)
{
	Stream *stream = tcpData->stream;
	if (!stream || !tcpData->tcpStream) {
		StreamErrored(stream, tcpMissingStreamErr);
		return NULL;
	}
	MyTCPiopb *pb = calloc(1, sizeof(MyTCPiopb));
	if (!pb) {
		StreamErrored(stream, tcpOutOfMemoryErr);
		return NULL;
	}
	pb->tcpData = tcpData;
	pb->pb.csCode = csCode;
	pb->pb.tcpStream = tcpData->tcpStream;
	pb->pb.ioCompletion = TCPIOComplete;
	pb->pb.ioCRefNum = tcpRefNum;
	return (TCPiopb *)pb;
}

// provide a tcp stream
void ProvideTCPActiveStream(Stream *s, const char *host, tcp_port port)
{
	if (!s) return;
	TCPData *tcpData = NewTCPData(s);
	StreamProvide(s, &tcpStreamProvider, tcpData);
	if (!tcpData) {
		// NewTCPData will have sent an error
		return;
	}
	tcpData->remoteHost.port = port;
	strncpy(tcpData->remoteHost.name, host, sizeof(tcpData->remoteHost.name));
}

// make a data object for a tcp stream
// on success, returns pointer to tcp data
// on failure, returns null and sends error
TCPData *NewTCPData(Stream *s)
{
	TCPiopb pb;
	TCPData *tcpData = malloc(sizeof(TCPData));
	if (!tcpData) {
		StreamErrored(s, tcpOutOfMemoryErr);
		return NULL;
	}

	if (!tcpRefNum) {
		// open the MacTCP driver
		OSErr err = OpenDriver("\p.IPP", &tcpRefNum);
		if (err != noErr) {
			if (err == fnfErr) {
				StreamErrored(s, tcpMissingDriverErr);
			} else {
				StreamErrored(s, tcpSetupErr);
			}
			return NULL;
		}
	}

	pb.csCode = TCPCreate;
	pb.ioCRefNum = tcpRefNum;
	pb.csParam.create.rcvBuff = tcpData->recvBuf;
	pb.csParam.create.rcvBuffLen = sizeof(tcpData->recvBuf);
	pb.csParam.create.notifyProc = TCPNotifyProc;
	PBControlSync((ParmBlkPtr)&pb);
	switch (pb.ioResult) {
		case noErr:
			break;
		case insufficientResources:
			StreamErrored(s, tcpStreamLimitErr);
			return NULL;
		case streamAlreadyOpen:
			StreamErrored(s, -10);
			return NULL;
		case connectionExists:
			// FIXME
			StreamErrored(s, -11);
			return NULL;
		case invalidLength:
			StreamErrored(s, -12);
			return NULL;
		case invalidBufPtr:
			StreamErrored(s, -13);
			return NULL;
		default:
			StreamErrored(s, pb.ioResult);
			return NULL;
	}

	tcpData->stream = s;
	tcpData->tcpStream = pb.tcpStream;
	tcpData->completedPBs.qHead = NULL;
	tcpData->completedPBs.qTail = NULL;
	tcpData->resolveState = notResolved;
	tcpData->remoteHost.addr = 0;
	return tcpData;
}

void TCPStreamOpen(Stream *stream, void *providerData)
{
	TCPData *tcpData = (TCPData *)providerData;
	if (!tcpData) return;

	if (tcpData->resolveState != resolveFinished) {
		// resolve the host before connecting
		TCPStreamResolve(tcpData);
		return;
	}

	TCPiopb *pb = NewTCPPB(tcpData, TCPActiveOpen);
	if (!pb) return;

	TCPOpenPB *openPb = &pb->csParam.open;
	openPb->remotePort = tcpData->remoteHost.port;
	openPb->remoteHost = tcpData->remoteHost.addr;

	printf("port: %hu. stream: %p\n", openPb->remotePort, pb->tcpStream);
	//puts("open stream?\n");
	//getchar();

	PBControlAsync((ParmBlkPtr)pb);
	//printf("open done. result: %hd\n", pb->ioResult);
	// TODO: check for completion here in case it was synchronous
}

void TCPStreamClose(Stream *stream, void *providerData)
{
	TCPData *tcpData = (TCPData *)providerData;
	if (!tcpData || !tcpData->tcpStream) {
		// don't send an error on duplicate close
		return;
	}
	TCPiopb *pb = NewTCPPB(tcpData, TCPClose);
	if (!pb) return;

	TCPClosePB *closePb = &pb->csParam.close;
	closePb->ulpTimeoutValue = 30; // seconds without FIN acknowledged
	closePb->ulpTimeoutAction = 1; // abort on timeout
	closePb->validityFlags = 0xC0; // timeout value and action are valid
	//tcpData->tcpStream = NULL;
	PBControlAsync((ParmBlkPtr)pb);
}

/*
void TCPStreamAbort(Stream *stream, void *providerData)
{
	TCPData *tcpData = (TCPData *)providerData;
	if (!tcpData) {
		StreamErrored(stream, tcpMissingStreamErr);
		return;
	}
	TCPiopb *pb = NewTCPPB(tcpData, TCPAbort);
	if (!pb) return;

	PBControlAsync((ParmBlkPtr)pb);
}
*/

void TCPStreamWrite(Stream *s, void *pData, char *data, unsigned short len)
{
	TCPData *tcpData = (TCPData *)pData;
	if (!tcpData) {
		StreamErrored(s, tcpMissingStreamErr);
		return;
	}
	TCPiopb *pb = NewTCPPB(tcpData, TCPSend);
	if (!pb) return;

	// TODO: allow data to be sent without copying
	// copy the data into a new wds struct
	// second element of the wds list is null terminator
	wdsEntry *wds = calloc(2, sizeof(wdsEntry));
	if (!wds) {
		StreamErrored(s, tcpOutOfMemoryErr);
		free(pb);
		return;
	}
	char *data_copy = malloc(len);
	if (!data_copy) {
		StreamErrored(s, tcpOutOfMemoryErr);
		free(pb);
		free(wds);
		return;
	}
	memcpy(data_copy, data, len);
	wds->length = len;
	wds->ptr = data_copy;
	pb->csParam.send.wdsPtr = (Ptr)wds;
	PBControlAsync((ParmBlkPtr)pb);
}

// get data
void TCPStreamReceive(TCPData *tcpData, MyTCPiopb *pb)
{
	pb->pb.csCode = TCPNoCopyRcv;
	TCPReceivePB *receivePb = &pb->pb.csParam.receive;
	receivePb->secondTimeStamp = 0;
	receivePb->commandTimeoutValue = 0;
	receivePb->rdsPtr = (Ptr)tcpData->rds;
	receivePb->rdsLength = ARRAYSIZE(tcpData->rds);
	PBControlAsync((ParmBlkPtr)pb);
}

void TCPStreamClosed(TCPData *tcpData)
{
	StreamClosed(tcpData->stream);
	if (tcpData->tcpStream) {
		TCPStreamRelease(tcpData->stream, tcpData);
	}
}

// release the TCP stream and free the memory
//void TCPStreamFree(Stream *stream, void *providerData)
void TCPStreamRelease(Stream *stream, TCPData *tcpData)
{
	TCPiopb pb;
	pb.csCode = TCPRelease;
	pb.tcpStream = tcpData->tcpStream;
	pb.ioCompletion = TCPIOComplete;
	pb.ioCRefNum = tcpRefNum;
	switch (PBControlSync((ParmBlkPtr)&pb)) {
		case invalidStreamPtr:
			StreamErrored(stream, tcpMissingStreamErr);
			break;
		case noErr:
			//free(tcpData);
			break;
	}
	tcpData->tcpStream = 0;
}

// called by stream.c (not in interrupt)
// after we requested it with StreamWait
void TCPStreamPoll(Stream *stream, void *providerData)
{
	TCPData *tcpData = (TCPData *)providerData;
	QHdr *q = &tcpData->completedPBs;
	MyTCPiopb *pb;

	// Handle each completed operation
	while (tcpData->completedPBs.qHead) {
		pb = (MyTCPiopb *)q->qHead;
		if (Dequeue((QElemPtr)pb, q) != noErr) {
			// race condition avoided
			continue;
		}
		TCPStreamCompleted(stream, pb);
	}

	// Handle completed DNS resolution
	if (tcpData->resolveState == resolveCompleted) {
		TCPStreamResolveCompleted(tcpData);
	}
}

// handle completed IO operation, not in interrupt
void TCPStreamCompleted(Stream *stream, MyTCPiopb *pb)
{
	TCPData *tcpData = pb->tcpData;
	if (!tcpData) {
		return;
	}
	switch (pb->pb.csCode) {
		case TCPPassiveOpen:
		case TCPActiveOpen:
			switch (pb->pb.ioResult) {
				case noErr:
					StreamOpened(stream);
					// Start auto-receive. Reuse pb.
					TCPStreamReceive(tcpData, pb);
					break;
				case connectionTerminated:
					StreamErrored(stream, tcpConnectErr);
					free(pb);
					break;
				default:
					alertf("open error: %hd\n", pb->pb.ioResult);
					StreamErrored(stream, tcpConnectErr);
					free(pb);
			}
			break;
		case TCPClose:
			//alertf("closing: %hu", pb->pb.ioResult);
			// let TCPNoCopyRcv completion send the close event
			switch (pb->pb.ioResult) {
				case noErr:
					StreamEnded(stream);
					//TCPStreamClosed(tcpData);
					break;
				case connectionTerminated:
					StreamErrored(stream, tcpTerminatedErr);
					//TCPStreamClosed(tcpData);
					break;
				case invalidStreamPtr:
				case connectionDoesntExist:
				case connectionClosing:
					StreamErrored(stream, tcpMissingStreamErr);
					break;
				default:
					StreamErrored(stream, tcpInternalErr);
			}
			free(pb);
			break;
		/*
		case TCPAbort:
			switch (pb->pb.ioResult) {
				case noErr:
					TCPStreamClosed(tcpData);
					break;
				case invalidStreamPtr:
				case connectionDoesntExist:
					StreamErrored(stream, tcpMissingStreamErr);
					break;
				default:
					StreamErrored(stream, tcpInternalErr);
			}
			free(pb);
		*/
		case TCPSend: {
			// free the wds and sent data
			wdsEntry *wds = (wdsEntry *)pb->pb.csParam.send.wdsPtr;
			free(wds->ptr);
			free(wds);
			switch (pb->pb.ioResult) {
				case noErr:
					// success!
					break;
				case connectionTerminated:
					StreamErrored(stream, tcpTerminatedErr);
					// let TCPNoCopyRcv completeion send the close event
					//TCPStreamClosed(tcpData);
					break;
				case invalidStreamPtr:
				case connectionDoesntExist:
				case connectionClosing:
					StreamErrored(stream, tcpMissingStreamErr);
					break;
				case invalidLength:
				case invalidWDS:
				default:
					StreamErrored(stream, tcpInternalErr);
			}
			free(pb);
			break;
		}
		case TCPNoCopyRcv: {
			short rcvResult = pb->pb.ioResult;

			// Process the received data
			rdsEntry *rds = tcpData->rds;
			for (; rds->length; rds++) {
				StreamRead(stream, rds->ptr, rds->length);
			}

			// return rds bufs
			pb->pb.csCode = TCPRcvBfrReturn;
			// reuse pb. rds pointer is already set
			OSErr oe = PBControlSync((ParmBlkPtr)pb);
			if (oe != noErr) {
				StreamErrored(stream, tcpInternalErr);
			}

			switch (rcvResult) {
				case noErr:
					// Start another receive
					TCPStreamReceive(tcpData, pb);
					break;
				case connectionClosing:
					TCPStreamClosed(tcpData);
					free(pb);
					break;
				case connectionTerminated:
					StreamErrored(stream, tcpTerminatedErr);
					TCPStreamClosed(tcpData);
					break;
				case invalidStreamPtr:
				case connectionDoesntExist:
					StreamErrored(stream, tcpMissingStreamErr);
					break;
				case invalidLength:
				case invalidBufPtr:
				default:
					StreamErrored(stream, tcpInternalErr);
			}
			break;
		}
		default:
			printf("unhandled TCP operation completed\n");
	}
}

// asynchronous notification routine
pascal void TCPNotifyProc(
	StreamPtr tcpStream,
	TCPEventCode eventCode,
	Ptr userData,
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
	TCPData *tcpData = (TCPData *)userData;
	if (!tcpData) {
		return;
	}
	Stream *stream = tcpData->stream;
	if (!stream) {
		return;
	}
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
			printf("data arrived without recv in progress\n");
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
	Enqueue((QElemPtr)pb, &tcpData->completedPBs);

	// notify the stream manager that we have data to poll for
	StreamWait(pb->tcpData->stream);
}

pascal void StrToAddrProc(struct hostInfo *hostInfo, char *userData)
{
	TCPData *tcpData = (TCPData *)userData;
	tcpData->resolveState = resolveCompleted;
	StreamWait(tcpData->stream);
}
