CC = gcc
CFLAGS = -Wall -O2
LIBS = -lhidapi-hidraw

TARGET = cid-listener
SRC = src/main.c src/demux.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LIBS)

install: $(TARGET)
	install -m 755 $(TARGET) /usr/local/bin/$(TARGET)

clean:
	rm -f $(TARGET)
