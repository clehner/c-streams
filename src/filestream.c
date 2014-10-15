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
	MyParamBlock *completedPBsHead;
	MyParamBlock *completedPBsTail;
} FileData;

struct MyParamBlock {
	IOParam ioParam;
	FileData *fileData;
};

void FileStreamOpen(Stream *s, void *providerData);
void FileStreamClose(Stream *s, void *providerData);
void FileStreamWrite(Stream *s, void *pData, char *data, unsigned short len);
void FileStreamPoll(Stream *s, void *providerData);
void FileStreamRead(FileData *fileData, MyParamBlock *pb);
void FileStreamComplete(MyParamBlock *pb);
void FileStreamCompleted(Stream *s, MyParamBlock *pb);

static StreamProvider fileStreamProvider = {
	.open = FileStreamOpen,
	.close = FileStreamClose,
	.write = FileStreamWrite,
	.poll = FileStreamPoll,
};

MyParamBlock *NewPB(FileData *fileData)
{
	MyParamBlock *pb = malloc(sizeof(MyParamBlock));
	if (!pb) return NULL;
	pb->fileData = fileData;
	pb->ioParam.ioCompletion = FileStreamComplete;
	return pb;
}

// provide a file to a stream for read/write
void ProvideFileStream(Stream *s, StringPtr fileName, short vRefNum)
{
	if (!s) return;
	FileData *fileData = malloc(sizeof(FileData));
	if (!fileData) {
		// TODO: error
		return;
	}
	fileData->fileName = fileName;
	fileData->vRefNum = vRefNum;
	fileData->stream = s;
	fileData->completedPBsTail = NULL;
	fileData->completedPBsHead = NULL;
	StreamProvide(s, &fileStreamProvider, fileData);
}

void FileStreamOpen(Stream *s, void *providerData)
{
	FileData *fileData = (FileData *)providerData;

	ParmBlkPtr pb = (ParmBlkPtr)NewPB(fileData);
	if (!pb) {
		StreamErrored(s, fileStreamErr);
		return;
	}

	pb->ioParam.ioNamePtr = fileData->fileName;
	pb->ioParam.ioVRefNum = fileData->vRefNum;
	pb->ioParam.ioVersNum = 0;
	pb->ioParam.ioPermssn = fsRdPerm;
	pb->ioParam.ioMisc = 0;
	PBOpenAsync(pb);
}

void FileStreamClose(Stream *s, void *providerData)
{
	FileData *fileData = (FileData *)providerData;
	ParmBlkPtr pb = (ParmBlkPtr)NewPB(fileData);
	if (!pb) {
		StreamErrored(s, fileStreamErr);
		return;
	}
	pb->ioParam.ioRefNum = fileData->refNum;
	PBCloseAsync(pb);
}

void FileStreamWrite(Stream *s, void *pData, char *data, unsigned short len)
{
	FileData *fileData = (FileData *)pData;
	ParmBlkPtr pb = (ParmBlkPtr)NewPB(fileData);
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

void FileStreamRead(FileData *fileData, MyParamBlock *pb)
{
	if (!pb) {
		pb = NewPB(fileData);
	}
	if (!pb) {
		// error
		return;
	}
	pb->ioParam.ioRefNum = fileData->refNum;
	pb->ioParam.ioBuffer = fileData->readBuf;
	pb->ioParam.ioReqCount = sizeof fileData->readBuf;
	//pb->ioParam.ioPosMode = 0x0D80; // one line at a time from current mark
	pb->ioParam.ioPosMode = fsAtMark;
	pb->ioParam.ioPosOffset = 0;
	PBReadAsync((ParmBlkPtr)pb);
}

// handle completed IO operation, not in interrupt
void FileStreamCompleted(Stream *s, MyParamBlock *pb)
{
	FileData *fileData = pb->fileData;
	OSErr oe = pb->ioParam.ioResult;
	unsigned short trap = pb->ioParam.ioTrap;

	// Check which type of operation this was
	switch (trap) {
		case 0xA400: // Open
			fileData->refNum = pb->ioParam.ioRefNum;
			// check for error
			if (oe == noErr) {
				// start reading
				StreamOpened(s);
				FileStreamRead(fileData, pb);
			} else {
				// notify about error
				StreamErrored(s, oe);
				free(pb);
			}
			break;
		case 0xA401: // Close
			if (oe == noErr) {
				StreamClosed(s);
			} else {
				StreamErrored(s, oe);
			}
			free(pb);
			free(fileData);
			break;
		case 0xA403: // Write
			// check ioActCount, ioPosOffset, ioResult
			break;
		case 0xA402: // Read
			if (oe == noErr) {
				// deliver the data
				StreamRead(s, pb->ioParam.ioBuffer, pb->ioParam.ioActCount);
				// do another read
				FileStreamRead(fileData, pb);
			} else if (oe == eofErr) {
				// deliver the data
				StreamRead(s, pb->ioParam.ioBuffer, pb->ioParam.ioActCount);
				// signal stream ended
				StreamEnded(s);
				// close the file
				StreamClose(s);
				free(pb);
			} else {
				// pass along error
				StreamErrored(s, oe);
				free(pb);
			}
			break;
		case 0xA404: // Control
		case 0xA405: // Status
		case 0xA406: // KillIO
		default: {
			//printf("unhandled operation\n");
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
	// Then we can retrieve it in FileStreamPoll.
	// Make use of the linked-list pointer which is not used after completion.
	if (fileData->completedPBsTail) {
		fileData->completedPBsTail->ioParam.qLink = (QElemPtr)pb;
	} else {
		fileData->completedPBsHead = pb;
	}
	fileData->completedPBsTail = pb;

	// notify the stream manager that we have data to poll for
	StreamWait(pb->fileData->stream);
}

// called by stream manager to tell us to handle completed operations
void FileStreamPoll(Stream *stream, void *providerData)
{
	FileData *fileData = (FileData *)providerData;
	MyParamBlock *pb, *pbNext;
	// Handle each completed operation
	for (pb = fileData->completedPBsHead; pb; pb = pbNext) {
		FileStreamCompleted(stream, pb);
		pbNext = (MyParamBlock *)pb->ioParam.qLink;
	}
	fileData->completedPBsHead = NULL;
	fileData->completedPBsTail = NULL;
}
