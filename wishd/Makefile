CPP    := g++ -Wall -g
LIB   := -lwish -lmicrohttpd -lcrypto -lcurl
INC   := -I/usr/include -I../ -I../libwish/ 
LIBINC:= -L../libwish/
C_SRCS:= $(wildcard *.c)
CXSRCS:= $(wildcard *.cpp)
OBJ   := $(patsubst %.c,%.o,$(C_SRCS)) $(patsubst %.cpp,%.o,$(CXSRCS))
DEFS  := -D_REENTRANT -D_THREAD_SAFE

WISHD := wishd

DESTDIR = /
BIN_DIR = /usr/bin

all: wishd

wishd: $(OBJ)
	$(CPP) -o $(WISHD) $(OBJ) $(LIBINC) $(LIB)

%.o : %.c
	$(CPP) -o $@ $(INC) -c $< $(DEFS)

%.o : %.cpp
	$(CPP) -o $@ $(INC) -c $< $(DEFS)

.PHONY: clean
clean:
	/bin/rm -f $(OBJ) $(WISHD)
