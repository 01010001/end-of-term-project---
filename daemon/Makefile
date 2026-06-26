CC = gcc
CFLAGS = -Wall -Wextra -O2
LDFLAGS = -lz

TARGET = vcfsd
SRCS = vcfsd.c

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
