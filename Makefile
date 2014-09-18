LIBWISH	:= libwish/
WISHD		:= wishd/
CLIENT	:= client/
COMMANDS	:= commands/

all:
	$(MAKE) -C $(LIBWISH)
	$(MAKE) -C $(WISHD)
	$(MAKE) -C $(CLIENT)
	$(MAKE) -C $(COMMANDS)

install:
	$(MAKE) -C $(LIBWISH) install
	$(MAKE) -C $(WISHD) install
	$(MAKE) -C $(CLIENT) install
	$(MAKE) -C $(COMMANDS) install

clean:
	$(MAKE) -C $(LIBWISH) clean
	$(MAKE) -C $(WISHD) clean
	$(MAKE) -C $(CLIENT) clean
	$(MAKE) -C $(COMMANDS) clean
