#ifndef _WIFIMESH_H_
#define _WIFIMESH_H_

#include "util.h"
#include "peer.h"

typedef enum {
    DATA_BROADCAST,
    DATA_LOCALIZATION,
    DATA_ALERT,
    DATA_DYNAMIC,
    DATA_CONTROL
} message_t;

typedef enum {
    LOCALIZATION_START,
    LOCALIZATION_CHECK,
    LOCALIZATION_STOP,
} localization_message_t;

/* Wi-Fi Mesh PAYLOAD */
typedef struct { 
    uint8_t id;                           //Peer unit ID.
    uint8_t type;                         //Type of Wi-Fi Mesh message.
    float field_1;                        
    float field_2;                        
    float field_3;                        
    float field_4;                        
} __attribute__((packed)) wifiMesh_data_t;

/**
 * @brief Initialize the Wi-Fi mesh network.
 * 
 */
void wifi_mesh_init();

#endif /* _WIFIMESH_H_ */