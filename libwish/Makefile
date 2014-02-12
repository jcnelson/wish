CPP	:= g++ -Wall -g -fPIC
AR	:= ar
LIBINC	:=
INC	:= -I/usr/include -I.

PACKETS_DIR := packets

LIBWISH_FULL_SO		:= libwish.so.1.0.1
LIBWISH_SO		:= libwish.so.1
LIBWISH			:= libwish.so
LIBWISH_A		:= libwish.a
LIBWISH_H		:= libwish.h

LIB	:= -lpthread -lcurl -lmicrohttpd 
OBJ	:= libwish.o 
DEFS	:= -D_REENTRANT -D_THREAD_SAFE

DESTDIR		:= /
LIBDIR		:= usr/lib
INCDIR		:= usr/include/wish

all: all_packets libwish_static libwish_dynamic
	/bin/ln -s $(LIBWISH_FULL_SO) $(LIBWISH_SO) || true
	/bin/ln -s $(LIBWISH_SO) $(LIBWISH) || true

libwish_dynamic: all_packets $(OBJ) 
	$(CPP) -shared -W1,-soname,$(LIBWISH_SO) -o $(LIBWISH_FULL_SO) $(OBJ) $(PACKETS_DIR)/*.o $(LIB) $(LIBINC)

libwish_static: all_packets $(OBJ) 
	$(AR) rcs $(LIBWISH_A) $(OBJ)

all_packets:
	$(MAKE) -C $(PACKETS_DIR)

%.o: %.c 
	$(CPP) -o $@ $(INC) $(DEFS) -c $<

%.o: %.cpp 
	$(CPP) -o $@ $(INC) $(DEFS) -c $<

.PHONY : install
install:
	/bin/mkdir -p $(DESTDIR)/$(LIBDIR)
	/bin/cp -a $(LIBWISH_FULL_SO) $(DESTDIR)/$(LIBDIR)
	/bin/cp -a $(LIBWISH_A) $(DESTDIR)/$(LIBDIR)
	/bin/cp -a $(LIBWISH_SO) $(DESTDIR)/$(LIBDIR)
	/bin/cp -a $(LIBWISH) $(DESTDIR)/$(LIBDIR)
	/bin/mkdir -p $(DESTDIR)/$(INCDIR)
	/bin/cp -a $(LIBWISH_H) $(DESTDIR)/$(INCDIR)

.PHONY : uninstall
uninstall:
	/bin/rm $(DESTDIR)/$(LIBDIR)/$(LIBWISH_FULL_SO)
	/bin/rm $(DESTDIR)/$(LIBDIR)/$(LIBWISH_SO)
	/bin/rm $(DESTDIR)/$(LIBDIR)/$(LIBWISH)
	/bin/rm $(DESTDIR)/$(LIBDIR)/$(LIBWISH_A)
	/bin/rm -rf $(DESTDIR)/$(INCDIR)

.PHONY : clean
clean:
	/bin/rm -f $(LIBWISH_A) $(LIBWISH_FULL_SO) $(LIBWISH_SO) $(LIBWISH) $(OBJ)
	$(MAKE) -C $(PACKETS_DIR) clean
