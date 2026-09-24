#include "wifi_board.h"
#include "codecs/box_audio_codec.h"
#include "display/display.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "i2c_device.h"

#include <esp_log.h>
#include <driver/i2c_master.h>

#define TAG "Esp32S3V101"

class Pca9557 : public I2cDevice {
public:
    Pca9557(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : I2cDevice(i2c_bus, addr) {
        // IO0..IO2 输出。IO0 是 LCD_CS，保持高。IO1 是 PA_EN，先关。
        WriteReg(0x01, 0x01);
        WriteReg(0x03, 0xf8);
    }

    void SetOutputState(uint8_t bit, uint8_t level) {
        uint8_t data = ReadReg(0x01);
        data = (data & ~(1 << bit)) | (level << bit);
        WriteReg(0x01, data);
    }
};

class V101AudioCodec : public BoxAudioCodec {
private:
    Pca9557* pca9557_;

    void SetPa(bool on) {
        pca9557_->SetOutputState(AUDIO_CODEC_PA_IO, on ? 1 : 0);
        ESP_LOGI(TAG, "PCA9557 IO%d PA_EN=%d", AUDIO_CODEC_PA_IO, on ? 1 : 0);
    }

public:
    V101AudioCodec(i2c_master_bus_handle_t i2c_bus, Pca9557* pca9557)
        : BoxAudioCodec(i2c_bus,
                        AUDIO_INPUT_SAMPLE_RATE,
                        AUDIO_OUTPUT_SAMPLE_RATE,
                        AUDIO_I2S_GPIO_MCLK,
                        AUDIO_I2S_GPIO_BCLK,
                        AUDIO_I2S_GPIO_WS,
                        AUDIO_I2S_GPIO_DOUT,
                        AUDIO_I2S_GPIO_DIN,
                        GPIO_NUM_NC,
                        AUDIO_CODEC_ES8311_ADDR,
                        AUDIO_CODEC_ES7210_ADDR,
                        AUDIO_INPUT_REFERENCE),
          pca9557_(pca9557) {
    }

    void EnableOutput(bool enable) override {
        BoxAudioCodec::EnableOutput(enable);
        SetPa(enable);
    }

    void PreparePlayback() override {
        SetPa(true);
    }
};

class Esp32S3V101Board : public WifiBoard {
private:
    i2c_master_bus_handle_t i2c_bus_ = nullptr;
    Pca9557* pca9557_ = nullptr;
    Button boot_button_;

    void InitializeI2c() {
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = (i2c_port_t)1,
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
        pca9557_ = new Pca9557(i2c_bus_, AUDIO_CODEC_PCA9557_ADDR);

        ESP_LOGI(TAG, "I2C1 scan SDA=1 SCL=2 (ES8311 0x18, ES7210 0x41, PCA9557 0x19)");
        const uint8_t addrs[] = {0x18, 0x41, 0x19, 0x38};
        for (uint8_t addr : addrs) {
            esp_err_t err = i2c_master_probe(i2c_bus_, addr, 50);
            ESP_LOGI(TAG, "  I2C %s 0x%02x", err == ESP_OK ? "ack" : "nak", addr);
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

public:
    Esp32S3V101Board() : boot_button_(BOOT_BUTTON_GPIO) {
        ESP_LOGW(TAG, "S3 V1.0.1: audio only, PA_EN = PCA9557 IO1, no LCD");
        InitializeI2c();
        InitializeButtons();
    }

    AudioCodec* GetAudioCodec() override {
        static V101AudioCodec audio_codec(i2c_bus_, pca9557_);
        return &audio_codec;
    }

    Display* GetDisplay() override {
        static NoDisplay display;
        return &display;
    }
};

DECLARE_BOARD(Esp32S3V101Board);
