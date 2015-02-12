#include "lib/sensors.h"


/** Sensors **/
const struct sensors_sensor *sensors[] = {
    0
};

unsigned char sensors_flags[(sizeof(sensors) / sizeof(struct sensors_sensor *))];
