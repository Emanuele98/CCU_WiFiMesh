#ifndef _WIFIMESH_H_
#define _WIFIMESH_H_

#include "util.h"
#include "peer.h"

#define ESPNOW_QUEUE_MAXDELAY         10000 //10 seconds

#define MAX_COMMS_ERROR         10

#define ESPNOW_QUEUE_SIZE       10
#define BROADCAST_TIMEGAP       pdMS_TO_TICKS(1000)

typedef enum {
    DATA_BROADCAST,
    DATA_LOCALIZATION,
    DATA_ALERT,
    DATA_DYNAMIC,
    DATA_CONTROL
} message_type;

typedef enum {
    LOCALIZATION_START,
    LOCALIZATION_CHECK,
    LOCALIZATION_STOP,
} localization_message_t;

/* ESP NOW PAYLOAD */
typedef struct { 
    uint8_t id;                           //Peer unit ID.
    uint8_t type;                         //Broadcast/Unicast ESPNOW data.
    uint16_t crc;                         //CRC16 value of ESPNOW data.
    float field_1;                        
    float field_2;                        
    float field_3;                        
    float field_4;                        
} __attribute__((packed)) espnow_data_t;

typedef enum {
    ID_ESPNOW_SEND_CB,
    ID_ESPNOW_RECV_CB,
} espnow_event_id_t;

typedef struct {
    uint8_t mac_addr[ESP_NOW_ETH_ALEN];
    esp_now_send_status_t status;
} espnow_event_send_cb_t;

typedef struct {
    uint8_t mac_addr[ESP_NOW_ETH_ALEN];
    uint8_t *data;
    int data_len;
} espnow_event_recv_cb_t;

typedef union {
    espnow_event_send_cb_t send_cb;
    espnow_event_recv_cb_t recv_cb;
} espnow_event_info_t;

/* When ESPNOW sending or receiving callback function is called, post event to ESPNOW task. */
typedef struct {
    espnow_event_id_t id;
    espnow_event_info_t info;
} espnow_event_t;

/**
 * @brief Initialize the Wi-Fi mesh network.
 * 
 */
void wifi_mesh_init();

#endif /* _WIFIMESH_H_ */