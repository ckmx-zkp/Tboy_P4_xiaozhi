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
#include "mg90s_servo.h"
#include "face_follow_controller.h"
#include "servo_controller.h"

#include <esp_log.h>
#include <esp_heap_caps.h>
#include <cstdio>
#include <cstring>
#include <driver/gpio.h>
#include <driver/uart.h>
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
    Mg90sServo servo_;
    FaceFollowController* follow_ = nullptr;
    ServoController* servo_ctrl_ = nullptr;
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
        eye_ctrl_ = new EyeController(eyes_, led_mood_);
        ESP_LOGI(TAG, "C1 dual eyes ready: right=original left=mirrored");
    }

    bool OpenUart(uart_port_t port, gpio_num_t tx, gpio_num_t rx, int baud,
                  int rx_buf, const char* name) {
        uart_config_t cfg = {};
        cfg.baud_rate = baud;
        cfg.data_bits = UART_DATA_8_BITS;
        cfg.parity = UART_PARITY_DISABLE;
        cfg.stop_bits = UART_STOP_BITS_1;
        cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
        cfg.source_clk = UART_SCLK_DEFAULT;
        if (uart_driver_install(port, rx_buf, 256, 0, nullptr, 0) != ESP_OK) {
            ESP_LOGE(TAG, "%s UART%d install failed", name, (int)port);
            return false;
        }
        if (uart_param_config(port, &cfg) != ESP_OK ||
            uart_set_pin(port, tx, rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
            ESP_LOGE(TAG, "%s UART%d pin/baud failed TX=GPIO%d RX=GPIO%d",
                     name, (int)port, (int)tx, (int)rx);
            return false;
        }
        ESP_LOGI(TAG, "%s UART%d enabled TX=GPIO%d RX=GPIO%d %d baud",
                 name, (int)port, (int)tx, (int)rx, baud);
        return true;
    }

    void InitializeUarts() {
        // UART0 已是 CH340 调试口。K230 只收自动 JSON，4G 由任务主动发 AT。
        OpenUart(K230_UART_NUM, K230_UART_TX_GPIO, K230_UART_RX_GPIO,
                 K230_UART_BAUD, 2048, "K230");
        OpenUart(ML307_UART_NUM, ML307_UART_TX_GPIO, ML307_UART_RX_GPIO,
                 ML307_UART_BAUD, 1024, "ML307");
        ESP_LOGW(TAG, "ML307 UART is wired 3V3 straight through; schematic still marks 1.8V?");
    }

    void Enable4gPower() {
        gpio_config_t io = {};
        io.pin_bit_mask = 1ULL << ML307_PWR_GPIO;
        io.mode = GPIO_MODE_OUTPUT;
        io.pull_up_en = GPIO_PULLUP_DISABLE;
        io.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io.intr_type = GPIO_INTR_DISABLE;
        gpio_config(&io);
        gpio_set_level(ML307_PWR_GPIO, 1);
        ESP_LOGI(TAG, "4G_PWR GPIO%d=1 (TPS562200 EN, expect +4V VBAT; PWRKEY already GND)",
                 (int)ML307_PWR_GPIO);
    }

    static void LogRaw(const char* tag, const uint8_t* data, int n) {
        char hex[96];
        int p = 0;
        for (int i = 0; i < n && p + 3 < (int)sizeof(hex); ++i) {
            p += snprintf(hex + p, sizeof(hex) - p, "%02X ", data[i]);
        }
        ESP_LOGI(TAG, "ML307 %s %dB [%s] ascii=%.*s", tag, n, hex, n, (const char*)data);
    }

    static int ReadModem(uint8_t* buf, int cap, int timeout_ms) {
        int n = uart_read_bytes(ML307_UART_NUM, buf, cap, pdMS_TO_TICKS(timeout_ms));
        return n < 0 ? 0 : n;
    }

    static bool SendAt(const char* cmd, int timeout_ms) {
        uart_flush_input(ML307_UART_NUM);
        uart_write_bytes(ML307_UART_NUM, cmd, std::strlen(cmd));
        uart_write_bytes(ML307_UART_NUM, "\r\n", 2);
        uint8_t buf[256];
        int n = ReadModem(buf, sizeof(buf) - 1, timeout_ms);
        if (n <= 0) {
            ESP_LOGW(TAG, "ML307 no reply: %s", cmd);
            return false;
        }
        buf[n] = 0;
        LogRaw(cmd, buf, n);
        return std::strstr(reinterpret_cast<char*>(buf), "OK") != nullptr ||
               std::strstr(reinterpret_cast<char*>(buf), "READY") != nullptr;
    }

    static bool ProbeAtBauds(gpio_num_t tx, gpio_num_t rx) {
        const int bauds[] = {115200, 9600, 230400, 460800, 921600};
        uint8_t boot[128];
        int nb = ReadModem(boot, sizeof(boot), 800);
        if (nb > 0) {
            LogRaw("boot-rx", boot, nb);
        }
        for (int baud : bauds) {
            uart_set_baudrate(ML307_UART_NUM, baud);
            ESP_LOGI(TAG, "ML307 AT probe @ %d TX=GPIO%d RX=GPIO%d",
                     baud, (int)tx, (int)rx);
            for (int i = 0; i < 2; ++i) {
                if (SendAt("AT", 600)) {
                    SendAt("ATI", 800);
                    SendAt("AT+CPIN?", 1500);
                    SendAt("AT+CSQ", 800);
                    return true;
                }
            }
        }
        return false;
    }

    static void ProbeMl307Task(void*) {
        ESP_LOGI(TAG, "ML307 wait 8s after 4G_PWR (need SW4 VIN + +4V VBAT)");
        vTaskDelay(pdMS_TO_TICKS(8000));
        bool ok = ProbeAtBauds(ML307_UART_TX_GPIO, ML307_UART_RX_GPIO);
        if (!ok) {
            ESP_LOGW(TAG, "ML307 try swapped TX/RX (bring-up only)");
            uart_set_pin(ML307_UART_NUM, ML307_UART_RX_GPIO, ML307_UART_TX_GPIO,
                         UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
            ok = ProbeAtBauds(ML307_UART_RX_GPIO, ML307_UART_TX_GPIO);
        }
        if (!ok) {
            ESP_LOGE(TAG, "ML307 still silent. Check: SW4 to 9V/VBAT or BAT, U14 +4V, "
                          "schematic 1.8V UART vs S3 3V3 (Vih~2.5V, 1.8V TX may be invisible)");
        }
        vTaskDelete(nullptr);
    }

    void InitializeServo() {
        if (!servo_.Begin(SERVO_PWM_GPIO)) {
            ESP_LOGE(TAG, "MG90S PWM init failed");
            return;
        }
        follow_ = new FaceFollowController(&servo_, eyes_);
        servo_ctrl_ = new ServoController(&servo_, follow_);
        ESP_LOGI(TAG, "MG90S auto-follow on CN2 GPIO8; MCP self.servo.*");
    }

    static void ListenK230Task(void* arg) {
        auto* self = static_cast<Esp32S3UsbCamBoard*>(arg);
        // K230 会自己往 UART1 推 JSON，S3 只收不发；解析后本地跟随。
        char line[768];
        size_t pos = 0;
        int lines = 0;
        uint32_t idle_ticks = 0;
        ESP_LOGI(TAG, "K230 listen-only on UART1 GPIO%d/GPIO%d",
                 (int)K230_UART_TX_GPIO, (int)K230_UART_RX_GPIO);
        while (true) {
            uint8_t ch = 0;
            int n = uart_read_bytes(K230_UART_NUM, &ch, 1, pdMS_TO_TICKS(200));
            if (n <= 0) {
                if (++idle_ticks == 25) {
                    ESP_LOGW(TAG, "K230 no JSON yet (waited ~5s)");
                }
                continue;
            }
            idle_ticks = 0;
            if (ch == '\r') {
                continue;
            }
            if (ch == '\n') {
                if (pos > 0) {
                    line[pos] = 0;
                    ++lines;
                    if (lines <= 20 || (lines % 25) == 0) {
                        ESP_LOGI(TAG, "K230 JSON[%d] %s", lines, line);
                    }
                    if (self->follow_ != nullptr) {
                        self->follow_->OnVisionJson(line);
                    }
                    pos = 0;
                }
                continue;
            }
            if (pos + 1 < sizeof(line)) {
                line[pos++] = static_cast<char>(ch);
            } else {
                line[sizeof(line) - 1] = 0;
                ESP_LOGW(TAG, "K230 line overflow: %s", line);
                pos = 0;
            }
        }
    }

public:
    Esp32S3UsbCamBoard() : boot_button_(BOOT_BUTTON_GPIO) {
        ESP_LOGW(TAG, "S3 USB-cam: WiFi+audio+eyes+WS2812; enable UART1/UART2 + 4G_PWR");
        ESP_LOGW(TAG, "PA_EN schematic GPIO46 is input-only; codec PA pin left NC");
        camera_.StartHost();
        InitializeLedPower();
        InitializeCodecI2c();
        InitializeButtons();
        InitializeEyes();
        InitializeServo();
        GetBacklight()->RestoreBrightness();
        InitializeUarts();
        Enable4gPower();
        xTaskCreate(ListenK230Task, "k230_rx", 6144, this, 3, nullptr);
        xTaskCreate(ProbeMl307Task, "ml307_at", 4096, nullptr, 3, nullptr);
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
