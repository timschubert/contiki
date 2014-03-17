#include <string.h>
#include "contiki.h"
#include "lib/assert.h"
#include "lib/sensors.h"
#include "dev/acc-mag-sensor.h"

#include "dev/leds.h"

PROCESS(acc_mag_update, "acc_mag_update");

const struct sensors_sensor acc_sensor;
const struct sensors_sensor mag_sensor;


static struct {
  struct {
    lsm303dlhc_acc_datarate_t datarate;
    lsm303dlhc_acc_scale_t scale;
    int sensitivity;
    lsm303dlhc_acc_update_t update;
    int active;

    int16_t xyz[3];
  } acc;
  struct {
    int active;

    int16_t xyz[3];
  } mag;
} conf = {{0}, {0}};

/* Sensitivity for scales values >> 4 (0-3) */
static const int acc_scale_sens[] = {1, 2, 4, 12};

static void acc_measure_isr(void *arg);

/*---------------------------------------------------------------------------*/
/** Accelerometer value in mg
 * type == axis: X Y Z */
static int acc_value(int type)
{
  int raw = 0;
  switch (type) {
    case (ACCELEROMETER_SENSOR_X):
    case (ACCELEROMETER_SENSOR_Y):
    case (ACCELEROMETER_SENSOR_Z):
      raw = conf.acc.xyz[type];
      break;
    default:
      return raw;  // invalid argument
      break;
  }
  return raw / conf.acc.sensitivity;
}

/*---------------------------------------------------------------------------*/

static int acc_status(int type)
{
  return conf.acc.active;
}

/*---------------------------------------------------------------------------*/

static void acc_start()
{
  lsm303dlhc_acc_config(conf.acc.datarate, conf.acc.scale, conf.acc.update);
  lsm303dlhc_acc_set_drdy_int1(acc_measure_isr, NULL);
  lsm303dlhc_read_acc(conf.acc.xyz);
}

static void acc_stop()
{
  lsm303dlhc_acc_set_drdy_int1(NULL, NULL);
}

static int acc_configure(int type, int c)
{
  switch (type) {
    case SENSORS_HW_INIT:
      process_start(&acc_mag_update, NULL);

      lsm303dlhc_powerdown();
      conf.acc.datarate    = LSM303DLHC_ACC_RATE_1344HZ_N_5376HZ_LP;
      conf.acc.scale       = LSM303DLHC_ACC_SCALE_2G;
      conf.acc.update      = LSM303DLHC_ACC_UPDATE_ON_READ;
      conf.acc.sensitivity = acc_scale_sens[conf.acc.scale >> 4];
      // default config
      break;
    case SENSORS_ACTIVE:
      conf.acc.active = c;
      if (c) {
        acc_start();
      } else {
        acc_stop();
        if (!conf.acc.active && !conf.mag.active)
          lsm303dlhc_powerdown();  // stop if no more sensors activated
      }
      break;
    case SENSORS_READY:
      return conf.acc.active;  // return value
      break;

#if 0
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
#endif

    default:
      return 1;  // error
  }
  return 0;
}

/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/

static void acc_measure_isr(void *arg)
{
  // tried with lsm303dlhc_read_acc_async but it messed up i2c chip
  process_post(&acc_mag_update, PROCESS_EVENT_CONTINUE, NULL);
}

/*
 * Values are updated by a process, to ensure that when reading
 * X, then Y, then Z, in a process, the values are coherent when not yielding
 */

PROCESS_THREAD(acc_mag_update, ev, data)
{
  PROCESS_BEGIN();

  // Sometimes the sensors gets stuck because a value
  // does not get read in accelerometer so no new interrupts happen
  // We add a watchdog to prevent the accelerometer to be stuck forever
  static struct etimer acc_mag_watchdog;
  etimer_set(&acc_mag_watchdog, CLOCK_SECOND);
  etimer_stop(&acc_mag_watchdog);


  int acc_new_val;

  while (1) {
    PROCESS_WAIT_EVENT_UNTIL(\
        ((acc_new_val = lsm303dlhc_acc_get_drdy_int1_pin_value())) ||
        (ev == PROCESS_EVENT_TIMER) \
        );

    if (acc_new_val) {
      while (lsm303dlhc_acc_get_drdy_int1_pin_value())
        lsm303dlhc_read_acc(conf.acc.xyz);
      sensors_changed(&acc_sensor);
    }

    if (ev == PROCESS_EVENT_TIMER) {
      static unsigned watchdog_count = 0;
      printf("\t\tYOUHOU got to the watchdog %u\n", ++watchdog_count);
    }


    etimer_restart(&acc_mag_watchdog);  // reset watchdog
  }
  PROCESS_END();
}


/*---------------------------------------------------------------------------*/

SENSORS_SENSOR(acc_sensor, "Accelerometer", acc_value, acc_configure,
    acc_status);
#if 0
SENSORS_SENSOR(mag_sensor, "Magnetometer", mag_value, mag_configure,
    mag_status);
#endif
