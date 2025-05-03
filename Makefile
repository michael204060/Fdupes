CC = gcc
CFLAGS = -Wall -Wextra
LIBS = -lssl -lcrypto -lmagic
TARGET = mimefdupes

all: $(TARGET)

$(TARGET): mimefdupes.c
	$(CC) $(CFLAGS) $< -o $@ $(LIBS)

clean:
	rm -f $(TARGET)

.PHONY: all clean