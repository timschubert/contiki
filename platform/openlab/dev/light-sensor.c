#include "contiki.h"
#include "lib/sensors.h"
#include "dev/light-sensor.h"
#include "periph/isl29020.h"

const struct sensors_sensor light_sensor;

static struct {
  isl29020_light_t light;
  isl29020_resolution_t resolution;
  isl29020_range_t range;

  int active;
} conf = {0};

static int value(int type)
{
  float value = isl29020_read_sample();
  return (int)(value * LIGHT_SENSOR_VALUE_SCALE);
}

/*---------------------------------------------------------------------------*/

static int status(int type)
{
  return conf.active;
}

/*---------------------------------------------------------------------------*/

static int configure(int type, int c)
{
  switch (type) {
    case SENSORS_HW_INIT:
      // default values to 0 are OK
      isl29020_prepare(conf.light, conf.resolution, conf.range);
      break;
    case SENSORS_ACTIVE:
      conf.active = c;
      if (conf.active)
        isl29020_sample_continuous();
      else
        isl29020_powerdown();
      break;
    case SENSORS_READY:
      return conf.active;  // return value
      break;

    // Configuration
    case LIGHT_SENSOR_SOURCE:
      conf.light = c;
      isl29020_prepare(conf.light, conf.resolution, conf.range);
      break;
    case LIGHT_SENSOR_RESOLUTION:
      conf.resolution = c;
      isl29020_prepare(conf.light, conf.resolution, conf.range);
      break;
    case LIGHT_SENSOR_RANGE:
      conf.range = c;
      isl29020_prepare(conf.light, conf.resolution, conf.range);
      break;

    default:
      return 1;  // error
  }
  return 0;
}

/*---------------------------------------------------------------------------*/

SENSORS_SENSOR(light_sensor, "Light", value, configure, status);
