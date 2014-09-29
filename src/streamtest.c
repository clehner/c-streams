#include <Events.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "stream.h"
#include "filestream.h"
#include "tcpstream.h"
#include "stdoutconsume.h"

char *fileName = "\pMacintosh HD:asdf.txt";
//ip_addr host = IP_ADDR(127,0,0,1);
ip_addr host = IP_ADDR(192,168,0,5);
tcp_port port = 9999;

Stream *socket = NULL;

void OpenAFile()
{
	printf("Opening file \"%s\"\n", fileName+1);

	Stream *stream = NewStream();
	StdoutConsumeStream(stream);
	ProvideFileStream(stream, fileName, 0);
	StreamOpen(stream);
}

void OpenASocket()
{
	printf("Opening tcp socket %s:%hu\n", sprint_ip_addr(host), port);

	Stream *stream = NewStream();
	StdoutConsumeStream(stream);
	ProvideTCPActiveStream(stream, host, port);
	StreamOpen(stream);
	socket = stream;
}

// handle the next event. return false to quit, true to continue
bool HandleEvent()
{
	EventRecord e;
	GetNextEvent(everyEvent, &e);

	switch(e.what) {
		case keyDown: {
			char key = e.message & charCodeMask;
			if (key == 'q' && e.modifiers & cmdKey) {
				return false;
			} else if (key == 's') {
				OpenASocket();
			} else if (key == 'o') {
				OpenAFile();
			} else if (key != '\03') {
				if (socket) {
					if (key == '\r') key = '\n';
					StreamWrite(socket, &key, 1);
					printf("You sent '%c' (%u)\n", key, key);
				} else {
					printf("You pressed '%c' (%u)\n", key, key);
				}
			}
			return true;
		}
	}
}

int main()
{
	printf("Press 's' to open a socket, 'o' to open a file\n");
	printf("Press other characters to write them to the socket.\n");
	//OpenASocket();
	//OpenAFile();
	while (HandleEvent()) {
		PollStreams();
	}
	return 0;
}
