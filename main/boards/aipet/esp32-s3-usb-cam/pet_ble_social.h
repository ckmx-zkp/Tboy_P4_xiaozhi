#pragma once
#include "sdkconfig.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <cstdint>

// 本轮仅两个白名单测试身份；不作为正式名片认证协议。
#if CONFIG_BT_NIMBLE_ENABLED
class PetBleSocial {
public:
    void Start(std::function<void(bool, const std::string&)> on_session);
    void Touch();
    void OnPacket(const uint8_t* data, size_t size, int rssi);
    void OnSync();
private:
    uint8_t role_ = 0;
    uint8_t address_type_ = 0;
    uint8_t state_ = 0;
    uint32_t nonce_ = 0;
    int64_t last_seen_ = 0;
    int64_t expires_ = 0;
    int64_t cooldown_ = 0;
    uint32_t last_started_ = 0;
    bool seen_pending_ = false;
    bool adv_dirty_ = false;
    bool begin_pending_ = false;
    std::atomic<bool> synced_{false};
    std::mutex mutex_;
    std::function<void(bool, const std::string&)> on_session_;
    void Loop();
    void Advertise();
};

#else
class PetBleSocial {
public:
    void Start(std::function<void(bool, const std::string&)>) {}
    void Touch() {}
};
#endif
