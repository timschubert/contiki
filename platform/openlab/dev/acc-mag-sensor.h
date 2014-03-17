#ifndef ACC_MAG_SENSOR_H
#define ACC_MAG_SENSOR_H

#include "lib/sensors.h"
#include "lsm303dlhc.h"

/** Accelerometer and magnetometer available values */
enum {
  ACC_MAG_SENSOR_X = 0,
  ACC_MAG_SENSOR_Y = 1,
  ACC_MAG_SENSOR_Z = 2,
  TEMP_SENSOR      = 3,
};

enum {
  // check sensor temp values LOOKS STRANGE
  TEMP_SENSOR_VALUE_SCALE = 64,  // drivers say 256
};


/** Configure types */
enum {
  ACC_MAG_SENSOR_DATARATE = 0,
  ACC_MAG_SENSOR_SCALE = 1,
  ACC_MAG_SENSOR_MODE = 2,

};


extern const struct sensors_sensor acc_sensor;
extern const struct sensors_sensor mag_temp_sensor;

#endif /* ACC_MAG_SENSOR_H */
