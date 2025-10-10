#ifndef PEER_H
#define PEER_H

#include "util.h"

typedef enum {
    TX,        //TX UNIT
    RX         //RX UNIT
} peer_type;

extern peer_type UNIT_ROLE;

typedef enum {
    TX_OFF,                //when the pad is off
    TX_LOCALIZATION,       //when the pad is active for localization (soft duty cycle thresholds) 
    TX_DEPLOY,             //when the pad is active on deploy (hard duty cycle thresholds)
    TX_FULLY_CHARGED,      //when the pad is off but a fully charged scooter is still present on it
    TX_ALERT               //when the pad sent an alert (overcurrent, overvoltage, overtemperature, FOD)
} TX_status;

typedef enum {
    RX_CONNECTED,      //when the scooter is connected but not localized yet
    RX_CHARGING,       //when the position is found
    RX_MISALIGNED,     //when the scooter is misaligned
    RX_FULLY_CHARGED,  //when the scooter is still on the pad but fully charged
    RX_ALERT           //when the scooter sent an alert (overcurrent, overvoltage, overtemperature)
} RX_status;

typedef enum {
    LED_OFF,
    LED_CONNECTED,
    LED_CHARGING,
    LED_MISALIGNED,
    LED_FULLY_CHARGED,
    LED_ALERT,
} led_command_type;

/**
 * @brief Static characteristic structure. This contains elements necessary for static payload.
 * 
 */
typedef struct
{
    uint8_t          id;
    peer_type        type;
    uint8_t          macAddr[ETH_HWADDR_LEN]; /**< MAC Address of peer */
    float            OVERVOLTAGE_limit;     /* Overvoltage alert limit */
    float            OVERCURRENT_limit;     /* Overcurrent alert limit */
    float            OVERTEMPERATURE_limit; /* Overtemperature alert limit */
    bool             FOD;                   /* FOD active */
} mesh_static_payload_t;

/**
 * @brief Dynamic characteristic structure. This contains elements necessary for dynamic payload.
 * 
 */
typedef struct
{
    struct 
    {
        uint8_t           macAddr[ETH_HWADDR_LEN];  /**< MAC Address of TX peer */
        TX_status         tx_status;               /* TX status */
        float             voltage;                  /**< TX Voltage (4 bytes). */
        float             current;                  /**< TX Current Irect (4 bytes). */
        float             temp1;                    /**< TX Temperature (4 bytes). */
        float             temp2;                    /**< TX Temperature (4 bytes). */
    } TX;
    struct 
    {
        uint8_t           macAddr[ETH_HWADDR_LEN];  /**< MAC Address of RX peer */
        RX_status         rx_status;                /* RX status */
        float             voltage;                  /**< RX Voltage (4 bytes). */
        float             current;                  /**< RX Current Irect (4 bytes). */
        float             temp1;                    /**< RX Temperature (4 bytes). */
        float             temp2;                    /**< RX Temperature (4 bytes). */
    } RX;
} mesh_dynamic_payload_t;

/**
 * @brief Alert characteristic structure. This contains elements necessary for alert payload.
 *        The union structure allows to check only all_flags instead of each alert separately
 */
typedef struct
{
    struct
    {
        uint8_t            macAddr[ETH_HWADDR_LEN]; /**< MAC Address of TX peer */          
        union {
            struct {
                uint8_t       overtemperature:1;    /* TX Overtemperature alert */
                uint8_t       overcurrent:1;        /* TX Overcurrent alert     */
                uint8_t       overvoltage:1;        /* TX Overvoltage alert     */
                uint8_t       FOD:1;                /* TX FOD */
            } TX_internal;
            uint8_t TX_all_flags;                   /* TX To check if at least one alert is active */
        }; 
    } TX;
    struct
    {
        uint8_t            macAddr[ETH_HWADDR_LEN]; /**< MAC Address of TX peer */          
        union {
            struct {
                uint8_t       overtemperature:1;    /* RX Overtemperature alert */
                uint8_t       overcurrent:1;        /* RXOvercurrent alert     */
                uint8_t       overvoltage:1;        /* RX Overvoltage alert     */
                uint8_t       FullyCharged:1;       /* RX FULLY CHARGED for RX */
            } RX_internal;
            uint8_t RX_all_flags;                   /* RX To check if at least one alert is active */
        }; 
    } RX;
    
} mesh_alert_payload_t; 

/**
 * @brief Payload for control mesh lite messages
 * 
 */
typedef struct
{
    uint8_t          macAddr[ETH_HWADDR_LEN]; /**< MAC Address of peer */
    uint8_t          command;                  /* Command to be sent to STM32 */
} mesh_control_payload_t;

/**
 * @brief Payload for localization mesh lite messages
 * 
 */
typedef struct
{
    uint8_t          macAddr[ETH_HWADDR_LEN];   /**< MAC Address of peer */
    uint8_t          position;                  /* Position of the RX peer */
} mesh_localization_payload_t;


/**
 * @brief Tuning params structure for TX transitor waveforms
 * 
 */
typedef struct
{
    uint8_t          macAddr[ETH_HWADDR_LEN];   /**< MAC Address of peer */
    float             duty_cycle;
    uint8_t           tuning;
    uint8_t           low_vds_threshold;
    uint8_t           low_vds;
} mesh_tuning_params_t;

/**
 * @brief TX peer structure. This contains elements necessary for TX peer management.
 * 
 */
struct TX_peer 
{
    SLIST_ENTRY(TX_peer) next;

    /* ID of the peer */
    int8_t id;

    /* POSITION OF THE CHARGING PAD - FIXED */
    int8_t position; 

    /* MAC ADDRESS OF THE TX */
    uint8_t MACaddress[6];

    /* STATUS OF THE CHARGING PAD */
    TX_status tx_status;

    /* LED STATUS OF THE CHARGING PAD */
    led_command_type led_command;

    /** Peripheral payloads. */
    mesh_static_payload_t *static_payload;
    mesh_dynamic_payload_t *dynamic_payload;
    mesh_alert_payload_t  *alert_payload;
    mesh_tuning_params_t *tuning_params;
};

/**
 * @brief RX peer structure. This contains elements necessary for RX peer management.
 * 
 */
struct RX_peer 
{
    SLIST_ENTRY(RX_peer) next;

    /* ID of the peer */
    int8_t id;

    /* POSITION OF THE CHARGING PAD UPON WHICH THE RX IS PLACED - VARIABLE */
    int8_t position;

    /* MAC ADDRESS OF THE RX */
    uint8_t MACaddress[6];

    /* STATUS OF THE SCOOTER */
    RX_status RX_status;

    /** Peripheral payloads. */
    mesh_static_payload_t *static_payload;
    mesh_dynamic_payload_t *dynamic_payload;
    mesh_alert_payload_t  *alert_payload;
};

//Self-structures
extern mesh_static_payload_t self_static_payload;
extern mesh_dynamic_payload_t self_dynamic_payload;
extern mesh_alert_payload_t self_alert_payload;
extern mesh_tuning_params_t self_tuning_params;

/**
 * @brief Initialize hardware components
 * 
 */
void init_HW();

/**
 * @brief Check if at least one RX was not localized yet, and at least one TX pad is available
 * 
 * @return true 
 * @return false 
 */
bool atLeastOneRxNeedLocalization();

/**
 * @brief Initialize the peer management system
 * 
 */
void peer_init();

/**
 * @brief Add a new TX peer to the TX_peers list
 * 
 * @param mac MAC address of the new peer
 * @return TX_peer on success, NULL if unable to add (list full or duplicate ID)
 */
struct TX_peer* TX_peer_add(uint8_t *mac);

/**
 * @brief Add a new RX peer to the RX_peers list
 * 
 * @param mac MAC address of the new peer
 * @return RX_peer on success, NULL if unable to add (list full or duplicate ID)
 */
struct RX_peer* RX_peer_add(uint8_t *mac);

/**
 * @brief Find a TX_peer by its MAC address
 * 
 * @param mac MAC address of the peer to find
 * @return struct TX_peer* 
 */
struct TX_peer* TX_peer_find_by_mac(uint8_t *mac);

/**
 * @brief Find a RX_peer by its MAC address
 * 
 * @param mac MAC address of the peer to find
 * @return struct RX_peer* 
 */
struct RX_peer* RX_peer_find_by_mac(uint8_t *mac);

/**
 * @brief Find a TX_peer by its position
 * 
 * @param id ID of the peer to find
 * @return struct TX_peer* 
 */
struct TX_peer* TX_peer_find_by_position(uint8_t position);

/**
 * @brief Find a RX_peer by its position
 * @return struct RX_peer*
*/
struct RX_peer* RX_peer_find_by_position(int8_t position);

/**
 * @brief Delete a peer from the peer list
 * 
 * @param mac MAC address of the peer to delete
 */
void peer_delete(uint8_t *mac);

/**
 * @brief Delete all peers from the peer list
 * 
 */
void delete_all_peers(void);

/**
 * @brief Update to OFF all TX peers available for localization
 * 
 */
void allLocalizationTxPeersOFF();

/**
 * @brief Find peer with given TX position
 * 
 * @param pos TX position (UNIT ID)
 * @return struct RX_peer* return NULL if none
 */
struct RX_peer* findRXpeerWPosition(uint8_t pos);

/**
 * @brief Find the next TX peer for localization
 * @param previousTX_pos Position of the previous TX peer used for localization
 * @return struct TX_peer* Pointer to the next TX peer for localization
 */
struct TX_peer* find_next_TX_for_localization(int8_t previousTX_pos);

/**
 * @brief Check if the dynamic payload has changed compared to the previous one
 * 
 * @param previous Previous dynamic payload to compare with
 * @return true 
 * @return false 
 */
bool dynamic_payload_changes(mesh_dynamic_payload_t *previous);

/**
 * @brief Check if the alert payload has changed compared to the previous one
 * 
 * @param previous Previous alert payload to compare with
 * @return true 
 * @return false 
 */
bool alert_payload_changes(mesh_alert_payload_t *previous);

/**
 * @brief Init payloads self-structures
 * 
 */
void init_payloads();

#endif /* PEER_H */