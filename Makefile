INCLUDE_DIRS =
LIB_DIRS =
CC=gcc

CDEFS=

# -O2        : better bthan O0 timing  wise 
# -pthread   : correct compile and linking thread flags
CFLAGS= -O2 -g -Wall -Wextra -pthread $(INCLUDE_DIRS) $(CDEFS)


LIBS= -lrt

HFILES=
CFILES= lab1.c

SRCS= ${HFILES} ${CFILES}
OBJS= ${CFILES:.c=.o}

all: lab1

clean:
	-rm -f *.o *.d
	-rm -f lab1

lab1: lab1.o
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $@.o $(LIBS)

depend:

.c.o:
	$(CC) $(CFLAGS) -c $<
