#ifndef UNIT_ID_H
#define UNIT_ID_H

//* Define TX or RX unit (exclusive)
#define CONFIG_TX_UNIT      1
//#define CONFIG_RX_UNIT    1

//* ID of the unit (make sure it's unique in the network)
#define CONFIG_UNIT_ID      1 


// todo later: Unit role automatically detected (I2C scan?)
// todo later: unit ID saved into NVS - can be changed manually

#endif /* UNIT_ID_H */