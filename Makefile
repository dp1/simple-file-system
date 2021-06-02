CCOPTS= -Wall -g -std=gnu99 -Wstrict-prototypes -Iinclude/
CC=gcc
AR=ar

HEADERS = $(wildcard include/*.h)
SRCS = $(wildcard src/*.c)
OBJS = $(patsubst %.c,%.o,$(SRCS))
TESTSRCS = $(wildcard tests/*.c)
TESTS = $(patsubst %.c,%,$(TESTSRCS))

.phony: clean all


all: $(OBJS) $(TESTS)

%.o: %.c $(HEADERS)
	$(CC) $(CCOPTS) -c -o $@ $<

%: %.c $(OBJS) $(HEADERS)
	$(CC) $(CCOPTS) -o $@ $< $(OBJS)

clean:
	rm -rf *~  $(TESTS) $(OBJS)
