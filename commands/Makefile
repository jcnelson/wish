CPP   := g++ -Wall -g
LIB   := -lwish
INC   := -I/usr/include -I../ -I../libwish/ 
LIBINC:= -L../libwish/
C_SRCS:= $(wildcard *.c)
CXSRCS:= $(wildcard *.cpp)
OBJ   := $(patsubst %.c,%.o,$(C_SRCS)) $(patsubst %.cpp,%.o,$(CXSRCS))
DEFS  := -D_REENTRANT -D_THREAD_SAFE

DESTDIR = /
BIN_DIR = /usr/bin

all: $(OBJ)

%.o : %.c
	$(CPP) -o $(patsubst %.o,%,$@) $(INC) $< $(DEFS) $(LIBINC) $(LIB)

%.o : %.cpp
	$(CPP) -o $(patsubst $.o,%,$@) $(INC) $< $(DEFS) $(LIBINC) $(LIB)

.PHONY: clean
clean:
	/bin/rm -f $(OBJ) $(patsubst %.o,%,$(OBJ))
