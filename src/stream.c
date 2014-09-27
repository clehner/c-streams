#include <stdlib.h>
#include <stdbool.h>
#include "stream.h"

// stream object: a connection between a consumer and provider of data
struct Stream {
	StreamConsumer *consumer;
	void *consumerData;

	StreamProvider *provider;
	void *providerData;
};

// create a new stream object
Stream *NewStream()
{
	Stream *s = malloc(sizeof(Stream));
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
void StreamWrite(Stream *stream, char *data, short len)
{
	stream->provider->write(stream, stream->providerData, data, len);
}

// deliver new data from the stream to the provider, if it has data
void StreamPoll(Stream *stream)
{
	char *data;
	short len;
	// get data from the provider
	while (stream->provider->poll(stream, stream->providerData, &data, &len)) {
		// deliver the result to the consumer
		if (len < 0) {
			// error
			stream->consumer->on_error(stream->consumerData, len);
		} else if (len == 0) {
			// closed
			stream->consumer->on_close(stream->consumerData);
		} else {
			// data
			stream->consumer->on_data(stream->consumerData, data, len);
		}
	}
}
