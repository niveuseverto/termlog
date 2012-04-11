CFLAGS+=	-I. -Wall -Wbad-function-cast -Wcast-align -Wcast-qual -Wchar-subscripts
CFLAGS+=	-Winline -Wmissing-prototypes -Wnested-externs -Wpointer-arith
CFLAGS+=	-Wredundant-decls -Wshadow -Wstrict-prototypes -Wwrite-strings -g
CFLAGS+=	-DNDEBUG
OBJS=		rdwrlock.c termlog.o fileops.o
CC?=		CC
LIBS=		-pthread -lmd
PROG=		termlog
PREFIX?=	/usr/local

termlog:	$(OBJS)
		$(CC) -o $(PROG) $(OBJS) $(LIBS)

install:
		cp termlog.1 $(PREFIX)/man/man1/
		cp termlog $(PREFIX)/bin

deinstall:
		rm -f $(PREFIX)/bin/termlog
		rm -f $(PREFIX)/man/man1/termlog.1

clean:
		rm -f *.o $(PROG)

all:		termlog

