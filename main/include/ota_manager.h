/**
 * @file ota_manager.h
 * @brief Bumblebee OTA (Over-The-Air) Firmware Update Manager
 * 
 * Provides HTTPS-based firmware updates with SHA256 verification.
 * Integrates with existing MQTT client for update triggers.
 * 
 * Features:
 * - HTTPS download with TLS (reuses MQTT CA certificate)
 * - SHA256 integrity verification
 * - Progress reporting
 * - Automatic rollback support
 * - Non-blocking download task
 */

#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include "esp_err.h"
#include "esp_ota_ops.h"
#include <stdbool.h>
#include <stdint.h>

#include <string.h>
#include <ctype.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_app_format.h"
#include "esp_system.h"

#include "mbedtls/sha256.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// CONFIGURATION
// ============================================================================

/** OTA firmware URL - served by Nginx on AWS */
//#define OTA_FIRMWARE_URL        "https://15.188.29.195/ota/firmware.bin"
#define OTA_FIRMWARE_URL        "https://10.218.18.6/ota/firmware.bin"

/** MQTT topic for OTA commands */
#define OTA_MQTT_TOPIC          "bumblebee/ota/start"

/** Download buffer size (bytes) */
#define OTA_BUF_SIZE            4096

/** HTTP timeout (milliseconds) */
#define OTA_HTTP_TIMEOUT_MS     60000

/** Delay before reboot after successful OTA (milliseconds) */
#define OTA_REBOOT_DELAY_MS     3000

/** SHA256 hash length in hex characters */
#define OTA_SHA256_HEX_LEN      64

// ============================================================================
// STATUS & PROGRESS
// ============================================================================

/**
 * @brief OTA update status codes
 */
typedef enum {
    OTA_STATUS_IDLE = 0,           /**< No OTA in progress */
    OTA_STATUS_STARTING,           /**< OTA process starting */
    OTA_STATUS_DOWNLOADING,        /**< Firmware download in progress */
    OTA_STATUS_VERIFYING,          /**< SHA256 verification */
    OTA_STATUS_FLASHING,           /**< Writing to flash */
    OTA_STATUS_SUCCESS,            /**< OTA completed successfully */
    OTA_STATUS_FAILED,             /**< OTA failed */
    OTA_STATUS_ROLLBACK            /**< Rolled back to previous firmware */
} ota_status_t;

/**
 * @brief OTA error codes
 */
typedef enum {
    OTA_ERR_NONE = 0,              /**< No error */
    OTA_ERR_ALREADY_RUNNING,       /**< OTA already in progress */
    OTA_ERR_HTTP_CONNECT,          /**< HTTP connection failed */
    OTA_ERR_HTTP_RESPONSE,         /**< Invalid HTTP response */
    OTA_ERR_DOWNLOAD,              /**< Download error */
    OTA_ERR_SHA256_MISMATCH,       /**< SHA256 verification failed */
    OTA_ERR_FLASH_WRITE,           /**< Flash write error */
    OTA_ERR_PARTITION,             /**< Partition error */
    OTA_ERR_IMAGE_INVALID,         /**< Invalid firmware image */
    OTA_ERR_NO_MEMORY,             /**< Memory allocation failed */
    OTA_ERR_TIMEOUT                /**< Operation timeout */
} ota_error_t;

/**
 * @brief OTA progress information
 */
typedef struct {
    ota_status_t status;           /**< Current status */
    ota_error_t error;             /**< Error code (if status == FAILED) */
    uint32_t bytes_downloaded;     /**< Bytes downloaded so far */
    uint32_t total_bytes;          /**< Total firmware size (0 if unknown) */
    uint8_t progress_percent;      /**< Download progress (0-100) */
    char version[32];              /**< New firmware version (if available) */
} ota_progress_t;

/**
 * @brief Callback for OTA completion (used for mesh coordination)
 */
typedef void (*ota_completion_cb_t)(bool success, ota_error_t error);

// ============================================================================
// PUBLIC API
// ============================================================================

/**
 * @brief Initialize OTA manager
 * 
 * Checks for pending rollback from previous failed update.
 * Should be called once during system initialization.
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t ota_manager_init(void);

/**
 * @brief Start OTA firmware update
 * 
 * Begins non-blocking firmware download and update process.
 * Progress can be monitored via ota_get_progress().
 * 
 * @param expected_sha256 Expected SHA256 hash (64 hex chars), or NULL to skip verification
 * @return ESP_OK if update started, ESP_ERR_INVALID_STATE if already running
 */
esp_err_t ota_start_update(const char *expected_sha256);

/**
 * @brief Get current OTA progress
 * 
 * Thread-safe function to get current update status.
 * 
 * @param[out] progress Pointer to progress structure to fill
 * @return ESP_OK on success
 */
esp_err_t ota_get_progress(ota_progress_t *progress);

/**
 * @brief Check if OTA is currently in progress
 * 
 * @return true if OTA is running, false otherwise
 */
bool ota_is_running(void);

/**
 * @brief Mark current firmware as valid
 * 
 * Call this after successful boot to prevent automatic rollback.
 * Should be called after system verification (e.g., MQTT connected).
 * 
 * @return ESP_OK on success
 */
esp_err_t ota_mark_valid(void);

/**
 * @brief Register callback for OTA completion
 * 
 * Used for mesh coordination - ROOT can be notified when child completes.
 * 
 * @param callback Function to call when OTA completes
 */
void ota_register_completion_callback(ota_completion_cb_t callback);

/**
 * @brief Get string representation of OTA status
 * 
 * @param status Status code
 * @return Human-readable status string
 */
const char* ota_status_to_string(ota_status_t status);

/**
 * @brief Get string representation of OTA error
 * 
 * @param error Error code
 * @return Human-readable error string
 */
const char* ota_error_to_string(ota_error_t error);

// ============================================================================
// MESH COORDINATION (Phase 2)
// ============================================================================

/**
 * @brief OTA command for mesh coordination
 */
typedef struct {
    uint8_t target_mac[6];         /**< Target node MAC (or broadcast) */
    char sha256[OTA_SHA256_HEX_LEN + 1];  /**< Expected SHA256 */
    uint8_t command;               /**< 0=start, 1=status, 2=abort */
} ota_mesh_command_t;

#ifdef __cplusplus
}
#endif

#endif /* OTA_MANAGER_H */
