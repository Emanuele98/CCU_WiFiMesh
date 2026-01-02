/**
 * @file ota_manager.c
 * @brief Bumblebee OTA Manager Implementation
 * 
 * HTTPS-based OTA with SHA256 verification for ESP32/ESP32-C6.
 */

#include "ota_manager.h"

static const char *TAG = "OTA_MANAGER";

// ============================================================================
// PRIVATE VARIABLES
// ============================================================================

static ota_progress_t s_progress = {0};
static SemaphoreHandle_t s_progress_mutex = NULL;
static TaskHandle_t s_ota_task_handle = NULL;
static ota_completion_cb_t s_completion_callback = NULL;
static char s_expected_sha256[OTA_SHA256_HEX_LEN + 1] = {0};
static bool s_initialized = false;

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

static void ota_publish_status(const char* status, int progress, int bytes_written, int total_size)
{
    char topic[64];
    char payload[256];
    
    snprintf(topic, sizeof(topic), "bumblebee/%d/ota/status", UNIT_ID);
    snprintf(payload, sizeof(payload), 
        "{\"unit_id\":%d,\"status\":\"%s\",\"progress\":%d,\"bytes\":%d,\"total\":%d}",
        UNIT_ID, status, progress, bytes_written, total_size);

        ESP_LOGI(TAG, "Publishing OTA status: %s", payload);
    
    if (mqtt_client) {
        esp_mqtt_client_publish(mqtt_client, topic, payload, 0, 1, 0);
    }
}

/**
 * @brief Thread-safe progress update
 */
static void update_progress(ota_status_t status, ota_error_t error,
                           uint32_t downloaded, uint32_t total)
{
    if (xSemaphoreTake(s_progress_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_progress.status = status;
        s_progress.error = error;
        s_progress.bytes_downloaded = downloaded;
        s_progress.total_bytes = total;
        
        if (total > 0) {
            s_progress.progress_percent = (uint8_t)((downloaded * 100) / total);
        } else {
            s_progress.progress_percent = 0;
        }
        
        xSemaphoreGive(s_progress_mutex);
    }
}

/**
 * @brief Convert hex string to lowercase for comparison
 */
static void hex_to_lower(char *str)
{
    while (*str) {
        *str = tolower((unsigned char)*str);
        str++;
    }
}

/**
 * @brief Convert binary SHA256 to hex string
 */
static void sha256_to_hex(const uint8_t *hash, char *hex_str)
{
    for (int i = 0; i < 32; i++) {
        sprintf(hex_str + (i * 2), "%02x", hash[i]);
    }
    hex_str[64] = '\0';
}

/**
 * @brief Compare two SHA256 hex strings (case-insensitive)
 */
static bool sha256_compare(const char *hash1, const char *hash2)
{
    char h1[65], h2[65];
    
    strncpy(h1, hash1, 64);
    h1[64] = '\0';
    strncpy(h2, hash2, 64);
    h2[64] = '\0';
    
    hex_to_lower(h1);
    hex_to_lower(h2);
    
    return (strcmp(h1, h2) == 0);
}

// ============================================================================
// OTA DOWNLOAD TASK
// ============================================================================

static void ota_download_task(void *pvParameters)
{
    ESP_LOGI(TAG, "OTA Download Task Started");
    ESP_LOGI(TAG, "URL: %s", OTA_FIRMWARE_URL);
    
    esp_err_t err;
    esp_ota_handle_t ota_handle = 0;
    const esp_partition_t *update_partition = NULL;
    
    // SHA256 context for streaming calculation
    mbedtls_sha256_context sha256_ctx;
    uint8_t sha256_result[32];
    char sha256_hex[65];
    
    // Download buffer
    char *buffer = malloc(OTA_BUF_SIZE);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate download buffer");
        update_progress(OTA_STATUS_FAILED, OTA_ERR_NO_MEMORY, 0, 0);
        ota_publish_status("failed", 0, 0, 0);
        goto cleanup;
    }
    
    // Initialize SHA256
    mbedtls_sha256_init(&sha256_ctx);
    mbedtls_sha256_starts(&sha256_ctx, 0);  // 0 = SHA256 (not SHA224)
    
    update_progress(OTA_STATUS_STARTING, OTA_ERR_NONE, 0, 0);
    ota_publish_status("starting", 0, 0, 0);
    
    // ========================================================================
    // STEP 1: Find update partition
    // ========================================================================
    
    update_partition = esp_ota_get_next_update_partition(NULL);
    if (update_partition == NULL) {
        ESP_LOGE(TAG, "No OTA partition found");
        update_progress(OTA_STATUS_FAILED, OTA_ERR_PARTITION, 0, 0);
        ota_publish_status("failed", 0, 0, 0);
        goto cleanup;
    }
    
    ESP_LOGI(TAG, "Writing to partition: %s (offset 0x%08lx)",
             update_partition->label, update_partition->address);
    
    // ========================================================================
    // STEP 2: Setup HTTP client with TLS
    // ========================================================================
    
    esp_http_client_config_t config = {
        .url = OTA_FIRMWARE_URL,
        .cert_pem = NULL,  // Set to NULL for HTTP, or keep for HTTPS
        .timeout_ms = OTA_HTTP_TIMEOUT_MS,
        .buffer_size = OTA_BUF_SIZE,
        .buffer_size_tx = OTA_BUF_SIZE,
        .username = OTA_HTTP_USERNAME,     
        .password = OTA_HTTP_PASSWORD,     
        .auth_type = HTTP_AUTH_TYPE_BASIC, 
    };
    
    esp_http_client_handle_t http_client = esp_http_client_init(&config);
    if (!http_client) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        update_progress(OTA_STATUS_FAILED, OTA_ERR_HTTP_CONNECT, 0, 0);
        ota_publish_status("failed", 0, 0, 0);
        goto cleanup;
    }

    ota_publish_status("connecting", 0, 0, 0);
    
    // ========================================================================
    // STEP 3: Open HTTP connection
    // ========================================================================
    
    err = esp_http_client_open(http_client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP connection failed: %s", esp_err_to_name(err));
        update_progress(OTA_STATUS_FAILED, OTA_ERR_HTTP_CONNECT, 0, 0);
        ota_publish_status("failed", 0, 0, 0);
        goto cleanup_http;
    }
    
    int content_length = esp_http_client_fetch_headers(http_client);
    int status_code = esp_http_client_get_status_code(http_client);
    
    ESP_LOGI(TAG, "HTTP Status: %d, Content-Length: %d", status_code, content_length);
    
    if (status_code != 200) {
        ESP_LOGE(TAG, "HTTP error: %d", status_code);
        update_progress(OTA_STATUS_FAILED, OTA_ERR_HTTP_RESPONSE, 0, 0);
        ota_publish_status("failed", 0, 0, 0);
        goto cleanup_http;
    }
    
    if (content_length <= 0) {
        ESP_LOGW(TAG, "Content-Length unknown, proceeding anyway");
        content_length = 0;  // Will show 0% progress
    }

    ota_publish_status("connecting", 0, 0, content_length);
    
    // ========================================================================
    // STEP 4: Begin OTA
    // ========================================================================
    
    err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        update_progress(OTA_STATUS_FAILED, OTA_ERR_PARTITION, 0, 0);
        ota_publish_status("failed", 0, 0, 0);
        goto cleanup_http;
    }
    
    update_progress(OTA_STATUS_DOWNLOADING, OTA_ERR_NONE, 0, content_length);
    
    // ========================================================================
    // STEP 5: Download and write firmware
    // ========================================================================
    
    uint32_t bytes_downloaded = 0;
    int bytes_read = 0;
    bool header_checked = false;
    int last_progress_log = -1;
    
    while (1) {
        bytes_read = esp_http_client_read(http_client, buffer, OTA_BUF_SIZE);
        
        if (bytes_read < 0) {
            ESP_LOGE(TAG, "HTTP read error");
            update_progress(OTA_STATUS_FAILED, OTA_ERR_DOWNLOAD, bytes_downloaded, content_length);
            ota_publish_status("failed", 0, bytes_downloaded, content_length);
            esp_ota_abort(ota_handle);
            goto cleanup_http;
        }
        
        if (bytes_read == 0) {
            // Download complete
            if (esp_http_client_is_complete_data_received(http_client)) {
                ESP_LOGI(TAG, "Download complete: %lu bytes", bytes_downloaded);
                break;
            }
            // Connection closed prematurely
            ESP_LOGE(TAG, "Connection closed unexpectedly");
            update_progress(OTA_STATUS_FAILED, OTA_ERR_DOWNLOAD, bytes_downloaded, content_length);
            ota_publish_status("failed", 0, bytes_downloaded, content_length);
            esp_ota_abort(ota_handle);
            goto cleanup_http;
        }
        
        // Check firmware header on first chunk
        if (!header_checked && bytes_read > sizeof(esp_image_header_t)) {
            esp_app_desc_t *app_desc = (esp_app_desc_t *)(buffer + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t));
            
            // Basic sanity check
            if (app_desc->magic_word == ESP_APP_DESC_MAGIC_WORD) {
                ESP_LOGI(TAG, "New firmware version: %s", app_desc->version);
                ESP_LOGI(TAG, "Project name: %s", app_desc->project_name);
                
                // Store version in progress
                if (xSemaphoreTake(s_progress_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    strncpy(s_progress.version, app_desc->version, sizeof(s_progress.version) - 1);
                    xSemaphoreGive(s_progress_mutex);
                }
            }
            header_checked = true;
        }
        
        // Update SHA256 hash
        mbedtls_sha256_update(&sha256_ctx, (const unsigned char *)buffer, bytes_read);
        
        // Write to flash
        err = esp_ota_write(ota_handle, buffer, bytes_read);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            update_progress(OTA_STATUS_FAILED, OTA_ERR_FLASH_WRITE, bytes_downloaded, content_length);
            ota_publish_status("failed", 0, bytes_downloaded, content_length);
            esp_ota_abort(ota_handle);
            goto cleanup_http;
        }
        
        bytes_downloaded += bytes_read;
        
        // Update progress
        update_progress(OTA_STATUS_DOWNLOADING, OTA_ERR_NONE, bytes_downloaded, content_length);
        
        // Log progress every 10%
        if (content_length > 0) {
            int current_progress = (bytes_downloaded * 100) / content_length;
            if (current_progress / 10 > last_progress_log / 10) {
                ESP_LOGI(TAG, "Download progress: %d%% (%lu / %d bytes)",
                        current_progress, bytes_downloaded, content_length);
                ota_publish_status("downloading", current_progress, bytes_downloaded, content_length);
                last_progress_log = current_progress;
            }
        }
        
        // Yield to other tasks
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    
    // ========================================================================
    // STEP 6: Finalize SHA256 and verify
    // ========================================================================
    
    update_progress(OTA_STATUS_VERIFYING, OTA_ERR_NONE, bytes_downloaded, content_length);
    ota_publish_status("verifying", 100, bytes_downloaded, content_length);
    
    mbedtls_sha256_finish(&sha256_ctx, sha256_result);
    sha256_to_hex(sha256_result, sha256_hex);
    
    ESP_LOGI(TAG, "Calculated SHA256: %s", sha256_hex);
    
    // Verify SHA256 if expected hash was provided
    if (strlen(s_expected_sha256) == OTA_SHA256_HEX_LEN) {
        ESP_LOGI(TAG, "Expected SHA256:   %s", s_expected_sha256);
        
        if (!sha256_compare(sha256_hex, s_expected_sha256)) {
            ESP_LOGE(TAG, "SHA256 MISMATCH! Aborting OTA");
            update_progress(OTA_STATUS_FAILED, OTA_ERR_SHA256_MISMATCH, bytes_downloaded, content_length);
            ota_publish_status("failed", 0, bytes_downloaded, content_length);
            esp_ota_abort(ota_handle);
            goto cleanup_http;
        }
        
        ESP_LOGI(TAG, "SHA256 verification PASSED");
    } else {
        ESP_LOGW(TAG, "No SHA256 provided, skipping verification (NOT RECOMMENDED)");
    }
    
    // ========================================================================
    // STEP 7: Finalize OTA
    // ========================================================================
    
    update_progress(OTA_STATUS_FLASHING, OTA_ERR_NONE, bytes_downloaded, content_length);
    
    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
            ESP_LOGE(TAG, "Image validation failed (corrupt or wrong chip)");
            update_progress(OTA_STATUS_FAILED, OTA_ERR_IMAGE_INVALID, bytes_downloaded, content_length);
            ota_publish_status("failed", 0, bytes_downloaded, content_length);
        } else {
            ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
            update_progress(OTA_STATUS_FAILED, OTA_ERR_FLASH_WRITE, bytes_downloaded, content_length);
            ota_publish_status("failed", 0, bytes_downloaded, content_length);
        }
        goto cleanup_http;
    }
    
    // ========================================================================
    // STEP 8: Set boot partition
    // ========================================================================
    
    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        update_progress(OTA_STATUS_FAILED, OTA_ERR_PARTITION, bytes_downloaded, content_length);
        ota_publish_status("failed", 0, bytes_downloaded, content_length);
        goto cleanup_http;
    }
    
    // ========================================================================
    // SUCCESS!
    // ========================================================================
    
    update_progress(OTA_STATUS_SUCCESS, OTA_ERR_NONE, bytes_downloaded, content_length);
    ota_publish_status("success", 100, bytes_downloaded, content_length);
    
    ESP_LOGI(TAG, "============================================");
    ESP_LOGI(TAG, "OTA UPDATE SUCCESSFUL!");
    ESP_LOGI(TAG, "Firmware size: %lu bytes", bytes_downloaded);
    ESP_LOGI(TAG, "SHA256: %s", sha256_hex);
    ESP_LOGI(TAG, "Rebooting in %d seconds...", OTA_REBOOT_DELAY_MS / 1000);
    ESP_LOGI(TAG, "============================================");
    
    // Notify completion callback
    if (s_completion_callback) {
        s_completion_callback(true, OTA_ERR_NONE);
    }
    
    // Delay then reboot
    vTaskDelay(pdMS_TO_TICKS(OTA_REBOOT_DELAY_MS));
    esp_restart();
    
    // Should never reach here
    
cleanup_http:
    esp_http_client_close(http_client);
    esp_http_client_cleanup(http_client);
    
cleanup:
    mbedtls_sha256_free(&sha256_ctx);
    
    if (buffer) {
        free(buffer);
    }
    
    // Notify completion callback on failure
    if (s_progress.status == OTA_STATUS_FAILED && s_completion_callback) {
        s_completion_callback(false, s_progress.error);
    }
    
    s_ota_task_handle = NULL;
    vTaskDelete(NULL);
}

// ============================================================================
// PUBLIC API IMPLEMENTATION
// ============================================================================

esp_err_t ota_manager_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing OTA Manager");
    
    // Create progress mutex
    s_progress_mutex = xSemaphoreCreateMutex();
    if (!s_progress_mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }
    
    // Initialize progress
    memset(&s_progress, 0, sizeof(s_progress));
    s_progress.status = OTA_STATUS_IDLE;
    
    // Check for pending rollback
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            ESP_LOGW(TAG, "Running new firmware - pending verification");
            ESP_LOGW(TAG, "Call ota_mark_valid() after successful system check");
        } else if (ota_state == ESP_OTA_IMG_VALID) {
            ESP_LOGI(TAG, "Running verified firmware");
        }
    }
    
    // Log partition info
    ESP_LOGI(TAG, "Running partition: %s @ 0x%08lx",
             running->label, running->address);
    
    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    if (next) {
        ESP_LOGI(TAG, "Next OTA partition: %s @ 0x%08lx",
                 next->label, next->address);
    }
    
    s_initialized = true;
    ESP_LOGI(TAG, "OTA Manager initialized");
    
    return ESP_OK;
}

esp_err_t ota_start_update(const char *expected_sha256)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "OTA Manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (s_ota_task_handle != NULL) {
        ESP_LOGW(TAG, "OTA already in progress");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Store expected SHA256
    memset(s_expected_sha256, 0, sizeof(s_expected_sha256));
    if (expected_sha256 && strlen(expected_sha256) == OTA_SHA256_HEX_LEN) {
        strncpy(s_expected_sha256, expected_sha256, OTA_SHA256_HEX_LEN);
        ESP_LOGI(TAG, "OTA update requested with SHA256 verification");
    } else {
        ESP_LOGW(TAG, "OTA update requested WITHOUT SHA256 verification");
    }
    
    // Create OTA task
    BaseType_t ret = xTaskCreate(
        ota_download_task,
        "ota_download",
        8192,           // Stack size (TLS needs ~6KB)
        NULL,
        5,              // Priority
        &s_ota_task_handle
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create OTA task");
        return ESP_ERR_NO_MEM;
    }
    
    ESP_LOGI(TAG, "OTA update started");
    return ESP_OK;
}

esp_err_t ota_get_progress(ota_progress_t *progress)
{
    if (!progress) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!s_progress_mutex) {
        memset(progress, 0, sizeof(ota_progress_t));
        return ESP_OK;
    }
    
    if (xSemaphoreTake(s_progress_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memcpy(progress, &s_progress, sizeof(ota_progress_t));
        xSemaphoreGive(s_progress_mutex);
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

bool ota_is_running(void)
{
    return (s_ota_task_handle != NULL);
}

esp_err_t ota_mark_valid(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    
    esp_err_t err = esp_ota_get_state_partition(running, &ota_state);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get partition state: %s", esp_err_to_name(err));
        return err;
    }
    
    if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
        err = esp_ota_mark_app_valid_cancel_rollback();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to mark app valid: %s", esp_err_to_name(err));
            return err;
        }
        ESP_LOGI(TAG, "Firmware marked as VALID - rollback disabled");
    } else {
        ESP_LOGI(TAG, "Firmware already valid (state: %d)", ota_state);
    }
    
    return ESP_OK;
}

void ota_register_completion_callback(ota_completion_cb_t callback)
{
    s_completion_callback = callback;
}

const char* ota_status_to_string(ota_status_t status)
{
    switch (status) {
        case OTA_STATUS_IDLE:        return "IDLE";
        case OTA_STATUS_STARTING:    return "STARTING";
        case OTA_STATUS_DOWNLOADING: return "DOWNLOADING";
        case OTA_STATUS_VERIFYING:   return "VERIFYING";
        case OTA_STATUS_FLASHING:    return "FLASHING";
        case OTA_STATUS_SUCCESS:     return "SUCCESS";
        case OTA_STATUS_FAILED:      return "FAILED";
        case OTA_STATUS_ROLLBACK:    return "ROLLBACK";
        default:                     return "UNKNOWN";
    }
}

const char* ota_error_to_string(ota_error_t error)
{
    switch (error) {
        case OTA_ERR_NONE:            return "None";
        case OTA_ERR_ALREADY_RUNNING: return "Already running";
        case OTA_ERR_HTTP_CONNECT:    return "HTTP connection failed";
        case OTA_ERR_HTTP_RESPONSE:   return "Invalid HTTP response";
        case OTA_ERR_DOWNLOAD:        return "Download error";
        case OTA_ERR_SHA256_MISMATCH: return "SHA256 mismatch";
        case OTA_ERR_FLASH_WRITE:     return "Flash write error";
        case OTA_ERR_PARTITION:       return "Partition error";
        case OTA_ERR_IMAGE_INVALID:   return "Invalid firmware image";
        case OTA_ERR_NO_MEMORY:       return "Memory allocation failed";
        case OTA_ERR_TIMEOUT:         return "Timeout";
        default:                      return "Unknown error";
    }
}
