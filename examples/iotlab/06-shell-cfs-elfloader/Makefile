CONTIKI_PROJECT = console_server
CONTIKI_SOURCEFILES += console_server.c myencode.c

ELF_LOADER=1

CONSOLE_CLIENT_SRCS = client.c myencode.c

#CFLAGS += -DWITH_UIP6

all: $(CONTIKI_PROJECT) client

client: $(CONSOLE_CLIENT_SRCS)
	gcc -std=gnu99 $(CONSOLE_CLIENT_SRCS) -o client -lpthread

distclean: clean-client
clean-client:
	rm -f client

CONTIKI = ../../..
include $(CONTIKI)/Makefile.include

ifeq (iotlab-a8-m3,$(TARGET))
# No flash on iotlab-a8-m3
$(error Unsuported target iotlab-a8-m3 for this firmware)
endif
