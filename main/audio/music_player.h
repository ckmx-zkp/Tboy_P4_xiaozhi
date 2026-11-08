#ifndef MUSIC_PLAYER_H
#define MUSIC_PLAYER_H

#include <atomic>
#include <mutex>
#include <string>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

class AudioService;

// HTTP/解码仅在工作任务执行，设备状态切换由 Application 主任务协调。
class MusicPlayer {
public:
    explicit MusicPlayer(AudioService& audio) : audio_(audio) {}
    ~MusicPlayer();
    bool Request(const std::string& url, const std::string& title, std::string& error);
    bool StartPending();
    void Stop();
    bool HasPending();
    bool IsActive();
    bool TakeFinished();
    std::string GetStatus();

private:
    AudioService& audio_;
    std::mutex mutex_;
    std::string url_;
    std::string title_;
    std::string state_ = "idle";
    std::string error_;
    uint32_t request_id_ = 0;
    uint32_t generation_ = 0;
    bool pending_ = false;
    bool active_ = false;
    bool finished_ = false;
    bool worker_busy_ = false;
    TaskHandle_t task_ = nullptr;
    std::atomic<bool> shutdown_{false};
    std::atomic<bool> worker_exited_{false};

    void Worker();
    void Download(uint32_t id, uint32_t generation, const std::string& url);
    bool Cancelled(uint32_t id);
    void SetState(uint32_t id, const char* state, const std::string& error = "");
};

#endif
