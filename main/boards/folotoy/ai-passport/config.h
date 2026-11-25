#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>
#include <driver/i2c_types.h>
#include <driver/spi_master.h>
#include <hal/adc_types.h>

// ============================================================================
// FoloToy AI Passport (ESP32-C3, 8MB Flash, No PSRAM)
// Hardware pins synced with FoloToy/ai-passport components/bsp/include/bsp_pins.h
// ============================================================================

// Audio: ES8311 full-duplex I2S0 + shared I2C0
#define AUDIO_INPUT_SAMPLE_RATE   16000
#define AUDIO_OUTPUT_SAMPLE_RATE  16000

#define AUDIO_I2S_GPIO_MCLK       GPIO_NUM_6
#define AUDIO_I2S_GPIO_BCLK       GPIO_NUM_5
#define AUDIO_I2S_GPIO_WS         GPIO_NUM_3
#define AUDIO_I2S_GPIO_DOUT       GPIO_NUM_2
#define AUDIO_I2S_GPIO_DIN        GPIO_NUM_4
#define AUDIO_CODEC_PA_PIN        GPIO_NUM_NC  // Amplifier always enabled

#define BSP_I2C_PORT              I2C_NUM_0
#define AUDIO_CODEC_I2C_SDA_PIN   GPIO_NUM_10
#define AUDIO_CODEC_I2C_SCL_PIN   GPIO_NUM_7
#define AUDIO_CODEC_ES8311_ADDR   (0x18 << 1)  // 7-bit 0x18 -> 8-bit 0x30
#define BSP_I2C_CW2017_ADDR       0x63         // 7-bit fuel gauge address

// Display: ST7789P3 240x320 Portrait, 4-line SPI2 @ 80MHz, mode 0
#define DISPLAY_SPI_HOST          SPI2_HOST
#define DISPLAY_SPI_MOSI_PIN      GPIO_NUM_9
#define DISPLAY_SPI_SCK_PIN       GPIO_NUM_8
#define DISPLAY_SPI_CS_PIN        GPIO_NUM_1
#define DISPLAY_DC_PIN            GPIO_NUM_20
#define DISPLAY_RST_PIN           GPIO_NUM_NC  // Tied to 3.3V -> SWRESET
#define DISPLAY_BACKLIGHT_PIN     GPIO_NUM_21  // Active HIGH LEDC 5kHz/10-bit
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_BACKLIGHT_FREQ_HZ 5000

#define DISPLAY_WIDTH             240
#define DISPLAY_HEIGHT            320
#define DISPLAY_OFFSET_X          0
#define DISPLAY_OFFSET_Y          0
#define DISPLAY_MIRROR_X          false
#define DISPLAY_MIRROR_Y          false
#define DISPLAY_SWAP_XY           false
#define DISPLAY_INVERT_COLOR      true
#define DISPLAY_SCREEN_RADIUS     30

// Buttons: 3-key ADC resistor ladder on GPIO0 (ADC1_CH0), external 10k pull-up
//   UP   : 0 ohm   ->   0 mV  -> [  0,  150)
//   DOWN : 1k ohm  -> 300 mV  -> [150,  447)
//   OK   : 2.2k    -> 595 mV  -> [447, 1900)
//   OPEN : open    -> 3300 mV -> >= 1900
#define BSP_BTN_GPIO              GPIO_NUM_0
#define BSP_BTN_ADC_UNIT          ADC_UNIT_1
#define BSP_BTN_ADC_CHANNEL       ADC_CHANNEL_0
#define BSP_BTN_COUNT             3
#define BSP_BTN_SHORT_PRESS_MS    180
#define BSP_BTN_LONG_PRESS_MS     500
#define BSP_BTN_CONFIRM_MS        40
#define BSP_BTN_HOLD_MS           120
#define BSP_BTN_MV_UP_MIN         0
#define BSP_BTN_MV_UP_MAX         150
#define BSP_BTN_MV_DOWN_MIN       150
#define BSP_BTN_MV_DOWN_MAX       447
#define BSP_BTN_MV_OK_MIN         447
#define BSP_BTN_MV_OK_MAX         1900

#define BUILTIN_LED_GPIO          GPIO_NUM_NC

#endif  // _BOARD_CONFIG_H_
