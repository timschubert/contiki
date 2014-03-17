#ifndef ACC_MAG_SENSOR_H
#define ACC_MAG_SENSOR_H

#include "lib/sensors.h"
#include "lsm303dlhc.h"

/** Accelerometer available values */
enum {
  ACCELEROMETER_SENSOR_X = 0,
  ACCELEROMETER_SENSOR_Y = 1,
  ACCELEROMETER_SENSOR_Z = 2,
};


extern const struct sensors_sensor acc_sensor;
extern const struct sensors_sensor mag_sensor;

#endif /* ACC_MAG_SENSOR_H */
