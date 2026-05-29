CC = gcc
CFLAGS = `pkg-config --cflags gtk+-3.0` -Wall -Wextra -O2
LDFLAGS = `pkg-config --libs gtk+-3.0`

TARGET = vcfs-gui
SRCS = main.c

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
