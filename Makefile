# Generated by:
#  genmakefile --create-bins --create-module --create-objs -o Makefile -m make -Ilib -d build -R obj *.c *.c -I/opt/libwebsockets/include -I/usr/local/include/quickjs -L/opt/libwebsockets/lib -lwebsockets -L/opt/libressl-3.5.1/lib -lssl -lcrypto -lbrotlidec -lbrotlienc -lbrotlicommon -lz -lquickjs -n net -DJS_SHARED_LIBRARY
CC = gcc
DEFS = -DJS_SHARED_LIBRARY
CPPFLAGS = -Ilib -I../libwebsockets/include -I../quickjs
LDFLAGS = -shared
LIBS = -L../libwebsockets/lib -L../quickjs -lwebsockets -lssl -lcrypto -lbrotlienc -lbrotlidec -lbrotlicommon -lz

ifdef DEBUG
CFLAGS = -g3 -ggdb -O0 -fPIC
DEFS += -DDEBUG_OUTPUT
else
CFLAGS = -g -O2 -fPIC
DEFS += -DNDEBUG
endif

VPATH = lib:src

.PHONY: all
all: build/ net.so

build/:
	mkdir -p build

build/%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $(DEFS) -c -o $@ $<

build/ws.o: ws.c
build/utils.o: utils.c
build/url.o: url.c
build/session.o: session.c
build/ringbuffer.o: ringbuffer.c
build/response.o: response.c
build/request.o: request.c
build/ref.o: ref.c
build/queue.o: queue.c
build/query.o: query.c
build/poll.o: poll.c
build/opaque.o: opaque.c
build/lws-utils.o: lws-utils.c
build/jsutils.o: jsutils.c
build/headers.o: headers.c
build/generator.o: generator.c
build/form-parser.o: form-parser.c
build/deferred.o: deferred.c
build/context.o: context.c
build/closure.o: closure.c
build/callback.o: callback.c
build/buffer.o: buffer.c
build/asynciterator.o: asynciterator.c
build/minnet.o: minnet.c
build/minnet-websocket.o: minnet-websocket.c
build/minnet-url.o: minnet-url.c
build/minnet-server.o: minnet-server.c
build/minnet-server-ws.o: minnet-server-ws.c
build/minnet-server-proxy.o: minnet-server-proxy.c
build/minnet-server-http.o: minnet-server-http.c
build/minnet-ringbuffer.o: minnet-ringbuffer.c
build/minnet-response.o: minnet-response.c
build/minnet-request.o: minnet-request.c
build/minnet-plugin-broker.o: minnet-plugin-broker.c
build/minnet-hash.o: minnet-hash.c
build/minnet-generator.o: minnet-generator.c
build/minnet-form-parser.o: minnet-form-parser.c
build/minnet-fetch.o: minnet-fetch.c
build/minnet-client.o: minnet-client.c
build/minnet-client-http.o: minnet-client-http.c

net.so: build/ws.o build/utils.o build/url.o build/session.o build/ringbuffer.o build/response.o build/request.o build/ref.o build/queue.o build/query.o build/poll.o build/opaque.o build/lws-utils.o build/jsutils.o build/headers.o build/generator.o build/form-parser.o build/deferred.o build/context.o build/closure.o build/callback.o build/buffer.o build/asynciterator.o build/minnet.o build/minnet-websocket.o build/minnet-url.o build/minnet-server.o build/minnet-server-ws.o build/minnet-server-proxy.o build/minnet-server-http.o build/minnet-ringbuffer.o build/minnet-response.o build/minnet-request.o build/minnet-plugin-broker.o build/minnet-hash.o build/minnet-generator.o build/minnet-form-parser.o build/minnet-fetch.o build/minnet-client.o build/minnet-client-http.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)

clean:
	$(RM) build/*.o net.so