#include <stdlib.h>
#include <Files.h>
#include <Devices.h>
#include "stream.h"
#include "filestream.h"

typedef struct {
	char *fileName;
	short vRefNum;
	short refNum;
	ParamBlockRec bp;
	char readBuf[256];
	ParmBlkPtr completedPBsHead;
	ParmBlkPtr completedPBsTail;
} FileData;

void FileStreamComplete(ParmBlkPtr pb);
void FileStreamOpen(Stream *s, void *providerData);
void FileStreamClose(Stream *s, void *providerData);
void FileStreamRead(Stream *s, FileData *fileData);
void FileStreamWrite(Stream *s, void *providerData, char *data, short len);
bool FileStreamPoll(Stream *s, void *providerData, char **data, short *len);

static StreamProvider fileStreamProvider = {
	.open = FileStreamOpen,
	.close = FileStreamClose,
	.write = FileStreamWrite,
	.poll = FileStreamPoll
};

// create a stream to read/write the given file
Stream *NewFileStream(char *fileName, short vRefNum)
{
	Stream *s = NewStream();
	if (!s) return NULL;
	FileData *fileData = malloc(sizeof(FileData));
	if (!fileData) {
		free(s);
		return NULL;
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

	ParmBlkPtr pb = malloc(sizeof(IOParam));
	if (!pb) {
		// error
		return;
	}

	pb->ioParam.ioNamePtr = fileData->fileName;
	pb->ioParam.ioVRefNum = fileData->vRefNum;
	pb->ioParam.ioVersNum = 0;
	pb->ioParam.ioPermssn = 0;
	pb->ioParam.ioMisc = 0;
	pb->ioParam.ioCompletion = FileStreamComplete;
	PBOpenAsync(pb);
}

void FileStreamClose(Stream *s, void *providerData)
{
	FileData *fileData = (FileData *)providerData;
	ParmBlkPtr pb = malloc(sizeof(IOParam));
	if (!pb) {
		// error
		return;
	}
	pb->ioParam.ioRefNum = fileData->refNum;
	pb->ioParam.ioCompletion = FileStreamComplete;
	PBCloseAsync(pb);
}

void FileStreamWrite(Stream *s, void *providerData, char *data, short len)
{
	FileData *fileData = (FileData *)providerData;
	ParmBlkPtr pb = malloc(sizeof(IOParam));
	if (!pb) {
		// error
		return;
	}
	pb->ioParam.ioRefNum = fileData->refNum;
	pb->ioParam.ioBuffer = data;
	pb->ioParam.ioReqCount = len;
	pb->ioParam.ioPosMode = 0;
	pb->ioParam.ioPosOffset = 0;
	pb->ioParam.ioCompletion = FileStreamComplete;
	PBWriteAsync(pb);
}

void FileStreamRead(Stream *s, FileData *fileData)
{
	ParmBlkPtr pb = malloc(sizeof(IOParam));
	if (!pb) {
		// error
		return;
	}
	pb->ioParam.ioRefNum = fileData->refNum;
	pb->ioParam.ioBuffer = fileData->readBuf;
	pb->ioParam.ioReqCount = sizeof fileData->readBuf;
	pb->ioParam.ioPosMode = 0;
	pb->ioParam.ioPosOffset = 0;
	pb->ioParam.ioCompletion = FileStreamComplete;
	PBReadAsync(pb);
}

bool FileStreamPoll(Stream *s, void *providerData, char **data, short *len)
{
	ParmBlkPtr pb, pbNext;
	FileData *fileData = (FileData *)providerData;
	// For each completed operation
	for (pb = fileData->completedPBsHead; pb; pb = pbNext) {
		pbNext = (ParmBlkPtr)pb->ioParam.qLink;
		OSErr oe = pb->ioParam.ioResult;
		// Check which type of operation this was

		// Get the data

		free(pb);
	}
	fileData->completedPBsHead = NULL;
	fileData->completedPBsTail = NULL;
	return false;

	// When open or read completed and not eof, do another read
	// FileStreamRead(s, fileData)
	// TODO: try using continuous mode (set bit 7 of ioPosMode)
}

// IO completion function executed in interrupt
void FileStreamComplete(ParmBlkPtr pb)
{
	// TODO: find a safe place to store the pointer to fileData
	FileData *fileData = (FileData *)pb->ioParam.ioMisc;
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
