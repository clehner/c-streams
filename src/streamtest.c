#include <Events.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "stream.h"

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
	return true;
}

int main()
{
	printf("Hello.\n");
	while (HandleEvent() && HandleIO());
	return 0;
}
