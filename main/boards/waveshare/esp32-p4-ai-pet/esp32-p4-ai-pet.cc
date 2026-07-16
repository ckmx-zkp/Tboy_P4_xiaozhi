#include "wifi_board.h"
#include "codecs/box_audio_codec.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "pet_eye_display.h"
#include "display/display.h"

#include "esp_video.h"
#include "esp_video_init.h"

#include <esp_log.h>
#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_gc9a01.h>

#define TAG "Esp32P4AiPet"

class Esp32P4AiPetBoard : public WifiBoard {
private:
    i2c_master_bus_handle_t i2c_bus_ = nullptr;
    Button boot_button_;
    PetEyeDisplay* display_ = nullptr;
    EspVideo* camera_ = nullptr;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;

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
    }

    void InitializeSpiEye() {
        ESP_LOGI(TAG, "Init SPI eye bus: SCLK=%d MOSI=%d CS=%d DC=%d RST=%d",
                 DISPLAY_SPI_SCLK_PIN, DISPLAY_SPI_MOSI_PIN,
                 DISPLAY_SPI_CS_PIN, DISPLAY_SPI_DC_PIN, DISPLAY_SPI_RESET_PIN);

        spi_bus_config_t buscfg = {};
        buscfg.sclk_io_num = DISPLAY_SPI_SCLK_PIN;
        buscfg.mosi_io_num = DISPLAY_SPI_MOSI_PIN;
        buscfg.miso_io_num = -1;
        buscfg.quadwp_io_num = -1;
        buscfg.quadhd_io_num = -1;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(DISPLAY_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));

        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_SPI_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_SPI_DC_PIN;
        io_config.spi_mode = 0;
        io_config.pclk_hz = DISPLAY_SPI_SCLK_HZ;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)DISPLAY_SPI_HOST, &io_config, &panel_io_));

        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_SPI_RESET_PIN;
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_gc9a01(panel_io_, &panel_config, &panel_));
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel_));
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_, true));
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y));
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

        display_ = new PetEyeDisplay(panel_io_, panel_, DISPLAY_WIDTH, DISPLAY_HEIGHT);
        ESP_LOGI(TAG, "GC9A01 eye panel ready (MIPI 7-inch disabled)");
    }

    void InitializeCamera() {
        esp_video_init_csi_config_t base_csi_config = {
            .sccb_config = {
                .init_sccb = false,
                .i2c_handle = i2c_bus_,
                .freq = 400000,
            },
            .reset_pin = GPIO_NUM_NC,
            .pwdn_pin = GPIO_NUM_NC,
        };
        esp_video_init_config_t cam_config = {
            .csi = &base_csi_config,
        };
        camera_ = new EspVideo(cam_config);
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

public:
    Esp32P4AiPetBoard() : boot_button_(BOOT_BUTTON_GPIO) {
        // No InitializeLCD / InitializeTouch — free MIPI for other use later
        InitializeCodecI2c();
        InitializeSpiEye();
        InitializeCamera();
        InitializeButtons();
        ESP_LOGI(TAG, "AI Pet board: audio+wifi+camera+single eye, chat UI on monitor only");
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
        if (display_ != nullptr) {
            return display_;
        }
        static NoDisplay no_display;
        return &no_display;
    }

    Camera* GetCamera() override {
        return camera_;
    }

    Backlight* GetBacklight() override {
        return nullptr;
    }
};

DECLARE_BOARD(Esp32P4AiPetBoard);
