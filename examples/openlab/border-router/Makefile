ifndef TARGET
TARGET=openlab-fox
endif

all: border-router

CFLAGS+= -DPROJECT_CONF_H=\"project-conf.h\"

PROJECT_SOURCEFILES += slip-bridge.c

# remove openlab colors
# (escape character conflicts with slip)
CFLAGS+= -DNO_DEBUG_COLOR -DPLATFORM_UART_PRINT_BAUDRATE=115200 -DSLIP_ARCH_CONF_BAUDRATE=115200

CONTIKI = ../../..
include $(CONTIKI)/Makefile.include
