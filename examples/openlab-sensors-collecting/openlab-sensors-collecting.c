#include "contiki.h"
#include <stdio.h>
#include "dev/light-sensor.h"
#include "dev/acc-mag-sensor.h"

PROCESS(sensor_collection, "Sensors collection");
AUTOSTART_PROCESSES(&sensor_collection);

/*
 * Light sensor
 */
static void config_light()
{
  light_sensor.configure(LIGHT_SENSOR_SOURCE, ISL29020_LIGHT__AMBIENT);
  light_sensor.configure(LIGHT_SENSOR_RESOLUTION, ISL29020_RESOLUTION__16bit);
  light_sensor.configure(LIGHT_SENSOR_RANGE, ISL29020_RANGE__1000lux);
  SENSORS_ACTIVATE(light_sensor);
}
static void process_light()
{
  int light_val = light_sensor.value(0);
  float light = ((float)light_val) / LIGHT_SENSOR_VALUE_SCALE;
  printf("light_sensor: %f lux\n", light);
}


/*
 * Accelerometer / magnetometer
 */
static void config_acc()
{
  SENSORS_ACTIVATE(acc_sensor);
}

static void process_acc()
{
  int xyz[3];
  static unsigned count = 0;
  if ((++count % 10000) == 0) {
    xyz[0] = acc_sensor.value(ACCELEROMETER_SENSOR_X);
    xyz[1] = acc_sensor.value(ACCELEROMETER_SENSOR_Y);
    xyz[2] = acc_sensor.value(ACCELEROMETER_SENSOR_Z);

    printf("Got %d accelerometer values\n", count);
    printf("x %d y %d z %d\n", xyz[0], xyz[1], xyz[2]);
  }
}




/*---------------------------------------------------------------------------*/
PROCESS_THREAD(sensor_collection, ev, data)
{
  PROCESS_BEGIN();
  static struct etimer timer;

  config_light();
  config_acc();

  etimer_set(&timer, CLOCK_SECOND);

  while(1) {
    PROCESS_WAIT_EVENT();
    if (ev == PROCESS_EVENT_TIMER) {
      process_light();

      etimer_reset(&timer);
    } else if (ev == sensors_event && data == &acc_sensor) {
      process_acc();
    }
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
