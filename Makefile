CCOPTS= -Wall -g -std=gnu99 -Wstrict-prototypes -Iinclude/
CC=gcc
AR=ar


BINS= simplefs_test

HEADERS = $(wildcard include/*.h)
SRCS = $(wildcard src/*.c)
OBJS = $(patsubst %.c,%.o,$(SRCS))

.phony: clean all


all: $(OBJS) $(BINS)

%.o: %.c $(HEADERS)
	$(CC) $(CCOPTS) -c -o $@ $<

simplefs_test: simplefs_test.c $(OBJS) $(HEADERS)
	$(CC) $(CCOPTS)  -o $@ $^ $(OBJS)

clean:
	rm -rf *~  $(BINS) $(SRCS)
