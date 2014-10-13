#include <stdio.h>
#include <string.h>
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
	printf("the stream is open!\n");
}

void StdoutConsumeData(void *consumerData, char *data, short len)
{
	// print the data to stdout
	short line_len;
	char *line_end;
	while ((line_end = memchr(data, '\r', len))) {
		*line_end = '\0';
		puts(data);
		line_len = line_end - data + 1;
		data += line_len;
		len -= line_len;
	}
	if (len > 1 || (len == 0 && data[0] != '\0'))
		fwrite(data, len, 1, stdout);
}

void StdoutConsumeError(void *consumerData, short err)
{
	// write error to stderr
	fprintf(stderr, "There was an error: %hd\n", err);
}

void StdoutConsumeClose(void *consumerData)
{
	printf("the stream closed.\n");
}

void StdoutConsumeEnd(void *consumerData)
{
	printf("the stream ended.\n");
}
