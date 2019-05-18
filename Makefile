PROG = uksmd
OBJS = uksmd.o
PREFIX ?= /usr/local
CFLAGS = -O3 -Wall -Wextra -pedantic -pipe -fstack-protector-strong -fno-plt
LDFLAGS = -lprocps

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
