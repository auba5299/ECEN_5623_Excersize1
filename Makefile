INCLUDE_DIRS =
LIB_DIRS =
CC = gcc

CDEFS =
CFLAGS = -O3 $(INCLUDE_DIRS) $(CDEFS)
LIBS =

HFILES = $(wildcard *.h)
CFILES = $(wildcard *.c)

EXES = $(CFILES:.c=)

all: $(EXES)

clean:
	-rm -f *.o *.d
	-rm -f $(EXES)

distclean:
	-rm -f *.o *.d
	-rm -f $(EXES)

# Force .c → .o before linking
%: %.c
	$(CC) $(CFLAGS) -c $< -o $@.o
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $@.o -lpthread

depend:

.c.o:
	$(CC) $(CFLAGS) -c $<
