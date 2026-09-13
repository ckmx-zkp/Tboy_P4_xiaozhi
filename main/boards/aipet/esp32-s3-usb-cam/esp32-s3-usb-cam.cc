#include "wifi_board.h"
#include "codecs/box_audio_codec.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "camera.h"
#include "display/display.h"
#include "backlight.h"
#include "gc9107_boe_099_init.h"
#include "uvc_camera.h"
#include "led/circular_strip.h"
#include "led_mood_controller.h"
#include "dual_eye_display.h"
#include "eye_controller.h"

#include <esp_log.h>
#include <esp_heap_caps.h>
#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_gc9a01.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "Esp32S3UsbCam"

class Esp32S3UsbCamBoard : public WifiBoard {
private:
    UvcCamera camera_;
    i2c_master_bus_handle_t i2c_bus_ = nullptr;
    Button boot_button_;
    CircularStrip* led_strip_ = nullptr;
    LedMoodController* led_mood_ = nullptr;
    DualEyeDisplay* eyes_ = nullptr;
    EyeController* eye_ctrl_ = nullptr;
    esp_lcd_panel_handle_t left_eye_ = nullptr;
    esp_lcd_panel_handle_t right_eye_ = nullptr;

    void InitializeLedPower() {
        gpio_config_t io = {};
        io.pin_bit_mask = 1ULL << WS2812_EN_GPIO;
        io.mode = GPIO_MODE_OUTPUT;
        io.pull_up_en = GPIO_PULLUP_DISABLE;
        io.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io.intr_type = GPIO_INTR_DISABLE;
        gpio_config(&io);
        gpio_set_level(WS2812_EN_GPIO, 1);
        led_strip_ = new CircularStrip(WS2812_DIN_GPIO, WS2812_LED_COUNT);
        led_mood_ = new LedMoodController(led_strip_);
        ESP_LOGI(TAG, "WS2812 x%d DIN=GPIO%d EN=GPIO%d; MCP self.led.set_zodiac/set_emotion/off",
                 WS2812_LED_COUNT, WS2812_DIN_GPIO, WS2812_EN_GPIO);
    }

    void InitializeCodecI2c() {
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_1,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));
        ScanCodecI2c();
    }

    void ScanCodecI2c() {
        ESP_LOGI(TAG, "I2C1 scan SDA=%d SCL=%d (expect ES8311 0x18, ES7210 0x41=8bit 0x82)",
                 AUDIO_CODEC_I2C_SDA_PIN, AUDIO_CODEC_I2C_SCL_PIN);
        int found = 0;
        for (uint8_t addr = 0x08; addr < 0x78; ++addr) {
            esp_err_t err = i2c_master_probe(i2c_bus_, addr, 50);
            if (err == ESP_OK) {
                ++found;
                ESP_LOGI(TAG, "  I2C ack 0x%02X%s", addr,
                         addr == 0x18 ? " ES8311" : addr == 0x41 ? " ES7210" : "");
            }
        }
        if (found == 0) {
            ESP_LOGE(TAG, "I2C1 empty: codec 3V3 / SDA=GPIO1 / SCL=GPIO2 not responding");
        }
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });
    }

    bool InitializeEyeSpi() {
        ESP_LOGI(TAG, "eye SPI SCLK=GPIO%d (FPC SCL) MOSI=GPIO%d (FPC SDA) DC=GPIO%d",
                 DISPLAY_SPI_SCLK_PIN, DISPLAY_SPI_MOSI_PIN, DISPLAY_SPI_DC_PIN);
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_SPI_MOSI_PIN;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = DISPLAY_SPI_SCLK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_GRAM_HEIGHT * sizeof(uint16_t);
        esp_err_t err = spi_bus_initialize(DISPLAY_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "eye SPI init failed: %s", esp_err_to_name(err));
            return false;
        }
        return true;
    }

    esp_lcd_panel_handle_t CreateGc9107Eye(gpio_num_t cs) {
        esp_lcd_panel_io_handle_t io = nullptr;
        esp_lcd_panel_io_spi_config_t io_cfg = {};
        io_cfg.cs_gpio_num = cs;
        io_cfg.dc_gpio_num = DISPLAY_SPI_DC_PIN;
        io_cfg.spi_mode = 0;
        io_cfg.pclk_hz = DISPLAY_SPI_SCLK_HZ;
        io_cfg.trans_queue_depth = 8;
        io_cfg.lcd_cmd_bits = 8;
        io_cfg.lcd_param_bits = 8;
        if (esp_lcd_new_panel_io_spi(DISPLAY_SPI_HOST, &io_cfg, &io) != ESP_OK) {
            ESP_LOGE(TAG, "GC9107 IO CS=%d failed", cs);
            return nullptr;
        }

        gc9a01_vendor_config_t vendor = {};
        vendor.init_cmds = kGc9107Boe099InitCmds;
        vendor.init_cmds_size = sizeof(kGc9107Boe099InitCmds) / sizeof(kGc9107Boe099InitCmds[0]);

        esp_lcd_panel_dev_config_t panel_cfg = {};
        panel_cfg.reset_gpio_num = DISPLAY_SPI_RESET_PIN;
        panel_cfg.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_cfg.bits_per_pixel = 16;
        panel_cfg.vendor_config = &vendor;

        esp_lcd_panel_handle_t panel = nullptr;
        if (esp_lcd_new_panel_gc9a01(io, &panel_cfg, &panel) != ESP_OK) {
            ESP_LOGE(TAG, "GC9107 panel CS=%d create failed", cs);
            return nullptr;
        }
        // RES 已与 EN 短接，上电复位已发生；再等厂方要求的 120ms
        vTaskDelay(pdMS_TO_TICKS(120));
        if (esp_lcd_panel_reset(panel) != ESP_OK ||
            esp_lcd_panel_init(panel) != ESP_OK ||
            esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR) != ESP_OK ||
            esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY) != ESP_OK ||
            esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y) != ESP_OK ||
            ((DISPLAY_OFFSET_X || DISPLAY_OFFSET_Y) &&
             esp_lcd_panel_set_gap(panel, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y) != ESP_OK) ||
            esp_lcd_panel_disp_on_off(panel, true) != ESP_OK) {
            ESP_LOGE(TAG, "GC9107 panel CS=%d init failed", cs);
            return nullptr;
        }
        return panel;
    }

    void InitializeEyes() {
        if (!InitializeEyeSpi()) {
            return;
        }
        left_eye_ = CreateGc9107Eye(DISPLAY_SPI_CS1_PIN);
        right_eye_ = CreateGc9107Eye(DISPLAY_SPI_CS2_PIN);
        if (left_eye_ == nullptr || right_eye_ == nullptr) {
            ESP_LOGE(TAG, "GC9107 eyes missing, skip C1 animation");
            return;
        }
        eyes_ = new DualEyeDisplay(left_eye_, right_eye_, DISPLAY_WIDTH, DISPLAY_HEIGHT);
        eye_ctrl_ = new EyeController(eyes_);
        ESP_LOGI(TAG, "C1 dual eyes ready: right=original left=mirrored");
    }

public:
    Esp32S3UsbCamBoard() : boot_button_(BOOT_BUTTON_GPIO) {
        ESP_LOGW(TAG, "S3 USB-cam bring-up: WiFi+audio+GC9107 eyes+WS2812x2; servo/4G/K230 not started");
        ESP_LOGW(TAG, "PA_EN schematic GPIO46 is input-only; codec PA pin left NC");
        camera_.StartHost();
        InitializeLedPower();
        InitializeCodecI2c();
        InitializeButtons();
        InitializeEyes();
        GetBacklight()->RestoreBrightness();
    }

    Camera* GetCamera() override {
        return &camera_;
    }

    AudioCodec* GetAudioCodec() override {
        static BoxAudioCodec audio_codec(
            i2c_bus_,
            AUDIO_INPUT_SAMPLE_RATE,
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK,
            AUDIO_I2S_GPIO_BCLK,
            AUDIO_I2S_GPIO_WS,
            AUDIO_I2S_GPIO_DOUT,
            AUDIO_I2S_GPIO_DIN,
            AUDIO_CODEC_PA_PIN,
            AUDIO_CODEC_ES8311_ADDR,
            AUDIO_CODEC_ES7210_ADDR,
            AUDIO_INPUT_REFERENCE);
        return &audio_codec;
    }

    Display* GetDisplay() override {
        static NoDisplay no_display;
        return &no_display;
    }

    Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERTED);
        return &backlight;
    }
};

DECLARE_BOARD(Esp32S3UsbCamBoard);
