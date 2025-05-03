CC = gcc
CFLAGS = -Wall -Wextra -Werror -pedantic -std=gnu18
LOGIN = culbert
SUBMITPATH = ~cs537-1/handin/$(LOGIN)/


.PHONY: all

all: wsh wsh-dbg

wsh: wsh.c wsh.h
	$(CC) $(CFLAGS) -O2 $< -o $@

wsh-dbg: wsh.c wsh.h
	$(CC) $(CFLAGS) -Og -ggdb $< -o $@

clean: 
	rm -f wsh wsh-dbg

submit: clean
	cp -r ../../p3 $(SUBMITPATH)

test:
