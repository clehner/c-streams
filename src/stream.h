#ifndef _STREAM_H
#define _STREAM_H

#include <stdbool.h>

typedef struct Stream Stream;
typedef struct StreamConsumer StreamConsumer;
typedef struct StreamProvider StreamProvider;

// callbacks for consuming a stream
struct StreamConsumer {
	void (*on_open)(void *consumerData);
	void (*on_data)(void *consumerData, char *data, short len);
	void (*on_error)(void *consumerData, short err);
	void (*on_close)(void *consumerData);
	void (*on_end)(void *consumerData);
};

// functions for writing and receiving from a stream
struct StreamProvider {
	void (*open)(Stream *s, void *providerData);
	void (*close)(Stream *s, void *providerData);
	void (*write)(Stream *s, void *providerData, char *data, short len);
	void (*poll)(Stream *s, void *providerData);
};

void PollStreams();

Stream *NewStream();
void StreamConsume(Stream *s, StreamConsumer *consumer, void *consumerData);
void StreamProvide(Stream *s, StreamProvider *provider, void *providerData);
void StreamOpen(Stream *stream);
void StreamClose(Stream *stream);
void StreamWrite(Stream *stream, char *data, short len);

// call by provider
void StreamRead(Stream *stream, char *data, short len);
void StreamErrored(Stream *stream, short error);
void StreamOpened(Stream *stream);
void StreamClosed(Stream *stream);
void StreamEnded(Stream *stream);
void StreamWait(Stream *stream);

#endif
