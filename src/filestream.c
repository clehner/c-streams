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
	MyParamBlock *completedPBsHead;
	MyParamBlock *completedPBsTail;
} FileData;

struct MyParamBlock {
	IOParam ioParam;
	FileData *fileData;
};

void FileStreamComplete(MyParamBlock *pb);
void FileStreamOpen(Stream *s, void *providerData);
void FileStreamClose(Stream *s, void *providerData);
void FileStreamRead(Stream *s, FileData *fileData);
void FileStreamWrite(Stream *s, void *providerData, char *data, short len);
bool FileStreamPoll(Stream *s, void *providerData, char **data, short *len);

IOCompletionUPP asyncFileCompletionProc = nil;

static StreamProvider fileStreamProvider = {
	.open = FileStreamOpen,
	.close = FileStreamClose,
	.write = FileStreamWrite,
	.poll = FileStreamPoll
};

ParmBlkPtr NewPB(FileData *fileData)
{
	MyParamBlock *pb = malloc(sizeof(MyParamBlock));
	if (!pb) return NULL;
	pb->fileData = fileData;
	if (!asyncFileCompletionProc) {
		asyncFileCompletionProc = NewIOCompletionProc((void (*) (union ParamBlockRec *) ) FileStreamComplete);
	}
	pb->ioParam.ioCompletion = asyncFileCompletionProc;
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
	fileData->completedPBsHead = NULL;
	fileData->completedPBsTail = NULL;
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
	printf("open done. result: %u\n", pb->ioParam.ioResult);
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

void FileStreamRead(Stream *s, FileData *fileData)
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
	pb->ioParam.ioPosMode = 0;
	pb->ioParam.ioPosOffset = fsAtMark;
	PBReadAsync(pb);
}

bool FileStreamPoll(Stream *s, void *providerData, char **data, short *len)
{
	FileData *fileData = (FileData *)providerData;
	MyParamBlock *pb, *pbNext;

	// Get the next completed operation
	pb = fileData->completedPBsHead;
	if (!pb) {
		// no completed operations
		return false;
	}
	printf("something completed\n");

	pbNext = (MyParamBlock *)pb->ioParam.qLink;
	OSErr oe = pb->ioParam.ioResult;
	// Check which type of operation this was
	switch (pb->ioParam.ioTrap) {
		// 0xA000
		case 0x00: // Open
			printf("open completed\n");
			fileData->refNum = pb->ioParam.ioRefNum;
			// check ioResult
			// start reading
			FileStreamRead(s, fileData);
			break;
		case 0x01: // Close
			// check ioResult
			printf("close completed\n");
			break;
		case 0x03: // Write
			printf("write completed\n");
			// check ioActCount, ioPosOffset, ioResult
			break;
		case 0x02: // Read
			printf("got read\n");
			*data = pb->ioParam.ioBuffer;
			*len = pb->ioParam.ioActCount;
			if (pb->ioParam.ioResult == eofErr) {
				//*len = 0;
			}
			// do another read
			FileStreamRead(s, fileData);
			break;
		case 0x04: // Control
		case 0x05: // Status
		case 0x06: // KillIO
		default:
			printf("unhandled operation completed\n");
	}

	fileData->completedPBsHead = pbNext;
	if (!pbNext) {
		// list is empty
		fileData->completedPBsTail = NULL;
	}
	free(pb);
	return true;
}

// IO completion function executed in interrupt
void FileStreamComplete(MyParamBlock *pb)
{
	printf("addr: %p\n", pb);
	return;
	//printf("fileData: %p, fileName: %s\n", pb->fileData, pb->fileData->fileName+1);
	// TODO: find a safe place to store the pointer to fileData
	FileData *fileData = pb->fileData;
	// Put the PB on our queue of completed operations.
	// Then we can retrieve it in Poll.
	// Make use of the linked-list pointer which is not used after completion.
	if (fileData->completedPBsTail) {
		((IOParamPtr)fileData->completedPBsTail)->qLink = (QElemPtr)pb;
	} else {
		fileData->completedPBsHead = pb;
	}
	fileData->completedPBsTail = pb;
}
