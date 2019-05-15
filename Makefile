PROG = uksmd
OBJS = uksmd.o
PREFIX ?= /usr/local
CFLAGS ?= -O0 -Wall -Wextra -pedantic -ggdb
LDFLAGS ?= -lprocps

all: build

build: $(PROG)

$(PROG): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) $(LDFLAGS) -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $<

install:
	install -Dm0755 $(PROG) $(DESTDIR)$(PREFIX)/bin/$(PROG)

clean:
	rm -f $(PROG) $(OBJS)

.PHONY: all install clean
