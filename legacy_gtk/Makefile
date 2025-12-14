PKG_CFLAGS = $(shell pkg-config --cflags gtk+-3.0 json-c libcurl)
PKG_LIBS = $(shell pkg-config --libs gtk+-3.0 json-c libcurl) -lsodium

app: main.c
	gcc $(PKG_CFLAGS) -o app main.c $(PKG_LIBS)

clean:
	rm -f app
