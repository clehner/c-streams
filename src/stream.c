#include <OSUtils.h>
#include <stdlib.h>
#include <stdbool.h>
#include "stream.h"

// stream object: a connection between a consumer and provider of data
struct Stream {
	QElem q;
	StreamConsumer *consumer;
	void *consumerData;
	StreamProvider *provider;
	void *providerData;
};

QHdr readyStreams = {0};

// create a new stream object
Stream *NewStream()
{
	Stream *s = calloc(1, sizeof(Stream));
	return s;
}

// set the stream consumer
void StreamConsume(Stream *s, StreamConsumer *consumer, void *consumerData)
{
	s->consumer = consumer;
	s->consumerData = consumerData;
}

// set the stream provider
void StreamProvide(Stream *s, StreamProvider *provider, void *providerData)
{
	s->provider = provider;
	s->providerData = providerData;
}

// open the stream
void StreamOpen(Stream *stream)
{
	stream->provider->open(stream, stream->providerData);
}

// close the stream
void StreamClose(Stream *stream)
{
	stream->provider->close(stream, stream->providerData);
}

// write some data to the stream provider
// Consumer should call this
void StreamWrite(Stream *stream, char *data, unsigned short len)
{
	stream->provider->write(stream, stream->providerData, data, len);
}

// Provider should call these:

// deliver new data from the provider to the consumer
void StreamRead(Stream *stream, char *data, short len)
{
	stream->consumer->on_data(stream->consumerData, data, len);
}

// there was an error receiving or sending data
void StreamErrored(Stream *stream, short error)
{
	stream->consumer->on_error(stream->consumerData, error);
}

void StreamOpened(Stream *stream)
{
	stream->consumer->on_open(stream->consumerData);
}

// the resource closed
void StreamClosed(Stream *stream)
{
	stream->consumer->on_close(stream->consumerData);
}

// there is no more data to read
void StreamEnded(Stream *stream)
{
	stream->consumer->on_end(stream->consumerData);
}

// Queue a stream for polling in the event loop.
// May be called in interrupt.
void StreamWait(Stream *stream)
{
	if (stream->q.qData[0]) {
		// already in the queue
		return;
	}
	// add the stream to the queue
	stream->q.qData[0] = 1;
	Enqueue((QElem *)stream, &readyStreams);
}

void PollStreams()
{
	Stream *stream;
	// Handle each ready operation, popping them off the queue
	while (readyStreams.qHead) {
		stream = (Stream *)readyStreams.qHead;
		if (Dequeue(readyStreams.qHead, &readyStreams) != noErr) {
			// race condition: this shouldn't happen
			continue;
		}
		// tell the stream to handle its completed operations
		stream->provider->poll(stream, stream->providerData);
		stream->q.qData[0] = 0;
	}
}
