#include <stdio.h>
#include "stdoutconsume.h"
#include "stream.h"

void StdoutConsumeOpen(void *consumerData);
void StdoutConsumeData(void *consumerData, char *data, short len);
void StdoutConsumeError(void *consumerData, short err);
void StdoutConsumeClose(void *consumerData);
void StdoutConsumeEnd(void *consumerData);

StreamConsumer consumer = {
	.on_open = StdoutConsumeOpen,
	.on_data = StdoutConsumeData,
	.on_error = StdoutConsumeError,
	.on_close = StdoutConsumeClose,
	.on_end = StdoutConsumeEnd,
};

void StdoutConsumeStream(Stream *stream)
{
	StreamConsume(stream, &consumer, 0);
}

void StdoutConsumeOpen(void *consumerData)
{
	// stdout is already open
}

void StdoutConsumeData(void *consumerData, char *data, short len)
{
	// print the data to stdout
	fwrite(data, len, 1, stdout);
	putchar('\n');
}

void StdoutConsumeError(void *consumerData, short err)
{
	// write error to stderr
	fprintf(stderr, "There was an error: %hd\n", err);
}

void StdoutConsumeClose(void *consumerData)
{
	// don't need to close stdout
}

void StdoutConsumeEnd(void *consumerData)
{
}
