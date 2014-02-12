CPP    := g++ -Wall -g
LIB   := -lwish 
INC   := -I/usr/include -I../ -I../libwish/
LIBINC:= -L../libwish/ 
C_SRCS:= $(wildcard *.c)
CXSRCS:= $(wildcard *.cpp)
OBJ   := $(patsubst %.c,%.o,$(C_SRCS)) $(patsubst %.cpp,%.o,$(CXSRCS))
DEFS  := -D_REENTRANT -D_THREAD_SAFE

WISH := wlistend

DESTDIR = /
BIN_DIR = /usr/bin

all: wish

wish: $(OBJ) ../libwish/libwish.a
	$(CPP) -o $(WISH) $(OBJ) $(LIBINC) $(LIB) 

%.o : %.c
	$(CPP) -o $@ $(INC) -c $< $(DEFS)

%.o : %.cpp
	$(CPP) -o $@ $(INC) -c $< $(DEFS)

.PHONY: clean
clean:
	/bin/rm -f $(OBJ) $(WISH)
