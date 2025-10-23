#ifndef _WIFIMESH_H_
#define _WIFIMESH_H_

#include "util.h"
#include "peer.h"
#include "mqtt_client_manager.h"

/* Mesh-LITE*/
#define TO_ROOT_STATIC_MSG_ID               0x100
#define TO_ROOT_STATIC_MSG_ID_RESP          0x101

#define TO_ROOT_DYNAMIC_MSG_ID              0x102
#define TO_ROOT_DYNAMIC_MSG_ID_RESP         0x103

#define TO_ROOT_ALERT_MSG_ID                0x104
#define TO_ROOT_ALERT_MSG_ID_RESP           0x105

#define TO_ROOT_LOCALIZATION_ID             0x106
#define TO_ROOT_LOCALIZATION_ID_RESP        0x107

#define TO_CHILD_CONTROL_MSG_ID             0x108
#define TO_CHILD_CONTROL_MSG_ID_RESP        0x109

/* ESP-NOW*/
#define ESPNOW_QUEUE_MAXDELAY               10000 //10 seconds
#define MAX_COMMS_ERROR                     10
#define ESPNOW_QUEUE_SIZE                   5

/* ESP-NOW message type */
typedef enum {
    DATA_BROADCAST,                      // Contains Localization (RX Voltage)
    DATA_ALERT,                          // Contains Alert from RX
    DATA_DYNAMIC,                        // Dynamic Payload from RX
    DATA_ASK_DYNAMIC,                    // Ask dynamic payload from RX
} espnow_message_type;

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

/* ESP-NOW structs */
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