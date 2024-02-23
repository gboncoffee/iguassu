CC = tcc
CFLAGS = -Wall -g
INCS = -I/usr/X11R6/include -I/usr/include/freetype2
LIBS = -L/usr/X11R6/lib
CLIBS = -lfontconfig -lXft -lX11 -lX11-xcb -lxcb -lxcb-res

all: iguassu

iguassu: iguassu.c drw.c util.c config.h
	$(CC) $(CFLAGS) $(INCS) $(LIBS) -o $@ iguassu.c drw.c util.c $(CLIBS)

clean:
	rm iguassu
