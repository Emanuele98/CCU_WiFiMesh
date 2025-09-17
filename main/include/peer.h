#ifndef PEER_H
#define PEER_H

#include <stdint.h>
#include <string.h>
#include <sys/queue.h>
#include <sys/time.h>
#include "esp_log.h"

#define NUMBER_TX 4
#define NUMBER_RX 4
#define RSSI_LIMIT -80

typedef enum 
{
    PAD,
    SCOOTER
} peer_type;

typedef enum {
    PAD_DISCONNECTED,       //when the pad is not connected
    PAD_CONNECTED,          //when the pad is connected 
    PAD_LOW_POWER,          //when the pad is on low power mode
    PAD_FULL_POWER,         //when the pad is on full power mode
    PAD_FULLY_CHARGED,      //when the pad is off but a fully charged scooter is still present on it
    PAD_ALERT               //when the pad sent an alert (overcurrent, overvoltage, overtemperature, FOD)
} pad_status;

typedef enum {
    SCOOTER_DISCONNECTED,   //when the scooter is not connected
    SCOOTER_CONNECTED,      //when the scooter is connected but not localized yet
    SCOOTER_CHARGING,       //when the position is found
    SCOOTER_MISALIGNED,     //when the scooter is misaligned
    SCOOTER_FULLY_CHARGED,  //when the scooter is still on the pad but fully charged
    SCOOTER_ALERT           //when the scooter sent an alert (overcurrent, overvoltage, overtemperature)
} scooter_status;

typedef enum {
    LED_OFF,
    LED_CHARGING,
    LED_MISALIGNED,
    LED_FULLY_CHARGED,
    LED_ALERT,
} led_command_type;

/** @brief Dynamic characteristic structure. This contains elements necessary for dynamic payload. */
typedef struct
{
    float             voltage;            /**< Voltage value from I2C (4 bytes). */
    float             current;            /**< Current Irect value from I2C (4 bytes). */
    float             temp1;              /**< Temperature value from I2C (4 bytes). */
    float             temp2;              /**< Temperature value from I2C (4 bytes). */
    float             rx_power;           /* Calculate received power if peer is CRU */
    float             tx_power;           /* Calculate transmitted power if peer is CTU */
    time_t            dyn_time;           /** Time */
} wpt_dynamic_payload_t;

/**@brief Alert characteristic structure. This contains elements necessary for alert payload. */
/* The union structure allows to check only 'internal', as it gets positive as soon as at one field of the struct is 1 */
typedef struct
{
    bool           overtemperature;    
    bool           overcurrent;        
    bool           overvoltage;        
    bool           F;                      /* FOD for TX, FULLY CHARGED for RX */
} wpt_alert_payload_t;

struct peer 
{
    SLIST_ENTRY(peer) next;

    uint8_t id;
    peer_type type;
    uint8_t mac[6];

    /* STATUS ON/OFF */
    bool full_power;

    /* LOCALIZATION STATUS ON/OFF */
    bool low_power;

    /* LED STATUS OF THE PAD */
    led_command_type led_command;

    /* POSITION OF THE PAD UPON WHICH THE PEER IS PLACED */
    int8_t position;

    /** Peripheral payloads. */
    wpt_dynamic_payload_t dyn_payload;
    wpt_alert_payload_t  alert_payload; //not using it here, prob remove it to save memory resources
};




/**
 * @brief Add a new peer to the peer list
 * 
 * @param id ID of the new peer
 * @param mac MAC address of the new peer
 * @return uint8_t Returns 0 on success, 1 if unable to add (list full or duplicate ID)
 */
uint8_t peer_add(uint8_t id, uint8_t *mac);

/**
 * @brief Initialize the peer management system
 * 
 * @param max_peers Maximum number of peers to manage
 */
void peer_init(uint8_t max_peers);

/**
 * @brief Find a peer by its ID
 * 
 * @param id ID of the peer to find
 * @return struct peer* 
 */
struct peer * peer_find_by_id(uint8_t id);

/**
 * @brief Find a peer by its MAC address
 * 
 * @param mac MAC address of the peer to find
 * @return struct peer* 
 */
struct peer * peer_find_by_mac(uint8_t *mac);

/**
 * @brief Find a scooter by its position
*/
struct peer * peer_find_by_position(int8_t position);

/**
 * @brief Delete a peer from the peer list
 * 
 * @param id ID of the peer to delete
 */
void peer_delete(uint8_t id);

/**
 * @brief Delete all peers from the peer list
 * 
 */
void delete_all_peers(void);


#endif /* PEER_H */