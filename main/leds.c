#include "include/leds.h"

led_strip_handle_t strip = NULL;
bool blink1 = true;
bool blink2 = true;
int l1 = 0, l2 = 0;

// default breathing color when pads connected
int g = 0;
bool up = true;

/* virtual switch for default led mode */
bool strip_enable;
/* virtual switch for the led misalignment mode */
bool strip_misalignment;
/* virtual switch for the charging mode */
bool strip_charging;

static const char *TAG = "LED STRIP";

void set_strip(uint8_t r, uint8_t g, uint8_t b)  
{
    for (int j = 0; j < N_LEDS; j++) 
    {
        led_strip_set_pixel(strip, j, r, g, b);
    }
    led_strip_refresh(strip);
}

void install_strip(uint8_t pin)
{
    ESP_LOGI(TAG, "Installing LED strip on GPIO %d", pin);
    
    /* LED strip general initialization, according to your LED board design */
    led_strip_config_t strip_config = {
        .strip_gpio_num = pin,              // The GPIO that connected to the LED strip's data line
        .max_leds = N_LEDS,                 // The number of LEDs in the strip
        .led_pixel_format = LED_PIXEL_FORMAT_GRB, // Pixel format of your LED strip
        .led_model = LED_MODEL_WS2812,      // LED strip model
        .flags.invert_out = false,          // whether to invert the output signal
    };

    /* LED strip backend configuration: RMT */
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,        // different clock source can lead to different power consumption
        .resolution_hz = 10 * 1000 * 1000,     // 10MHz resolution
        .flags.with_dma = false,               // DMA feature is available on ESP target like ESP32-S3
    };

    /* LED Strip object handle */
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &strip));
    
    ESP_LOGI(TAG, "LED strip installed successfully");
    
    strip_enable = false;
    strip_misalignment = false;
    strip_charging = false;
    
    // Initial color
    set_strip(0, 100, 100);
}

void connected_leds(TimerHandle_t xTimer)
{
    /* BREATHING GREEN */
    if (strip_enable)
    {
        for (int j = 0; j < N_LEDS; j++)
        {
            led_strip_set_pixel(strip, j, 0, g, g);
        }

        led_strip_refresh(strip);

        switch (g)
        {
            case 100:
                g--;
                up = false;
                break;

            case 0:
                g++;
                up = true;
                break;

            default:
                if(up)
                    g++;
                else
                    g--;
                break;
        }
    }
}

void misaligned_leds(TimerHandle_t xTimer)
{
    if (strip_misalignment)
    {
        if (blink2)
        {
            for (int j = 0; j < N_LEDS; j++) 
                led_strip_set_pixel(strip, j, 200, 100, 0);
            led_strip_refresh(strip);
        } 
        else
        {
            led_strip_clear(strip);
        }

        blink2 = !blink2;
    }
}

void charging_state(TimerHandle_t xTimer)
{
    if (strip_charging)
    {
        for (int j = 0; j < l2; j++) 
            led_strip_set_pixel(strip, j, 0, 205, 0);
        for (int y = l2; y < N_LEDS; y++)
            led_strip_set_pixel(strip, y, 0, 20, 0);
        led_strip_refresh(strip);      
    }

    if (l2 >= N_LEDS)
        l2 = 0;
    else 
        l2++;
}