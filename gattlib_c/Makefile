CC = gcc
CFLAGS = -Wall -g $(shell pkg-config --cflags gattlib)
LDFLAGS = $(shell pkg-config --libs gattlib) -lpthread

TARGET = gattlib_cccd_demo
SRC = gattlib_cccd_demo.c

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: clean
