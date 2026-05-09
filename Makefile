CC ?= gcc
CFLAGS = -Wall -O2 -I/usr/include/spandsp
LIBS = -lhidapi-hidraw -lspandsp -lm

TARGET = cidv6d
SRC = src/main.c src/demux.c src/fsk.c

PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LIBS)

install: $(TARGET)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all install clean
