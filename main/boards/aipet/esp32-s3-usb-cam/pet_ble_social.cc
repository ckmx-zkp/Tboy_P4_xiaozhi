#include "pet_ble_social.h"
#if CONFIG_BT_NIMBLE_ENABLED
#include "settings.h"
#include "application.h"
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <cstring>
#include <cstdio>
extern "C" {
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_hs_adv.h"
}

static const char* TAG = "PetBleSocial";
static PetBleSocial* instance = nullptr;
static const uint8_t kUuid[16] = {0x71,0x9e,0x85,0xa1,0x17,0x11,0x26,0x20,0xa5,0x5a,0x70,0x65,0x74,0x00,0x01,0x01};

static int GapEvent(ble_gap_event* event, void*) {
    if (event->type == BLE_GAP_EVENT_DISC && instance) {
        ble_hs_adv_fields fields{};
        if (ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data) == 0 &&
            fields.svc_data_uuid128 && fields.svc_data_uuid128_len == 26) {
            instance->OnPacket(fields.svc_data_uuid128, fields.svc_data_uuid128_len, event->disc.rssi);
        }
    }
    return 0;
}

void PetBleSocial::Start(std::function<void(bool, const std::string&)> callback) {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    const uint8_t com7[6] = {0xd8,0x85,0xac,0xbb,0xb0,0x80};
    const uint8_t com25[6] = {0xd8,0x85,0xac,0xba,0x85,0xd0};
    role_ = memcmp(mac, com7, 6) == 0 ? 1 : memcmp(mac, com25, 6) == 0 ? 2 : 0;
    if (!role_) { ESP_LOGW(TAG, "Not a BLE social test fixture; disabled"); return; }
    on_session_ = std::move(callback);
    instance = this;
    ESP_ERROR_CHECK(nimble_port_init());
    ble_hs_cfg.sync_cb = []() { instance->OnSync(); };
    nimble_port_freertos_init([](void*) { nimble_port_run(); nimble_port_freertos_deinit(); });
    xTaskCreate([](void* arg) { static_cast<PetBleSocial*>(arg)->Loop(); }, "pet_ble", 4096, this, 3, nullptr);
    ESP_LOGI(TAG, "Test role=%d; touch enabled only on COM7", role_);
}

void PetBleSocial::OnSync() {
    int rc = ble_hs_id_infer_auto(0, &address_type_);
    if (rc) { ESP_LOGE(TAG, "BLE address rc=%d", rc); return; }
    synced_.store(true);
    ble_gap_disc_params scan{};
    scan.passive = 1;
    scan.itvl = 256;
    scan.window = 48;
    scan.filter_duplicates = 0;
    rc = ble_gap_disc(address_type_, BLE_HS_FOREVER, &scan, GapEvent, nullptr);
    ESP_LOGI(TAG, "BLE scan started rc=%d", rc);
    std::lock_guard<std::mutex> lock(mutex_);
    adv_dirty_ = true;
}

void PetBleSocial::OnPacket(const uint8_t* data, size_t size, int rssi) {
    if (size != 26 || memcmp(data, kUuid, 16) || data[16] != 1 ||
        data[17] != 3 - role_ || rssi < -78) return;
    const int64_t now = esp_timer_get_time();
    std::lock_guard<std::mutex> lock(mutex_);
    if (now - last_seen_ > 6000000) {
        ESP_LOGI(TAG, "Nearby peer=%s rssi=%d", role_ == 1 ? "wuxing" : "COM7", rssi);
        seen_pending_ = true;
    }
    last_seen_ = now;
    uint32_t nonce;
    memcpy(&nonce, data + 18, 4);
    if (data[22] != role_ || !nonce) return;
    const auto state = Application::GetInstance().GetDeviceState();
    const bool idle = state == kDeviceStateIdle || state == kDeviceStateListening;
    if (role_ == 2 && data[23] == 1 && nonce != last_started_ && idle && now >= cooldown_) {
        nonce_ = nonce;
        state_ = 2;
        expires_ = now + 20000000;
        adv_dirty_ = true;
        last_started_ = nonce;
        cooldown_ = now + 180000000;
        begin_pending_ = true;
    } else if (role_ == 1 && state_ == 1 && data[23] == 2 && nonce == nonce_ && now < expires_) {
        last_started_ = nonce;
        cooldown_ = now + 180000000;
        state_ = 2;
        adv_dirty_ = true;
        begin_pending_ = true;
    }
}

void PetBleSocial::Touch() {
    const int64_t now = esp_timer_get_time();
    const auto state = Application::GetInstance().GetDeviceState();
    std::lock_guard<std::mutex> lock(mutex_);
    if (role_ != 1 || !synced_ || !last_seen_ || now - last_seen_ > 6000000 ||
        now < cooldown_ || state_ != 0 ||
        (state != kDeviceStateIdle && state != kDeviceStateListening)) return;
    nonce_ = esp_random();
    if (!nonce_) nonce_ = 1;
    state_ = 1;
    expires_ = now + 20000000;
    adv_dirty_ = true;
    ESP_LOGI(TAG, "Touch invitation nonce=%08lx", static_cast<unsigned long>(nonce_));
}

void PetBleSocial::Advertise() {
    uint8_t payload[26]{};
    memcpy(payload, kUuid, 16);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        payload[16] = 1; payload[17] = role_;
        memcpy(payload + 18, &nonce_, 4);
        payload[22] = state_ ? 3 - role_ : 0;
        payload[23] = state_;
    }
    ble_gap_adv_stop();
    ble_hs_adv_fields fields{};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.svc_data_uuid128 = payload;
    fields.svc_data_uuid128_len = sizeof(payload);
    int rc = ble_gap_adv_set_fields(&fields);
    if (!rc) {
        ble_gap_adv_params params{};
        params.conn_mode = BLE_GAP_CONN_MODE_NON;
        params.disc_mode = BLE_GAP_DISC_MODE_GEN;
        params.itvl_min = 320; params.itvl_max = 480;
        rc = ble_gap_adv_start(address_type_, nullptr, BLE_HS_FOREVER, &params, GapEvent, nullptr);
    }
    ESP_LOGI(TAG, "BLE advertising rc=%d role=%d", rc, role_);
}

void PetBleSocial::Loop() {
    while (true) {
        bool dirty = false, seen = false, begin = false;
        uint32_t nonce = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (state_ && esp_timer_get_time() > expires_) {
                state_ = 0; nonce_ = 0; adv_dirty_ = true;
                ESP_LOGI(TAG, "BLE invitation window ended");
            }
            dirty = adv_dirty_; adv_dirty_ = false;
            seen = seen_pending_; seen_pending_ = false;
            begin = begin_pending_; begin_pending_ = false;
            nonce = nonce_;
        }
        if (seen) {
            Settings settings("pet_social", true);
            const char* key = role_ == 1 ? "wuxing_seen" : "com7_seen";
            if (!settings.GetBool(key)) {
                settings.SetBool(key, true);
                ESP_LOGI(TAG, "New toy recorded in NVS: %s", key);
            } else ESP_LOGI(TAG, "Known toy seen again: %s", key);
        }
        if (dirty && synced_) Advertise();
        if (begin && on_session_) {
            char id[9]; snprintf(id, sizeof(id), "%08lx", static_cast<unsigned long>(nonce));
            on_session_(role_ == 1, id);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

#endif
