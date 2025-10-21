#include "wifiMesh.h"

/*******************************************************
 *                Variable Definitions
 *******************************************************/
static const char *TAG = "wifiMesh";

// Network status
static bool is_mesh_connected = false;
static bool is_root_node = false;
static int mesh_level = -1;

// Send semaphore to avoid concurrent access to RF resources
static SemaphoreHandle_t send_semaphore = NULL;
// ESP-NOW messages queue
static QueueHandle_t espnow_queue;
// ESP-NOW payload structure
static espnow_data_t *espnow_data;
// ESP-NOW Retrasmissions variable
static uint8_t comms_fail = 0;
static espnow_message_type last_msg_type;

// Broadcast MAC address
static uint8_t broadcast_mac[ETH_HWADDR_LEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static uint8_t TX_parent_mac[ETH_HWADDR_LEN] = {0};

/*******************************************************
 *                Function Definitions
 *******************************************************/

// process response to static raw message - inside child
static esp_err_t static_to_root_raw_msg_response_process(uint8_t *data, uint32_t len, 
                                     uint8_t **out_data, uint32_t* out_len, 
                                     uint32_t seq) 
{
    //set static payload
    //ESP_LOGW( TAG, "Process static message RESPONSE!");   

    // Process the received data
    if (len != sizeof(mesh_static_payload_t)) {
        ESP_LOGW(TAG, "Received unexpected message size: %d", len);
        printf(" Expected: %d\n", sizeof(mesh_static_payload_t));
        printf("Data: ");
        for (int i = 0; i < len; i++) {
            printf("%02X ", data[i]);
        }
        printf("\n");
        return ESP_FAIL;
    }

    mesh_static_payload_t *received_payload = (mesh_static_payload_t *)data;
    //ESP_LOGW(TAG, "OVERVOLTAGE_limit: %.2f, OVERCURRENT_limit: %.2f, OVERTEMPERATURE_limit: %.2f, FOD: %s",
    //         received_payload->OVERVOLTAGE_limit,
    //         received_payload->OVERCURRENT_limit,
    //         received_payload->OVERTEMPERATURE_limit,
    //         received_payload->FOD ? "true" : "false");

    //* Set Local Alerts
    OVER_CURRENT = received_payload->OVERCURRENT_limit;
    OVER_VOLTAGE = received_payload->OVERVOLTAGE_limit;
    OVER_TEMPERATURE = received_payload->OVERTEMPERATURE_limit;
    FOD = received_payload->FOD;

    return ESP_OK;
}

// Process received static raw messages - inside root
static esp_err_t static_to_root_raw_msg_process(uint8_t *data, uint32_t len, 
                                     uint8_t **out_data, uint32_t* out_len, 
                                     uint32_t seq) 
{
    //ESP_LOGW( TAG, "Process static message");   
    
    // Process the received data
    if (len != sizeof(mesh_static_payload_t)) {
        ESP_LOGW(TAG, "Received unexpected message size: %d", len);
        printf(" Expected: %d\n", sizeof(mesh_static_payload_t));
        printf("Data: ");
        for (int i = 0; i < len; i++) {
            printf("%02X ", data[i]);
        }
        printf("\n");
        return ESP_FAIL;
    }

    mesh_static_payload_t *received_payload = (mesh_static_payload_t *)data;
    ESP_LOGI(TAG, "Received static payload from ID: %d, Type: %d, MAC: "MACSTR, 
             received_payload->id, received_payload->type, MAC2STR(received_payload->macAddr));

    // Set response to set limits (from master to child)
    *out_len = sizeof(mesh_static_payload_t);
    *out_data = malloc(*out_len);

    mesh_static_payload_t my_static_payload;
    memset(&my_static_payload, 0, sizeof(mesh_static_payload_t));

    if (received_payload->type == TX)
    {
        my_static_payload.OVERVOLTAGE_limit = OVERVOLTAGE_TX;
        my_static_payload.OVERCURRENT_limit = OVERCURRENT_TX;
        my_static_payload.OVERTEMPERATURE_limit = OVERTEMPERATURE_TX;
        my_static_payload.FOD = FOD_ACTIVE;
    }
    else
    {
        my_static_payload.OVERVOLTAGE_limit = OVERVOLTAGE_RX;
        my_static_payload.OVERCURRENT_limit = OVERCURRENT_RX;
        my_static_payload.OVERTEMPERATURE_limit = OVERTEMPERATURE_RX;
    }
    memcpy(my_static_payload.macAddr, self_mac, ETH_HWADDR_LEN);
    memcpy(*out_data, (uint8_t*)&my_static_payload, *out_len); // set here alerts

    //* Add peer strucutre
    if (received_payload->type == TX)
    {
        struct TX_peer *p = TX_peer_add(received_payload->macAddr, received_payload->id);
        if (p != NULL)
        {
            *p->static_payload = *received_payload;
            ESP_LOGI(TAG, "TX Peer structure added! ID: %d", p->static_payload->id);
        }
    }
    else if (received_payload->type == RX)
    {
        struct RX_peer *p = RX_peer_add(received_payload->macAddr, received_payload->id); 
        if (p != NULL)
        {
            ESP_LOGI(TAG, "RX Peer structure added! ID: %d", p->id);
        }
    }

    return ESP_OK;
}
// process response to dynamic raw message - inside child
static esp_err_t dynamic_to_root_raw_msg_response_process(uint8_t *data, uint32_t len, 
                                     uint8_t **out_data, uint32_t* out_len, 
                                     uint32_t seq) 
{
    //set static payload
    //ESP_LOGW( TAG, "Process dynamic message RESPONSE!");   

    return ESP_OK;
}

// Process received dynamic raw messages - inside root
static esp_err_t dynamic_to_root_raw_msg_process(uint8_t *data, uint32_t len, 
                                     uint8_t **out_data, uint32_t* out_len, 
                                     uint32_t seq) 
{
    //ESP_LOGW( TAG, "Process dynamic message");   

    // Process the received data
    if (len != sizeof(mesh_dynamic_payload_t)) {
        ESP_LOGW(TAG, "Received unexpected message size: %d", len);
        printf(" Expected: %d\n", sizeof(mesh_static_payload_t));
        printf("Data: ");
        for (int i = 0; i < len; i++) {
            printf("%02X ", data[i]);
        }
        printf("\n");
        return ESP_FAIL;
    }

    mesh_dynamic_payload_t *received_payload = (mesh_dynamic_payload_t *)data;
    //ESP_LOGI(TAG, "Received dynamic payload from MAC: "MACSTR,  MAC2STR(received_payload->TX.macAddr));

    struct TX_peer *p = TX_peer_find_by_mac(received_payload->TX.macAddr);
    if (p != NULL)
    {
        *p->dynamic_payload = *received_payload;
        ESP_LOGI(TAG, "TX Peer ID %d dynamic payload updated", p->id);
        // show data
        
        /*ESP_LOGI(TAG, "TX: Voltage: %.2f V, Current: %.2f A, Temp1: %.2f C, Temp2: %.2f C \n\
                        RX: Voltage: %.2f V, Current: %.2f A, Temp1: %.2f C, Temp2: %.2f C",
                 p->dynamic_payload->TX.voltage, p->dynamic_payload->TX.current, p->dynamic_payload->TX.temp1, p->dynamic_payload->TX.temp2,
                p->dynamic_payload->RX.voltage, p->dynamic_payload->RX.current, p->dynamic_payload->RX.temp1, p->dynamic_payload->RX.temp2);*/
    }

    return ESP_OK;
}

// process response to alert raw message - inside child
static esp_err_t alert_to_root_raw_msg_response_process(uint8_t *data, uint32_t len, 
                                     uint8_t **out_data, uint32_t* out_len, 
                                     uint32_t seq) 
{
    //set static payload
    //ESP_LOGW( TAG, "Process alert message RESPONSE!");   

    return ESP_OK;
}

// Process received alert raw messages - inside root
static esp_err_t alert_to_root_raw_msg_process(uint8_t *data, uint32_t len, 
                                     uint8_t **out_data, uint32_t* out_len, 
                                     uint32_t seq) 
{
    //ESP_LOGW( TAG, "Process alert message");   

    // Process the received data
    if (len != sizeof(mesh_alert_payload_t)) {
        ESP_LOGW(TAG, "Received unexpected message size: %d", len);
        printf(" Expected: %d\n", sizeof(mesh_alert_payload_t));
        printf("Data: ");
        for (int i = 0; i < len; i++) {
            printf("%02X ", data[i]);
        }
        printf("\n");
        return ESP_FAIL;
    }

    mesh_alert_payload_t *received_payload = (mesh_alert_payload_t *)data;
    //ESP_LOGI(TAG, "Received alert payload from MAC: "MACSTR,  MAC2STR(received_payload->TX.macAddr));

    struct TX_peer *p = TX_peer_find_by_mac(received_payload->TX.macAddr);
    if (p != NULL)
    {
        *p->alert_payload = *received_payload;
        ESP_LOGI(TAG, "TX Peer ID %d alert payload received - tx %d rx %d", p->id, p->alert_payload->TX.TX_all_flags, p->alert_payload->RX.RX_all_flags);
        // show data
        //ESP_LOGI(TAG, "TX: OV %d, OC %d, OT %d, FOD %d \n RX: OV: %d, OC %d, OT %d FC %d",
        //    p->alert_payload->TX.TX_internal.overvoltage, p->alert_payload->TX.TX_internal.overcurrent, p->alert_payload->TX.TX_internal.overtemperature, p->alert_payload->TX.TX_internal.FOD,
        //    p->alert_payload->RX.RX_internal.overvoltage, p->alert_payload->RX.RX_internal.overcurrent, p->alert_payload->RX.RX_internal.overtemperature, p->alert_payload->RX.RX_internal.FullyCharged);
    
        //todo handle alert payload (swith off command and reconnect)
    }

    return ESP_OK;
}

// process response to control raw message - inside root
static esp_err_t control_to_child_raw_msg_response_process(uint8_t *data, uint32_t len, 
                                     uint8_t **out_data, uint32_t* out_len, 
                                     uint32_t seq) 
{
    //set static payload
    //ESP_LOGW( TAG, "Process control message RESPONSE!");   

    return ESP_OK;
}

// Process received control raw messages - inside child
static esp_err_t control_to_child_raw_msg_process(uint8_t *data, uint32_t len, 
                                     uint8_t **out_data, uint32_t* out_len, 
                                     uint32_t seq) 
{
    //ESP_LOGW( TAG, "Process control message");   

    if (UNIT_ROLE == RX)
        return ESP_OK; // RX do not process control messages

    // Process the received data
    if (len != sizeof(mesh_control_payload_t)) {
        ESP_LOGW(TAG, "Received unexpected message size: %d", len);
        printf(" Expected: %d\n", sizeof(mesh_control_payload_t));
        printf("Data: ");
        for (int i = 0; i < len; i++) {
            printf("%02X ", data[i]);
        }
        printf("\n");
        return ESP_FAIL;
    }

    mesh_control_payload_t *received_payload = (mesh_control_payload_t *)data;
    //ESP_LOGI(TAG, "Received control payload for MAC: "MACSTR,  MAC2STR(received_payload->macAddr));

    TX_status command = (TX_status)received_payload->command;

    if (command == TX_OFF)
    {
        ESP_LOGI(TAG, "Received command to SWITCH OFF");
        write_STM_command(TX_OFF);
    }
    else if (memcmp(received_payload->macAddr, self_mac, ETH_HWADDR_LEN) == 0)
    {
        //ESP_LOGI(TAG, "Control message is for this unit");
        if (command == TX_DEPLOY)
        {
            ESP_LOGI(TAG, "Received command to SWITCH ON");
            write_STM_command(TX_DEPLOY);
        }
        else if (command == TX_LOCALIZATION)
        {
            ESP_LOGI(TAG, "Received command to SWITCH LOC");
            write_STM_command(TX_LOCALIZATION);
        }
    }

    return ESP_OK;
}

static esp_err_t localization_to_root_raw_msg_process_response(uint8_t *data, uint32_t len, 
                                     uint8_t **out_data, uint32_t* out_len, 
                                     uint32_t seq) 
{
    //set static payload
    //ESP_LOGW( TAG, "Process localization message RESPONSE!");   

    return ESP_OK;
}

static esp_err_t localization_to_root_raw_msg_process(uint8_t *data, uint32_t len, 
                                     uint8_t **out_data, uint32_t* out_len, 
                                     uint32_t seq) 
{
    //ESP_LOGW( TAG, "Process localization message");   

    // Process the received data
    if (len != sizeof(mesh_localization_payload_t)) {
        ESP_LOGW(TAG, "Received unexpected message size: %d", len);
        printf(" Expected: %d\n", sizeof(mesh_localization_payload_t));
        printf("Data: ");
        for (int i = 0; i < len; i++) {
            printf("%02X ", data[i]);
        }
        printf("\n");
                return ESP_FAIL;
    }

    mesh_localization_payload_t *received_payload = (mesh_localization_payload_t *)data;

    struct RX_peer *p = RX_peer_find_by_mac(received_payload->macAddr);
    if (p != NULL)
    {
        p->position = received_payload->position;
        p->RX_status = RX_CHARGING;
        ESP_LOGI(TAG, "RX Peer ID %d localized at position %d", p->id, p->position);
        previousTX_pos = 0;
    }

    return ESP_OK;
}

// Send dynamic message to Root
static void send_dynamic_message_to_root(uint8_t *data, size_t data_len) 
{
    //ESP_LOGW( TAG, "Sending message to Root");    
    
    esp_mesh_lite_msg_config_t config = {
        .raw_msg = {
            .msg_id = TO_ROOT_DYNAMIC_MSG_ID,  // Define your own message ID
            .expect_resp_msg_id = TO_ROOT_DYNAMIC_MSG_ID_RESP,  // Response ID if needed
            .max_retry = 3,
            .retry_interval = 10,
            .data = data,
            .size = data_len,
            .raw_resend = esp_mesh_lite_send_raw_msg_to_root,  // Send raw message to Root
        },
    };
    
    esp_mesh_lite_send_msg(ESP_MESH_LITE_RAW_MSG, &config);
}

// Send alert message to Root
static void send_alert_message_to_root(uint8_t *data, size_t data_len)
{
    //ESP_LOGW( TAG, "Sending message to Root");    
    
    esp_mesh_lite_msg_config_t config = {
        .raw_msg = {
            .msg_id = TO_ROOT_ALERT_MSG_ID,  // Define your own message ID
            .expect_resp_msg_id = TO_ROOT_ALERT_MSG_ID_RESP,  // Response ID if needed
            .max_retry = 3,
            .retry_interval = 10,
            .data = data,
            .size = data_len,
            .raw_resend = esp_mesh_lite_send_raw_msg_to_root,  // Send raw message to Root
        },
    };
    
    esp_mesh_lite_send_msg(ESP_MESH_LITE_RAW_MSG, &config);
}

// Send Static message to Root
static void send_static_message_to_root(uint8_t *data, size_t data_len) 
{
    //ESP_LOGW( TAG, "Sending message to Root");    
    
    esp_mesh_lite_msg_config_t config = {
        .raw_msg = {
            .msg_id = TO_ROOT_STATIC_MSG_ID,  // Define your own message ID
            .expect_resp_msg_id = TO_ROOT_STATIC_MSG_ID_RESP,  // Response ID if needed
            .max_retry = 3,
            .retry_interval = 10,
            .data = data,
            .size = data_len,
            .raw_resend = esp_mesh_lite_send_raw_msg_to_root,  // Send raw message to Root
        },
    };
    
    esp_mesh_lite_send_msg(ESP_MESH_LITE_RAW_MSG, &config);
}

// Send Localization message to Root
static void send_localization_message_to_root(uint8_t *data, size_t data_len) 
{
    //ESP_LOGW( TAG, "Sending localization message to Root");    
    
    esp_mesh_lite_msg_config_t config = {
        .raw_msg = {
            .msg_id = TO_ROOT_LOCALIZATION_ID,  // Define your own message ID
            .expect_resp_msg_id = TO_ROOT_LOCALIZATION_ID_RESP,  // Response ID if needed
            .max_retry = 3,
            .retry_interval = 10,
            .data = data,
            .size = data_len,
            .raw_resend = esp_mesh_lite_send_raw_msg_to_root,  // Send raw message to Root
        },
    };
    
    esp_mesh_lite_send_msg(ESP_MESH_LITE_RAW_MSG, &config);
}

// Send Control message to Child
static void send_control_message_to_child(uint8_t *data, size_t data_len) 
{
    //ESP_LOGW( TAG, "Sending message to Child");    
    
    esp_mesh_lite_msg_config_t config = {
        .raw_msg = {
            .msg_id = TO_CHILD_CONTROL_MSG_ID,  // Define your own message ID
            .expect_resp_msg_id = TO_CHILD_CONTROL_MSG_ID_RESP,  // Response ID if needed
            .max_retry = 3,
            .retry_interval = 10,
            .data = data,
            .size = data_len,
            .raw_resend = esp_mesh_lite_send_broadcast_raw_msg_to_child,  // Send raw message to Child
        },
    };
    
    esp_mesh_lite_send_msg(ESP_MESH_LITE_RAW_MSG, &config);
}

//* High level sending functions

static void send_alert_payload()
{
    struct TX_peer *p = TX_peer_find_by_mac(self_mac);
    if (p!=NULL)
        update_status(p);
    else
        ESP_LOGE(TAG, "self mac peer not found alert payload");

    send_alert_message_to_root((uint8_t*)&self_alert_payload, sizeof(mesh_alert_payload_t));
}

static void send_dynamic_payload()
{
    struct TX_peer *p = TX_peer_find_by_mac(self_mac);
    if (p!=NULL)
        update_status(p);
    else
        ESP_LOGE(TAG, "self mac peer not found dynamic payload");

    send_dynamic_message_to_root((uint8_t*)&self_dynamic_payload, sizeof(mesh_dynamic_payload_t));
}

static void send_localization_payload(uint8_t pos, uint8_t *mac)
{
    mesh_localization_payload_t my_localization_payload = {
        .position = pos,
        };
    memcpy(my_localization_payload.macAddr, mac, ETH_HWADDR_LEN);
    send_localization_message_to_root((uint8_t*)&my_localization_payload, sizeof(mesh_localization_payload_t));
}

static void send_control_payload(TX_status command, uint8_t *mac)
{
    mesh_control_payload_t my_control_payload = {
        .command = (uint8_t)command,
        };
    memcpy(my_control_payload.macAddr, mac, ETH_HWADDR_LEN);
    send_control_message_to_child((uint8_t*)&my_control_payload, sizeof(mesh_control_payload_t));
}

static void send_static_payload(void)
{
    send_static_message_to_root((uint8_t*)&self_static_payload, sizeof(mesh_static_payload_t));
}

//*esp-NOW functions
/* Parse received ESPNOW data. */
static void handle_peer_dynamic(espnow_data_t* data, uint8_t* mac)
{
    //todo: disconnect when scooter leaves
    
    //populate mesh lite dynamic payload
    memcpy(self_dynamic_payload.RX.macAddr, mac, ETH_HWADDR_LEN);
    self_dynamic_payload.RX.voltage = data->field_1;
    self_dynamic_payload.RX.current = data->field_2;
    self_dynamic_payload.RX.temp1 = data->field_3;
    self_dynamic_payload.RX.temp2 = data->field_4;

    if (self_alert_payload.RX.RX_internal.FullyCharged)
        self_dynamic_payload.RX.rx_status = RX_FULLY_CHARGED;
    else if (self_alert_payload.RX.RX_all_flags) {
        self_dynamic_payload.RX.rx_status = RX_ALERT;
        self_dynamic_payload.TX.tx_status = TX_ALERT;
    }
    else if (self_dynamic_payload.TX.voltage > MIN_RX_VOLTAGE) {
        self_dynamic_payload.RX.rx_status = RX_CHARGING;
        strip_misalignment = strip_enable = false;
        strip_charging = true;
    }
    else if (self_dynamic_payload.RX.voltage > MISALIGNED_LIMIT) {
        self_dynamic_payload.RX.rx_status = RX_MISALIGNED;
        strip_charging = strip_enable = false;
        strip_misalignment = true;
    }

    //Peer ID
    struct RX_peer* p = RX_peer_find_by_mac(mac);
    if (p != NULL)
        self_dynamic_payload.RX.id = p->id;
    else
        ESP_LOGE(TAG, "RX peer not found in the list upon dynamic espNOW msg");
}

static void handle_peer_alert(espnow_data_t* data, uint8_t* mac)
{
    //switch off locally
    write_STM_command(TX_OFF);

    //populate mesh lite alert payload 
    memcpy(self_alert_payload.RX.macAddr, mac, ETH_HWADDR_LEN);
    self_alert_payload.RX.RX_internal.overvoltage = data->field_1;
    self_alert_payload.RX.RX_internal.overcurrent = data->field_2;
    self_alert_payload.RX.RX_internal.overtemperature = data->field_3;
    self_alert_payload.RX.RX_internal.FullyCharged = data->field_4;

    if (self_alert_payload.RX.RX_all_flags) {
        self_dynamic_payload.RX.rx_status = RX_ALERT;
        self_dynamic_payload.TX.tx_status = TX_ALERT;
    }
    else    
        self_dynamic_payload.RX.rx_status = RX_CHARGING;

    //Peer ID
    struct RX_peer* p = RX_peer_find_by_mac(mac);
    if (p != NULL)
        self_alert_payload.RX.id = p->id;
    else
        ESP_LOGE(TAG, "RX peer not found in the list upon alert espNOW msg");

    //reconnection timeout
}


uint8_t espnow_data_crc_control(uint8_t *data, uint16_t data_len)
{
    espnow_data_t *buf = (espnow_data_t *)data;
    uint16_t crc, crc_cal = 0;

    if (data_len < sizeof(espnow_data_t)) {
        ESP_LOGE(TAG, "Receive ESPNOW data too short, len:%d", data_len);
        return -1;
    }

    crc = buf->crc;
    buf->crc = 0;
    crc_cal = esp_crc16_le(UINT16_MAX, (uint8_t const *)buf, data_len);

    if (crc_cal == crc) {
        return 1;
    }

    return 0;
}

/* Prepare ESPNOW data to be sent. */
static void espnow_data_prepare(espnow_data_t *buf, espnow_message_type type)
{
    // initiliaze buf to 0
    memset(buf, 0, sizeof(espnow_data_t));
    // esp NOW data
    buf->id = CONFIG_UNIT_ID;
    buf->type = type;

    // save last message type to allow retranmission
    last_msg_type = type;

    switch (type)
    {
    case DATA_BROADCAST:
        buf->field_1 = self_dynamic_payload.RX.voltage;
        ESP_LOGI(TAG, "Broadcast data voltage %.2f", buf->field_1);
        break;

    case DATA_ASK_DYNAMIC:
        buf->field_1 = PEER_DYNAMIC_TIMER; //Min time between dynamic messages from RX
        break;

    case DATA_ALERT:
        // fill in alerts data
        buf->field_1 = self_alert_payload.RX.RX_internal.overvoltage;
        buf->field_2 = self_alert_payload.RX.RX_internal.overcurrent;
        buf->field_3 = self_alert_payload.RX.RX_internal.overtemperature;
        buf->field_4 = self_alert_payload.RX.RX_internal.FullyCharged;
        break;

    case DATA_DYNAMIC:
        // fill in dynamic data
        buf->field_1 = self_dynamic_payload.RX.voltage;
        buf->field_2 = self_dynamic_payload.RX.current;
        buf->field_3 = self_dynamic_payload.RX.temp1;
        buf->field_4 = self_dynamic_payload.RX.temp2;
        break;
        
    default:
        ESP_LOGE(TAG, "Message type error: %d", type);
        break;
    }

    // CRC
    buf->crc = esp_crc16_le(UINT16_MAX, (uint8_t const *)buf, sizeof(espnow_data_t));
}

static void add_peer_if_needed(const uint8_t *peer_addr)
{
    if (peer_addr == NULL) {
        return;
    }
    
    // Check if peer exists
    if (!esp_now_is_peer_exist(peer_addr)) {
        esp_now_peer_info_t peer_info = {0};
        memcpy(peer_info.peer_addr, peer_addr, 6);
        peer_info.channel = 0;  // Use current channel
        // CRITICAL: Use correct interface based on role
        // Root sends via AP, children send via STA
        peer_info.ifidx = is_root_node ? ESP_IF_WIFI_AP : ESP_IF_WIFI_STA;
        peer_info.encrypt = false;
        
        esp_err_t ret = esp_now_add_peer(&peer_info);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Added peer: "MACSTR" on %s interface", 
                    MAC2STR(peer_addr), is_root_node ? "AP" : "STA");
        } else {
            ESP_LOGE(TAG, "Failed to add peer "MACSTR": %s", 
                    MAC2STR(peer_addr), esp_err_to_name(ret));
        }
    }
}

/* ESPNOW sending or receiving callback function is called in WiFi task.
 * Users should not do lengthy operations from this task. Instead, post
 * necessary data to a queue and handle it from a lower priority task. */
static void my_espnow_send_cb(const esp_now_send_info_t *tx_info, esp_now_send_status_t status)
{
    espnow_event_t evt;
    espnow_event_send_cb_t *send_cb = &evt.info.send_cb;

    if (tx_info == NULL) {
        ESP_LOGE(TAG, "Send cb arg error");
        return;
    }

    //give back the semaphore if status is successfull
    if (status == ESP_NOW_SEND_SUCCESS)
        xSemaphoreGive(send_semaphore);

    evt.id = ID_ESPNOW_SEND_CB;
    memcpy(send_cb->mac_addr, tx_info->des_addr, ESP_NOW_ETH_ALEN);
    send_cb->status = status;

    if (xQueueSend(espnow_queue, &evt, pdMS_TO_TICKS(ESPNOW_QUEUE_MAXDELAY)) != pdTRUE) {
        ESP_LOGW(TAG, "Send send queue fail");
    }
}

static esp_err_t my_espnow_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len)
{
    //ESP_LOGI(TAG, "ESP-NOW received %d bytes from "MACSTR" (RSSI: %d)", 
    //        len, MAC2STR(recv_info->src_addr), recv_info->rx_ctrl->rssi);
    
    espnow_event_t evt;
    espnow_event_recv_cb_t *recv_cb = &evt.info.recv_cb;
    uint8_t * mac_addr = recv_info->src_addr;
    //int rssi = recv_info->rx_ctrl->rssi;

    if (mac_addr == NULL || data == NULL || len <= 0) {
        ESP_LOGE(TAG, "Receive cb arg error");
        return ESP_FAIL;
    }

    evt.id = ID_ESPNOW_RECV_CB;
    memcpy(recv_cb->mac_addr, mac_addr, ESP_NOW_ETH_ALEN);
    recv_cb->data = malloc(len);
    if (recv_cb->data == NULL) {
        ESP_LOGE(TAG, "Malloc receive data fail");
        return ESP_FAIL;
    }
    memcpy(recv_cb->data, data, len);
    recv_cb->data_len = len;

    if (xQueueSend(espnow_queue, &evt, pdMS_TO_TICKS(ESPNOW_QUEUE_MAXDELAY)) != pdTRUE) {
        ESP_LOGW(TAG, "Send receive queue fail");
        free(recv_cb->data);
    }

    return ESP_OK;
}

static void espnow_send_message(espnow_message_type mdgType, uint8_t* mac_addr)
{
    if (xSemaphoreTake(send_semaphore, pdMS_TO_TICKS(ESPNOW_QUEUE_MAXDELAY)) == pdTRUE)
    {
        espnow_data_prepare(espnow_data, mdgType);
        if (esp_mesh_lite_espnow_send(ESPNOW_DATA_TYPE_RESERVE, mac_addr, (const uint8_t *)espnow_data, sizeof(espnow_data_t)) != ESP_OK) {
            ESP_LOGE(TAG, "Send error");
        }
    }
    else
        ESP_LOGE(TAG, "Could not take send semaphore!");
}


static void espnow_task(void *pvParameter)
{
    //Create queue to process esp now events
    espnow_queue = xQueueCreate(ESPNOW_QUEUE_SIZE, sizeof(espnow_event_t));
    if (espnow_queue == NULL) {
        ESP_LOGE(TAG, "Create ESP-NOW queue fail");
        return;
    }
    
    espnow_event_t evt;

    add_peer_if_needed(broadcast_mac);

    while (1) 
    {
        if (xQueueReceive(espnow_queue, &evt, portMAX_DELAY) && is_mesh_connected) //if mesh not connected dump the queue?
        {
            switch (evt.id) {
                case ID_ESPNOW_SEND_CB:
                {
                    espnow_event_send_cb_t *send_cb = &evt.info.send_cb;
                    //ESP_LOGI(TAG, "send data to "MACSTR", status: %d", MAC2STR(send_cb->mac_addr), send_cb->status);
                    bool addr_type = IS_BROADCAST_ADDR(send_cb->mac_addr);
                    if (addr_type)
                    {
                        //ESP_LOGI(TAG, "Broadcast data sent!");
                        //broadcast always successfull anyway (no ack)
                    }
                    else
                    {
                        ESP_LOGW(TAG, "Unicast data sent %d!", last_msg_type);

                        //unicast message
                        if (send_cb->status != ESP_NOW_SEND_SUCCESS) 
                        {
                            ESP_LOGE(TAG, "ERROR SENDING DATA TO "MACSTR"", MAC2STR(send_cb->mac_addr));
                            comms_fail++;

                            //MAX_COMMS_CONSECUTIVE_ERRORS --> RESTART
                            if (comms_fail > MAX_COMMS_ERROR)
                            {
                                ESP_LOGE(TAG, "TOO MANY COMMS ERRORS, RESTARTING");

                                //delete all peers
                                delete_all_peers();

                                //reboot
                                esp_restart();
                            }
                            else //RETRANSMISSIONS
                            {
                                //retransmit
                                ESP_LOGW(TAG, "RETRANSMISSION n. %d", comms_fail);
                                espnow_data_prepare(espnow_data, last_msg_type);
                                esp_mesh_lite_espnow_send(ESPNOW_DATA_TYPE_RESERVE, send_cb->mac_addr, (const uint8_t *)espnow_data, sizeof(espnow_data_t));
                            }
                        }
                        else 
                        {
                            //reset comms - we are good
                            comms_fail = 0;
                        }
                    }
                    break;
                }
                case ID_ESPNOW_RECV_CB:
                {
                    espnow_event_recv_cb_t *recv_cb = &evt.info.recv_cb;

                    // Check CRC of received ESPNOW data.
                    if(!espnow_data_crc_control(recv_cb->data, recv_cb->data_len))
                    {
                        ESP_LOGE(TAG, "Receive error data from: "MACSTR"", MAC2STR(recv_cb->mac_addr));
                        break;
                    }
                    // Parse received ESPNOW data.
                    espnow_data_t *recv_data = (espnow_data_t *)recv_cb->data; 
                    //int8_t unitID = recv_data->id;
                    espnow_message_type msg_type = recv_data->type;

                    //ESP_LOGI(TAG, "Received ESP-NOW message with from: "MACSTR"", MAC2STR(recv_cb->mac_addr));

                    if (msg_type == DATA_BROADCAST && (UNIT_ROLE == TX))
                    {
                        ESP_LOGI(TAG, "Receive broadcast data from: "MACSTR", RX voltage: %.2f", MAC2STR(recv_cb->mac_addr), recv_data->field_1);

                        if (recv_data->field_1 > MIN_RX_VOLTAGE)
                        {
                            //Case 1 - I am another RX - discard - done
                            //Case 2 - I am master TX - localization done (advise other TXs updating localization table)
                            if (is_root_node)
                            {
                                // TX will tell the RX via ESP-NOW
                                if (self_dynamic_payload.TX.tx_status == TX_LOCALIZATION)
                                {
                                    ESP_LOGI(TAG, "RX has been located to this TX (which is also the master)!");
                                    write_STM_command(TX_DEPLOY);
                                    previousTX_pos = 0;
                                    // Save peer and communicate via ESP-NOW
                                    add_peer_if_needed(recv_cb->mac_addr);
                                    // ask for dynamic data 
                                    espnow_send_message(DATA_ASK_DYNAMIC, recv_cb->mac_addr);
                                    //update RX peer position
                                    struct RX_peer* p = RX_peer_find_by_mac(recv_cb->mac_addr);
                                    if (p != NULL)
                                    {
                                        p->position = CONFIG_UNIT_ID; //position same as ID for RX
                                        p->RX_status = RX_CHARGING;
                                        ESP_LOGI(TAG, "RX peer position updated to %d", p->position);
                                    }
                                }
                            }
                            //Case 3 - I am TX - am I active? yes then tell master - no then discard
                            else if (self_dynamic_payload.TX.tx_status == TX_LOCALIZATION)
                            {
                                ESP_LOGI(TAG, "RX has been located to this pad!");
                                write_STM_command(TX_DEPLOY);
                                // Save peer and communicate via ESP-NOW
                                add_peer_if_needed(recv_cb->mac_addr);
                                // ask for dynamic data 
                                espnow_send_message(DATA_ASK_DYNAMIC, recv_cb->mac_addr);
                                //Advise master TO update peer position
                                send_localization_payload(CONFIG_UNIT_ID, recv_cb->mac_addr);
                            }
                        }   
                    }
                    else if (msg_type == DATA_ASK_DYNAMIC)
                    {
                        ESP_LOGW(TAG, "Locking TX on ESPNOW!");
                        // Save peer and communicate via ESP-NOW
                        add_peer_if_needed(recv_cb->mac_addr);
                        //save TX parent MAC addr
                        memcpy(TX_parent_mac, recv_cb->mac_addr, ETH_HWADDR_LEN);
                        DynTimeout = recv_data->field_1;
                        rxLocalized = true;
                    }
                    else if(msg_type == DATA_DYNAMIC)
                    {
                        ESP_LOGI(TAG, "Receive DYNAMIC data from: "MACSTR", len: %d", MAC2STR(recv_cb->mac_addr), recv_cb->data_len);
                        handle_peer_dynamic(recv_data, recv_cb->mac_addr);
                    }
                    else if (msg_type == DATA_ALERT)
                    {
                        ESP_LOGI(TAG, "Receive ALERT data from: "MACSTR", len: %d", MAC2STR(recv_cb->mac_addr), recv_cb->data_len);
                        handle_peer_alert(recv_data, recv_cb->mac_addr); 
                    }
                    else
                        ESP_LOGI(TAG, "Receive unexpected message type %d data from: "MACSTR"", msg_type, MAC2STR(recv_cb->mac_addr));

                    free(recv_data);
                    break;
                }
                default:
                    ESP_LOGE(TAG, "Callback type error: %d", evt.id);
                    break;
            }
        }
    }

    vQueueDelete(espnow_queue);
    vTaskDelete(NULL);
    free(espnow_data);
}

static void reset_the_baton()
{
    // switch all TX available for localization OFF (mesh-lite and local switch off)
    send_control_payload(TX_OFF, broadcast_mac); //broadcast
    write_STM_command(TX_OFF);

    // update list structures
    allLocalizationTxPeersOFF();
}

static void pass_the_baton()
{
    struct TX_peer* p = find_next_TX_for_localization(previousTX_pos);
    if (p == NULL)
    {
        ESP_LOGE(TAG, "TX peer not found during localization");
        return;
    }
    p->dynamic_payload->TX.tx_status = TX_LOCALIZATION;

    //if (previousTX_pos != p->position)
    //{
        //switch all OFF
        reset_the_baton();
        vTaskDelay(800);

        //switch next ON
        if(p->position == CONFIG_UNIT_ID)
        {
            ESP_LOGI(TAG, "I am the next TX for localization, switching ON locally");
            write_STM_command(TX_LOCALIZATION);
        }
        else
        {
            ESP_LOGI(TAG, "Next TX for localization is ID %d, switching it ON via mesh-lite", p->position);
            send_control_payload(TX_LOCALIZATION, p->MACaddress);
        }
        vTaskDelay(800);

        previousTX_pos = p->position;
    //}
}

static void alert_task(void *pvParameters)
{    
    while (1) 
    {
        // Check for alert
        if (alert_payload_changed(&self_alert_payload, &self_previous_alert_payload))
        {   
            // ESP-NOW for RX -> TX parent
            if (UNIT_ROLE == RX && rxLocalized) 
            {
                ESP_LOGW(TAG, "Sending CRITICAL alert via ESP-NOW");
                espnow_send_message(DATA_ALERT, TX_parent_mac);
                self_previous_alert_payload = self_alert_payload;
            } 
            else if (UNIT_ROLE == TX && !is_root_node) // Mesh-Lite for TX -> Master
            {
                ESP_LOGW(TAG, "Sending alert via Mesh-Lite");
                send_alert_payload();
                self_previous_alert_payload = self_alert_payload;
            }
            //LEDs
            if (UNIT_ROLE == TX && (self_alert_payload.TX.TX_all_flags || self_alert_payload.RX.RX_all_flags))
            {
                strip_charging = strip_enable = strip_misalignment = false;
                set_strip(200, 0, 0);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1)); //minimum wait
    }
}

static void wifi_mesh_lite_task(void *pvParameters)
{
    // Register rcv handlers
    esp_mesh_lite_raw_msg_action_t raw_actions[] = {
        { TO_ROOT_STATIC_MSG_ID, TO_ROOT_STATIC_MSG_ID_RESP, static_to_root_raw_msg_process},
        { TO_ROOT_STATIC_MSG_ID_RESP, 0, static_to_root_raw_msg_response_process},
        { TO_ROOT_DYNAMIC_MSG_ID, TO_ROOT_DYNAMIC_MSG_ID_RESP, dynamic_to_root_raw_msg_process},
        { TO_ROOT_DYNAMIC_MSG_ID_RESP, 0, dynamic_to_root_raw_msg_response_process},
        { TO_ROOT_ALERT_MSG_ID, TO_ROOT_ALERT_MSG_ID_RESP, alert_to_root_raw_msg_process},
        { TO_ROOT_ALERT_MSG_ID_RESP, 0, alert_to_root_raw_msg_response_process},
        { TO_ROOT_LOCALIZATION_ID, TO_ROOT_LOCALIZATION_ID_RESP, localization_to_root_raw_msg_process}, 
        { TO_ROOT_LOCALIZATION_ID_RESP, 0, localization_to_root_raw_msg_process_response},
        { TO_CHILD_CONTROL_MSG_ID, TO_CHILD_CONTROL_MSG_ID_RESP, control_to_child_raw_msg_process},
        { TO_CHILD_CONTROL_MSG_ID_RESP, 0, control_to_child_raw_msg_response_process},
        {0, 0, NULL}
    };
    esp_mesh_lite_raw_msg_action_list_register(raw_actions);

    static uint32_t lastDynamic = 0;

    while (1) 
    {
        if (is_mesh_connected)
        {
            if (is_root_node)
            {
                // take care of sequential switching during localization
                if (atLeastOneRxNeedLocalization())
                {
                    pass_the_baton();
                    //todo (make this smarter /hybrid for dead battery sceanario)
                }
            }
            else
            {
                if(UNIT_ROLE == TX)
                {
                    //meshlite send dynamic payload upon changes or min time
                    if (dynamic_payload_changed(&self_dynamic_payload, &self_previous_dynamic_payload) || 
                        ((xTaskGetTickCount() - lastDynamic) * portTICK_PERIOD_MS > DynTimeout * 1000))
                    {
                        send_dynamic_payload();
                        self_previous_dynamic_payload = self_dynamic_payload;
                        lastDynamic = xTaskGetTickCount();
                        vTaskDelay(pdMS_TO_TICKS(100)); //min interval to avoid flooding
                    }
                }
                else
                {
                    if (!rxLocalized)
                    {
                        //espnow broadcast when voltage rises
                        xEventGroupWaitBits(eventGroupHandle, LOCALIZEDBIT, pdTRUE, pdFALSE, portMAX_DELAY);
                        espnow_send_message(DATA_BROADCAST, broadcast_mac);
                    }
                    else
                    {
                        //espnow send dynamic payload upon changes or min time
                        if (dynamic_payload_changed(&self_dynamic_payload, &self_previous_dynamic_payload) || 
                            ((xTaskGetTickCount() - lastDynamic) * portTICK_PERIOD_MS > DynTimeout * 1000))
                        {           
                            espnow_send_message(DATA_DYNAMIC, TX_parent_mac);
                            self_previous_dynamic_payload = self_dynamic_payload;
                            lastDynamic = xTaskGetTickCount();
                            vTaskDelay(pdMS_TO_TICKS(100)); //min interval to avoid flooding
                        }
                    }
                }
            } 
        }
        vTaskDelay(pdMS_TO_TICKS(10)); // Delay for 10ms
    }

    vTaskDelete(NULL);
}
 
static void handle_mesh_changes_timercb(TimerHandle_t timer)
{
    uint8_t sta_mac[6] = {0};
    esp_wifi_get_mac(ESP_IF_WIFI_STA, sta_mac);

    ESP_LOGI(TAG, "System information -- layer: %d, self mac: " MACSTR "", 
             esp_mesh_lite_get_level(), MAC2STR(sta_mac));

    uint32_t size = 0;
    const node_info_list_t *node = esp_mesh_lite_get_nodes_list(&size);
    printf("MeshLite nodes %ld:\r\n", size);
    for (uint32_t loop = 0; (loop < size) && (node != NULL); loop++) {
        struct in_addr ip_struct;
        ip_struct.s_addr = node->node->ip_addr;
        printf("%ld: Level %d, MAC "MACSTR", %s, TTL %ld\r\n" , loop + 1, node->node->level, MAC2STR(node->node->mac_addr), inet_ntoa(ip_struct), node->ttl);
        node = node->next;
    }
}


static void mesh_lite_event_handler(void *arg, esp_event_base_t event_base,
                                        int32_t event_id, void *event_data)
{
    esp_mesh_lite_node_info_t *node_info = (esp_mesh_lite_node_info_t *)event_data;

    switch (event_id)
    {
        case ESP_MESH_LITE_EVENT_NODE_JOIN:
            ESP_LOGW(TAG, "<ESP_MESH_LITE_EVENT_NODE_JOIN>");
            ESP_LOGI(TAG, "New node joined: Level %d, MAC: "MACSTR", IP: %s", node_info->level, MAC2STR(node_info->mac_addr), inet_ntoa(node_info->ip_addr));
            is_mesh_connected = true;
            if (!is_root_node && (memcmp(node_info->mac_addr, self_mac, ETH_HWADDR_LEN) == 0)) {
                send_static_payload();
            }
            break;
        case ESP_MESH_LITE_EVENT_NODE_LEAVE:
            ESP_LOGW(TAG, "<ESP_MESH_LITE_EVENT_NODE_LEAVE>");
            ESP_LOGI(TAG, "Node left: Level %d, MAC: "MACSTR", IP: %s", node_info->level, MAC2STR(node_info->mac_addr), inet_ntoa(node_info->ip_addr));
            // Remove node from list
            peer_delete(node_info->mac_addr); // update status on tx payload
            break;
        case ESP_MESH_LITE_EVENT_NODE_CHANGE:
            ESP_LOGW(TAG, "<ESP_MESH_LITE_EVENT_NODE_CHANGE>");
            // NEW LEVEL OR IP ADDRESS
            ESP_LOGI(TAG, "Node changed: Level %d, MAC: "MACSTR", IP: %s", node_info->level, MAC2STR(node_info->mac_addr), inet_ntoa(node_info->ip_addr));
            xEventGroupSetBits(eventGroupHandle, MESH_FORMEDBIT);
            mesh_level = esp_mesh_lite_get_level();
            is_root_node = (mesh_level == 1);
            is_mesh_connected = true;
            // if not a root, send static payload to root
            if (!is_root_node && (memcmp(node_info->mac_addr, self_mac, ETH_HWADDR_LEN) == 0)) {
                send_static_payload();
            } else if (is_root_node) 
            { 
                // Initialize peer management (adding myself)
                peer_init();

                // Initialize MQTT client manager
                esp_err_t ret = mqtt_client_manager_init();
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to initialize MQTT client manager");
                } else {
                    ESP_LOGI(TAG, "MQTT client manager started successfully");
                }
            }
            break;
        case ESP_MESH_LITE_EVENT_CORE_STARTED:
            //ESP_LOGW(TAG, "<ESP_MESH_LITE_EVENT_CORE_STARTED>");
            break;
        default:
            ESP_LOGW(TAG, "Unhandled mesh event id: %d", event_id);
            break;
    }

}

static void ip_event_handler(void *arg, esp_event_base_t event_base,
                                        int32_t event_id, void *event_data)
{
    switch (event_id) 
    {
        case IP_EVENT_STA_GOT_IP:
            ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
            ESP_LOGI(TAG, "<IP_EVENT_STA_GOT_IP>IP:" IPSTR, IP2STR(&event->ip_info.ip)); 
            break;

        case IP_EVENT_STA_LOST_IP:
            ESP_LOGW(TAG, "<IP_EVENT_STA_LOST_IP>");
            is_mesh_connected = false;
            is_root_node = false;
            mesh_level = -1;
            break;

        default:
            ESP_LOGW(TAG, "Unhandled ip event id: %d", event_id);
            break;
    }
}


void app_wifi_set_softap_info(void)
{
    char softap_ssid[33];
    uint8_t softap_mac[6];
    esp_wifi_get_mac(WIFI_IF_AP, softap_mac);
    
    // Create unique SSID with MAC
    snprintf(softap_ssid, sizeof(softap_ssid), "%s_%02X%02X", 
             CONFIG_BRIDGE_SOFTAP_SSID, softap_mac[4], softap_mac[5]);
    
    esp_mesh_lite_set_softap_info(softap_ssid, CONFIG_BRIDGE_SOFTAP_PASSWORD);
}

static void wifi_init(void)
{
    esp_wifi_set_ps(WIFI_PS_NONE);
    // CRITICAL: Set AP to disconnect inactive stations quickly
    esp_wifi_set_inactive_time(WIFI_IF_AP, 15);  // 15 seconds of inactivity = disconnect

    // Station config (yes router connection)
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = "manu",//CONFIG_MESH_ROUTER_SSID,
            .password = "cherrycookies",//CONFIG_MESH_ROUTER_PASSWD,
        },
    };
    
    /* //! No router config test
    wifi_config_t wifi_config;
    memset(&wifi_config, 0x0, sizeof(wifi_config_t));
    */
    esp_bridge_wifi_set_config(WIFI_IF_STA, &wifi_config);
    
    // SoftAP config
    wifi_config_t softap_config = {
        .ap = {
            .ssid = CONFIG_BRIDGE_SOFTAP_SSID,
            .password = CONFIG_BRIDGE_SOFTAP_PASSWORD,
            .authmode = CONFIG_MESH_AP_AUTHMODE,
            .channel = 0, //automatically found
            .max_connection = 10,
            .beacon_interval = 100, //100 TU = 102ms (standard)
            .dtim_period = 1, // 1 to 10 - indicates how often the AP will send DTIM beacon indicating buffered data
        },
    };
    esp_bridge_wifi_set_config(WIFI_IF_AP, &softap_config);
}

void wifi_mesh_init()
{   
    // Initialize networking
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // Create network interfaces
    esp_bridge_create_all_netif();
    
    // Initialize WiFi
    wifi_init();
    
    // Initialize mesh-lite
    esp_mesh_lite_config_t mesh_lite_config = ESP_MESH_LITE_DEFAULT_INIT();
    esp_mesh_lite_init(&mesh_lite_config);

    /*
    if(IS_TX_UNIT)
        esp_mesh_lite_set_allowed_level(1); //! The mesh needs at least one node explicitly configured as root (level 1) for NO ROUTER configuration 
    */

    if(UNIT_ROLE == RX)
    {
        esp_mesh_lite_set_disallowed_level(1);
        //esp_mesh_lite_set_disallowed_level(2); (more than 10 TXs)
        //esp_mesh_lite_set_disallowed_level(3); (more than 100 TXs)
        esp_mesh_lite_set_leaf_node(true);
    }

    // Set SoftAP info
    app_wifi_set_softap_info();
    
    // Get MAC address
    esp_wifi_get_mac(ESP_IF_WIFI_STA, self_mac);
    ESP_LOGI(TAG, "Device MAC: "MACSTR, MAC2STR(self_mac));

    // Init mesh-lite payloads
    init_payloads();

    // Register WiFi event handler
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &ip_event_handler, NULL));

    // Register Mesh lite event handler
    ESP_ERROR_CHECK(esp_event_handler_register(ESP_MESH_LITE_EVENT, ESP_EVENT_ANY_ID, &mesh_lite_event_handler, NULL));
    
    // Start mesh
    esp_mesh_lite_start();

    // Start system info timer (debug)
    TimerHandle_t timer = xTimerCreate("mesh_nodes_info", 5000 / portTICK_PERIOD_MS,
                                      pdTRUE, NULL, handle_mesh_changes_timercb);
    xTimerStart(timer, 5000);

    // WiFi Mesh Lite task
    ESP_ERROR_CHECK(xTaskCreate(wifi_mesh_lite_task, "wifi_mesh_lite_task", 10000, NULL, 10, NULL));

    //* ESP-NOW
    // Initialize ESP-NOW through mesh-lite //max payload ESPNOW_PAYLOAD_MAX_LEN
    // Register receive callback through mesh-lite function
    ESP_ERROR_CHECK(esp_mesh_lite_espnow_recv_cb_register(ESPNOW_DATA_TYPE_RESERVE, my_espnow_recv_cb));

    // Register send callback
    ESP_ERROR_CHECK(esp_now_register_send_cb(my_espnow_send_cb));

    espnow_data = malloc(sizeof(espnow_data_t));
    if (espnow_data == NULL) {
        ESP_LOGE(TAG, "Malloc send buffer fail");
        return;
    }

    // Create ESPNOW task
    ESP_ERROR_CHECK(xTaskCreate(espnow_task, "espnow_task", 4096, NULL, 10, NULL));

    // Create alert high priority task
    ESP_ERROR_CHECK(xTaskCreate(alert_task, "alert_task", 4096, NULL, 11, NULL));

    send_semaphore = xSemaphoreCreateBinary();
    if (send_semaphore == NULL) {
        ESP_LOGE(TAG, "Create send semaphore fail");
        return;
    }
    xSemaphoreGive(send_semaphore); // crucial
    
    ESP_LOGI(TAG, "ESP-MESH-LITE initialized");
}