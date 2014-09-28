#include <Events.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "stream.h"
#include "filestream.h"
#include "stdoutconsume.h"

Stream *stream;

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
			}
			if (key != '\r' && key != '\03') {
				printf("You pressed '%c' (%u)\n", key, key);
			}
			return true;
		}
	}
}

// handle IO operations. return false to quit, true to continue
bool HandleIO()
{
	StreamPoll(stream);
	return true;
}

int main()
{
	char *fileName = "\pUntitled:StreamTest";
	printf("Opening file \"%s\"\n", fileName+1);

	stream = NewStream();
	StdoutConsumeStream(stream);
	ProvideFileStream(stream, fileName, 0);
	StreamOpen(stream);

	while (HandleEvent() && HandleIO());
	return 0;
}
