CCOPTS= -Wall -g -std=gnu99 -Wstrict-prototypes -Iinclude/
CC=gcc
AR=ar

HEADERS = $(wildcard include/*.h)
SRCS = $(wildcard src/*.c)
OBJS = $(patsubst %.c,%.o,$(SRCS))
TESTSRCS = $(wildcard tests/*.c)
TESTS = $(patsubst %.c,%,$(TESTSRCS))
SHELLSRCS = $(wildcard shell/*.c)

.phony: clean all


all: $(OBJS) $(TESTS) shell/shell

%.o: %.c $(HEADERS)
	$(CC) $(CCOPTS) -c -o $@ $<

%: %.c $(OBJS) $(HEADERS)
	$(CC) $(CCOPTS) -o $@ $< $(OBJS)

shell/shell: $(SHELLSRCS) $(OBJS) $(HEADERS)
	$(CC) $(CCOPTS) -o $@ $(SHELLSRCS) $(OBJS)

clean:
	rm -rf *~  $(TESTS) $(OBJS) shell/shell
