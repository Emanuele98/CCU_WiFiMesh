#ifndef _LEDS_H_
#define _LEDS_H_

#include <util.h>
#include "led_strip.h"  // New built-in component
#include "aux_ctu_hw.h"

/* virtual switch for default led mode */
extern bool strip_enable;
/* virtual switch for the led misalignment mode */
extern bool strip_misalignment;
/* virtual switch for the charging mode */
extern bool strip_charging;

// LED STRIP
#define N_LEDS                  25
#define STRIP_PIN               15

/* Default Leds connected blinking duration */
#define CONNECTED_LEDS_TIMER_PERIOD pdMS_TO_TICKS(40)

/* Leds misaligned blinking duration */
#define MISALIGNED_LEDS_TIMER_PERIOD pdMS_TO_TICKS(300)

/* Leds charging timer duration*/
#define CHARGING_LEDS_TIMER_PERIOD pdMS_TO_TICKS(200)

// External strip handle
extern led_strip_handle_t strip;

/**
 * @brief Construct a new install strip object
 * 
 * @param pin assigned GPIO pin
 */
void install_strip(uint8_t pin);

/**
 * @brief Function used by the timer for the default connected led state
 */
void connected_leds(TimerHandle_t xTimer);

/**
 * @brief Function used for orange blinking when the scooter is misaligned 
 */
void misaligned_leds(TimerHandle_t xTimer);

/**
 * @brief Function used for charging animation
 */
void charging_state(TimerHandle_t xTimer);

/**
 * @brief Set the led strip with one defined colour
 */
void set_strip(uint8_t r, uint8_t g, uint8_t b);

#endif /* _LEDS_H_ */