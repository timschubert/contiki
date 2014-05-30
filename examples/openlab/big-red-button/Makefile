all: red-button

CFLAGS+= -DPROJECT_CONF_H=\"project-conf.h\" -DWITH_UIP6

PROJECT_SOURCEFILES += command_parser.c web_service.c fox_gpio_button.c gpio_button.c
PROJECT_SOURCEFILES += console_echo.c


CONTIKI = ../../..
include $(CONTIKI)/Makefile.include
