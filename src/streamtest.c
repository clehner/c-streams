#include <Events.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "stream.h"
#include "filestream.h"
#include "stdoutconsume.h"

char *fileName = "\pLauncher:page.html";

void OpenAFile()
{
	printf("Opening file \"%s\"\n", fileName+1);

	Stream *stream = NewStream();
	StdoutConsumeStream(stream);
	ProvideFileStream(stream, fileName, 0);
	StreamOpen(stream);
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
			} else if (key == 'o') {
				OpenAFile();
			} else if (key != '\r' && key != '\03') {
				printf("You pressed '%c' (%u)\n", key, key);
			}
			return true;
		}
	}
}

int main()
{
	OpenAFile();
	while (HandleEvent()) {
		PollStreams();
	}
	return 0;
}
