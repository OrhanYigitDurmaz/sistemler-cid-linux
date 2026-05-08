CC = gcc
CFLAGS = -Wall -O2 $(shell pkg-config --cflags spandsp)
LIBS = -lhidapi-hidraw $(shell pkg-config --libs spandsp) -lm

TARGET = cid-listener
SRC = src/main.c src/demux.c src/fsk.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LIBS)

install: $(TARGET)
	install -m 755 $(TARGET) /usr/local/bin/$(TARGET)

clean:
	rm -f $(TARGET)
