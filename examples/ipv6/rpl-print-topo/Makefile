all: rpl-eval-sink rpl-eval-source
CONTIKI=../../..

PROJECT_SOURCEFILES += rpl-eval-common.c

CFLAGS += -DPROJECT_CONF_H=\"project-conf.h\"

ifdef WITH_COMPOWER
APPS+=powertrace
CFLAGS+= -DCONTIKIMAC_CONF_COMPOWER=1 -DWITH_COMPOWER=1 -DQUEUEBUF_CONF_NUM=4
endif

ifdef PERIOD
CFLAGS+=-DPERIOD=$(PERIOD)
endif

ifeq ($(MAKE_WITH_NON_STORING),1)
CFLAGS += -DWITH_NON_STORING=1
endif

include $(CONTIKI)/Makefile.include
