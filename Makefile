CC = clang
CFLAGS = -Wall -fPIC -shared -std = gnu17
LIBS = -lwebsockets -lcurl -L/usr/lib/quickjs/ -lquickjs 
DEFS = -DJS_SHARED_LIBRARY

all:
	$(CC) $(CFLAGS) $(DEFS) minnet.c -o minnet.so $(LIBS) 
