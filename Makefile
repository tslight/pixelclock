CC	 ?= cc
CFLAGS	 ?= -O2 -pedantic -std=c11
CFLAGS	 += -Wall -Wconversion -Wextra -Wshadow -Wunused
CFLAGS	 += -Wmissing-prototypes -Wstrict-prototypes
CFLAGS	 += -Wuninitialized -Wimplicit-fallthrough

LIBS      = x11 xft freetype2
INCLUDES != pkg-config --cflags $(LIBS)
LDFLAGS	 != pkg-config --libs $(LIBS)

PREFIX	 = /usr/local
BINDIR	 = $(DESTDIR)$(PREFIX)/bin

PROG	 = pixelclock
OBJS	 = pixelclock.o

all: $(PROG)

$(PROG): $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $@

$(OBJS): *.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

install: all
	install -s $(PROG) $(BINDIR)

clean:
	rm -f $(PROG) $(OBJS)

.PHONY: all install clean
