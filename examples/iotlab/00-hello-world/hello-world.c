#include "contiki.h"
#include "dev/serial-line.h"
#include "dev/uart1.h"
#include <stdio.h>

/*
 * Prints "Hello World !", and echoes whatever arrives on the serial link 
 */

PROCESS(serial_echo, "Serial Echo");
AUTOSTART_PROCESSES(&serial_echo);

/*---------------------------------------------------------------------------*/
static
int input_bytes_handler(unsigned char c)
{
  printf("%c", c);
  return serial_line_input_byte(c); // default contiki behaviour
}

/*---------------------------------------------------------------------------*/
PROCESS_THREAD(serial_echo, ev, data)
{
  PROCESS_BEGIN();
  
  uart1_set_input(input_bytes_handler);
  serial_line_init();

  printf("Hello World !\n");

  while(1) {
    printf("> ");
    PROCESS_YIELD();
    if (ev == serial_line_event_message) {
      printf("%s\n", (char*)data);
    }
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
