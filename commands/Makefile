CPP   := g++ -Wall -g
LIB   := -lwish
INC   := -I/usr/include -I../ -I../libwish/ 
LIBINC:= -L../libwish/
C_SRCS:= $(wildcard *.c)
CXSRCS:= $(wildcard *.cpp)
OBJ   := $(patsubst %.c,%.o,$(C_SRCS)) $(patsubst %.cpp,%.o,$(CXSRCS))
DEFS  := -D_REENTRANT -D_THREAD_SAFE
BIN	:= $(patsubst %.o,%,$(OBJ))

DESTDIR = /usr/local/bin

all: $(OBJ)

install: $(OBJ)
	/bin/cp -a $(BIN) $(DESTDIR)/

%.o : %.c
	$(CPP) -o $(patsubst %.o,%,$@) $(INC) $< $(DEFS) $(LIBINC) $(LIB)

%.o : %.cpp
	$(CPP) -o $(patsubst $.o,%,$@) $(INC) $< $(DEFS) $(LIBINC) $(LIB)

.PHONY: clean
clean:
	/bin/rm -f $(OBJ) $(BIN)
