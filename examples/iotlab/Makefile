ifndef TARGET
TARGET=iotlab-m3
endif

CONTIKI_PROJECT = sensors-collecting
CONTIKI_SOURCEFILES += sensors.c

# all: $(CONTIKI_PROJECT)
#
# HACK to generate elf files as for the moment, only elf is accepted
#
all: $(CONTIKI_PROJECT) $(addsuffix .elf, $(CONTIKI_PROJECT))

%.elf: %.iotlab-m3
	cp $^ $@


CONTIKI = ../..
include $(CONTIKI)/Makefile.include

