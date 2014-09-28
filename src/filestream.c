#include <stdlib.h>
#include <stdio.h>
#include <Files.h>
#include <Devices.h>
#include "stream.h"
#include "filestream.h"

typedef struct MyParamBlock MyParamBlock;

typedef struct {
	char *fileName;
	short vRefNum;
	short refNum;
	char readBuf[256];
	Stream *stream;
} FileData;

struct MyParamBlock {
	IOParam ioParam;
	FileData *fileData;
};

void FileStreamOpen(Stream *s, void *providerData);
void FileStreamClose(Stream *s, void *providerData);
void FileStreamWrite(Stream *s, void *providerData, char *data, short len);
void FileStreamRead(FileData *fileData);
void FileStreamComplete(MyParamBlock *pb);
void FileStreamCompleted(MyParamBlock *pb);

MyParamBlock *completedPBsHead = NULL;
MyParamBlock *completedPBsTail = NULL;

static StreamProvider fileStreamProvider = {
	.open = FileStreamOpen,
	.close = FileStreamClose,
	.write = FileStreamWrite,
};

ParmBlkPtr NewPB(FileData *fileData)
{
	MyParamBlock *pb = malloc(sizeof(MyParamBlock));
	if (!pb) return NULL;
	pb->fileData = fileData;
	pb->ioParam.ioCompletion = FileStreamComplete;
	return (ParmBlkPtr)pb;
}

// provide a file to a stream for read/write
void ProvideFileStream(Stream *s, char *fileName, short vRefNum)
{
	if (!s) return;
	FileData *fileData = malloc(sizeof(FileData));
	if (!fileData) {
		free(s);
		return;
	}
	fileData->fileName = fileName;
	fileData->vRefNum = vRefNum;
	fileData->stream = s;
	StreamProvide(s, &fileStreamProvider, fileData);
}

void FileStreamOpen(Stream *s, void *providerData)
{
	FileData *fileData = (FileData *)providerData;

	ParmBlkPtr pb = NewPB(fileData);
	printf("open. addr: %p, fileData: %p\n", pb, ((MyParamBlock *)pb)->fileData);
	printf("opened fileName: %s\n", ((MyParamBlock *)pb)->fileData->fileName+1);
	if (!pb) {
		// error
		return;
	}

	pb->ioParam.ioNamePtr = fileData->fileName;
	pb->ioParam.ioVRefNum = fileData->vRefNum;
	pb->ioParam.ioVersNum = 0;
	pb->ioParam.ioPermssn = 0;
	pb->ioParam.ioMisc = 0;
	PBOpenAsync(pb);
	printf("open done. result: %d\n", pb->ioParam.ioResult);
}

void FileStreamClose(Stream *s, void *providerData)
{
	FileData *fileData = (FileData *)providerData;
	ParmBlkPtr pb = NewPB(fileData);
	if (!pb) {
		// error
		return;
	}
	pb->ioParam.ioRefNum = fileData->refNum;
	PBCloseAsync(pb);
}

void FileStreamWrite(Stream *s, void *providerData, char *data, short len)
{
	FileData *fileData = (FileData *)providerData;
	ParmBlkPtr pb = NewPB(fileData);
	if (!pb) {
		// error
		return;
	}
	pb->ioParam.ioRefNum = fileData->refNum;
	pb->ioParam.ioBuffer = data;
	pb->ioParam.ioReqCount = len;
	pb->ioParam.ioPosMode = fsAtMark;
	pb->ioParam.ioPosOffset = 0;
	PBWriteAsync(pb);
}

void FileStreamRead(FileData *fileData)
{
	ParmBlkPtr pb = NewPB(fileData);
	if (!pb) {
		// error
		return;
	}
	// TODO: try using continuous mode (set bit 7 of ioPosMode)
	pb->ioParam.ioRefNum = fileData->refNum;
	pb->ioParam.ioBuffer = fileData->readBuf;
	pb->ioParam.ioReqCount = sizeof fileData->readBuf;
	pb->ioParam.ioPosMode = 0x0D80; // one line at a time from current mark
	pb->ioParam.ioPosOffset = 0;
	PBReadAsync(pb);
}

// handle completed IO operation, in main thread
void FileStreamCompleted(MyParamBlock *pb)
{
	FileData *fileData = pb->fileData;
	Stream *s = fileData->stream;
	OSErr oe = pb->ioParam.ioResult;
	unsigned short trap = pb->ioParam.ioTrap;

	//printf("pb: %p, trap: %hx, fileName: %s\n", pb, trap, fileData->fileName+1);

	// Check which type of operation this was
	switch (trap) {
		case 0xA400: // Open
			fileData->refNum = pb->ioParam.ioRefNum;
			printf("open completed: %hd. refnum: %u\n", oe, fileData->refNum);
			// check for error
			if (oe == noErr) {
				// start reading
				FileStreamRead(fileData);
			} else {
				// notify about error
				StreamErrored(s, oe);
			}
			break;
		case 0xA401: // Close
			printf("close completed\n");
			if (oe == noErr) {
				StreamClosed(s);
			} else {
				StreamErrored(s, oe);
			}
			break;
		case 0xA403: // Write
			printf("write completed\n");
			// check ioActCount, ioPosOffset, ioResult
			break;
		case 0xA402: // Read
			//printf("got read [%hu] %s\n", pb->ioParam.ioActCount, pb->ioParam.ioBuffer);
			//printf("requested: %hu, data ptr: %p\n", pb->ioParam.ioReqCount, pb->ioParam.ioBuffer);
			if (oe == noErr) {
				StreamRead(s, pb->ioParam.ioBuffer, pb->ioParam.ioActCount);
				// do another read
				FileStreamRead(fileData);
			} else if (oe == eofErr) {
				StreamRead(s, pb->ioParam.ioBuffer, pb->ioParam.ioActCount);
				// signal stream ended
				StreamEnded(s);
				// close the file
				StreamClose(s);
			} else {
				// pass along error
				StreamErrored(s, oe);
			}
			break;
		case 0xA404: // Control
		case 0xA405: // Status
		case 0xA406: // KillIO
		default: {
			printf("unhandled operation\n");
		}
	}
}

// Asynchronous IO completion function.
// May be executed in interrupt.
#pragma parameter FileStreamComplete(__A0)
void FileStreamComplete(MyParamBlock *pb)
{
	FileData *fileData = pb->fileData;
	// Put the PB on our queue of completed operations.
	// Then we can retrieve it in PollFileStreams.
	// Make use of the linked-list pointer which is not used after completion.
	if (completedPBsTail) {
		((IOParamPtr)completedPBsTail)->qLink = (QElemPtr)pb;
	} else {
		completedPBsHead = pb;
	}
	completedPBsTail = pb;
}

void PollFileStreams()
{
	//FileData *fileData = completedPBsHead;
	//StreamRead(stream);
	MyParamBlock *pb, *pbNext;
	//FileData *fileData = (FileData *)providerData;
	// Handle each completed operation
	for (pb = completedPBsHead; pb; pb = pbNext) {
		FileStreamCompleted(pb);
		pbNext = (MyParamBlock *)pb->ioParam.qLink;
		free(pb);
	}
	completedPBsHead = NULL;
	completedPBsTail = NULL;
}
