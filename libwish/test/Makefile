CC    := g++ -Wall -g
LIB   := -lwish
INC   := -I/usr/include -I../
LIBINC:= -L../
SRCS  := $(wildcard *.c)
OBJ   := $(patsubst %.c,%.o,$(SRCS))

all: libwish_server libwish_client

libwish_server: $(OBJ)
	$(CC) -o libwish_server libwish_server.o $(LIB) $(LIBINC)

libwish_client: $(OBJ)
	$(CC) -o libwish_client libwish_client.o $(LIB) $(LIBINC)

%.o: %.c
	$(CC) -o $@ $(INC) $(DEFS) -c $<

%.o: %.cpp
	$(CC) -o $@ $(INC) $(DEFS) -c $<

.PHONY: clean
clean:
	/bin/rm -f $(OBJ) libwish_server libwish_client
