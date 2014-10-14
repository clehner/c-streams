#ifndef _FILESTREAM_H
#define _FILESTREAM_H

enum {
	fileStreamErr = -1,
};

void ProvideFileStream(Stream *s, StringPtr fileName, short vRefNum);

#endif

