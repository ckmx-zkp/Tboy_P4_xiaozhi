#include "music_player.h"
#include "audio_service.h"
#include "demuxer/music_ogg_demuxer.h"
#include "board.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <cJSON.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <freertos/idf_additions.h>

#define TAG "MusicPlayer"

MusicPlayer::~MusicPlayer() {
    Stop();
    shutdown_ = true;
    if (task_ != nullptr) {
        // HTTP 和输出队列均有超时，不在其他线程关闭它们。
        while (!worker_exited_) {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
}

bool MusicPlayer::Request(const std::string& url, const std::string& title, std::string& error) {
    const auto scheme = url.find("://");
    if (url.size() > 4096 || scheme == std::string::npos ||
        (url.substr(0, scheme) != "http" && url.substr(0, scheme) != "https") ||
        std::any_of(url.begin(), url.end(), [](unsigned char ch) { return ch <= 0x20 || ch == 0x7f; })) {
        error = "需要有效的 HTTP(S) Ogg/Opus 音频 URL";
        return false;
    }
    const auto host_end = url.find_first_of("/?#", scheme + 3);
    const auto authority = url.substr(scheme + 3, host_end - (scheme + 3));
    if (authority.empty() || authority.find('@') != std::string::npos || title.size() > 256) {
        error = "音频 URL 或歌曲标题无效";
        return false;
    }
    if (Board::GetInstance().GetAudioCodec()->output_sample_rate() != 24000) {
        error = "当前播放器要求 24kHz 输出板型";
        return false;
    }
    Stop();
    std::lock_guard<std::mutex> lock(mutex_);
    if (task_ == nullptr) {
        if (xTaskCreateWithCaps([](void* arg) {
            auto* self = static_cast<MusicPlayer*>(arg);
            self->Worker();
            self->worker_exited_ = true;
            vTaskDeleteWithCaps(nullptr);
        }, "music_player", 24576, this, 2, &task_, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
            error = "无法创建音乐播放任务";
            state_ = "error";
            error_ = error;
            return false;
        }
    }
    url_ = url;
    title_ = title;
    pending_ = true;
    finished_ = false;
    state_ = "pending";
    error_.clear();
    return true;
}

bool MusicPlayer::StartPending() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!pending_ || worker_busy_) {
        return false;
    }
    generation_ = audio_.BeginMusicPlayback();
    pending_ = false;
    active_ = true;
    state_ = "loading";
    return true;
}

void MusicPlayer::Stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    ++request_id_;
    if (active_) {
        audio_.EndMusicPlayback(generation_);
    }
    pending_ = false;
    active_ = false;
    finished_ = false;
    state_ = "stopped";
    url_.clear();
}

bool MusicPlayer::HasPending() {
    std::lock_guard<std::mutex> lock(mutex_);
    return pending_;
}

bool MusicPlayer::IsActive() {
    std::lock_guard<std::mutex> lock(mutex_);
    return active_;
}

bool MusicPlayer::TakeFinished() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!finished_) {
        return false;
    }
    finished_ = false;
    return true;
}

std::string MusicPlayer::GetStatus() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto* json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "state", state_.c_str());
    cJSON_AddStringToObject(json, "title", title_.c_str());
    cJSON_AddStringToObject(json, "error", error_.c_str());
    cJSON_AddBoolToObject(json, "active", active_ || pending_);
    // 签名 URL 不进入 MCP 回包或日志。
    auto* text = cJSON_PrintUnformatted(json);
    std::string result = text ? text : "{}";
    cJSON_free(text);
    cJSON_Delete(json);
    return result;
}

bool MusicPlayer::Cancelled(uint32_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return shutdown_ || id != request_id_ || !active_;
}

void MusicPlayer::SetState(uint32_t id, const char* state, const std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (id != request_id_) {
        return;
    }
    if (state_ != state) {
        ESP_LOGI(TAG, "state=%s", state);
    }
    state_ = state;
    error_ = error;
    if (!error.empty()) {
        ESP_LOGW(TAG, "%s", error.c_str());
    }
}

void MusicPlayer::Worker() {
    while (!shutdown_) {
        uint32_t id = 0;
        uint32_t generation = 0;
        std::string url;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (active_ && !worker_busy_) {
                worker_busy_ = true;
                id = request_id_;
                generation = generation_;
                url = url_;
            }
        }
        if (url.empty()) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        Download(id, generation, url);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            worker_busy_ = false;
            if (id == request_id_) {
                audio_.EndMusicPlayback(generation);
                active_ = false;
                finished_ = true;
                url_.clear();
            }
        }
    }
}

void MusicPlayer::Download(uint32_t id, uint32_t generation, const std::string& url) {
    auto http = Board::GetInstance().GetNetwork()->CreateHttp(4);
    if (!http) {
        SetState(id, "error", "无法创建音乐 HTTP 连接");
        return;
    }
    http->SetTimeout(10000);
    http->SetKeepAlive(false);
    http->SetHeader("Accept", "audio/ogg, application/ogg");
    if (!http->Open("GET", url) || http->GetStatusCode() != 200) {
        SetState(id, "error", "音乐 HTTP 请求失败，状态码 " + std::to_string(http->GetStatusCode()));
        http->Close();
        return;
    }
    if (Cancelled(id)) {
        http->Close();
        return;
    }

    void* decoder = nullptr;
    auto decoder_guard = std::unique_ptr<void, decltype(&esp_opus_dec_close)>(nullptr, esp_opus_dec_close);
    std::vector<int16_t> decode_buffer(2880 * 2); // 最长 120ms、双声道，固定 24kHz。
    uint64_t decoded_samples = 0;
    uint64_t packets = 0;
    bool playing = false;
    std::string failure;
    MusicOggDemuxer* parser_ptr = nullptr;
    auto parser = std::make_unique<MusicOggDemuxer>([&](const uint8_t* bytes, size_t len, int samples) {
        if (Cancelled(id)) {
            return false;
        }
        if (!decoder) {
            esp_opus_dec_cfg_t cfg = {};
            cfg.sample_rate = 24000;
            cfg.channel = parser_ptr->channels();
            cfg.frame_duration = ESP_OPUS_DEC_FRAME_DURATION_120_MS;
            cfg.self_delimited = false;
            if (esp_opus_dec_open(&cfg, sizeof(cfg), &decoder) != ESP_AUDIO_ERR_OK || !decoder) {
                failure = "音乐 Opus 解码器初始化失败";
                return false;
            }
            decoder_guard.reset(decoder);
            audio_.PreparePlayback();
            vTaskDelay(pdMS_TO_TICKS(120)); // NS4150B 上电稳定，不阻塞主任务。
        }
        esp_audio_dec_in_raw_t raw = {};
        raw.buffer = const_cast<uint8_t*>(bytes);
        raw.len = len;
        esp_audio_dec_out_frame_t out = {};
        out.buffer = reinterpret_cast<uint8_t*>(decode_buffer.data());
        out.len = decode_buffer.size() * sizeof(int16_t);
        esp_audio_dec_info_t info = {};
        if (esp_opus_dec_decode(decoder, &raw, &out, &info) != ESP_AUDIO_ERR_OK ||
            out.decoded_size != samples * parser_ptr->channels() * sizeof(int16_t)) {
            failure = "音乐 Opus 包解码失败或帧长度不符";
            return false;
        }
        const uint64_t begin = decoded_samples;
        decoded_samples += samples;
        // Opus pre-skip 与末页 granule 均以 48kHz 计数。
        const uint64_t first = std::max(begin, static_cast<uint64_t>((parser_ptr->pre_skip() + 1) / 2));
        uint64_t last = decoded_samples;
        if (parser_ptr->page_eos()) {
            last = std::min(last, parser_ptr->page_granule() / 2);
        }
        const float gain = std::pow(10.0f, parser_ptr->output_gain() / (256.0f * 20.0f));
        for (uint64_t pos = first; pos < last;) {
            const auto count = static_cast<size_t>(std::min<uint64_t>(1440, last - pos));
            std::vector<int16_t> pcm(count);
            for (size_t i = 0; i < count; ++i) {
                const size_t index = (pos - begin + i) * parser_ptr->channels();
                int32_t value = decode_buffer[index];
                if (parser_ptr->channels() == 2) {
                    value = (value + decode_buffer[index + 1]) / 2;
                }
                pcm[i] = static_cast<int16_t>(std::clamp(value * gain, -32768.0f, 32767.0f));
            }
            if (Cancelled(id) || !audio_.PushMusicPcm(std::move(pcm), generation)) {
                failure = "音乐播放队列已取消或超时";
                return false;
            }
            pos += count;
            if (!playing && audio_.PlayedMusicFrames(generation) > 0) {
                playing = true;
                SetState(id, "playing");
            }
        }
        ++packets;
        return true;
    });
    parser_ptr = parser.get();
    std::vector<char> buffer(4096);
    size_t bytes_read = 0;
    const size_t expected = http->GetBodyLength();
    const auto started = std::chrono::steady_clock::now();
    while (!Cancelled(id)) {
        const int count = http->Read(buffer.data(), buffer.size());
        if (count < 0) {
            failure = "音乐网络读取失败或超时";
            break;
        }
        if (count == 0) {
            if (expected > 0 && bytes_read != expected) {
                failure = "音乐 HTTP 流提前结束";
            }
            break;
        }
        bytes_read += count;
        if (bytes_read > 64 * 1024 * 1024 ||
            std::chrono::steady_clock::now() - started > std::chrono::minutes(30)) {
            failure = "音乐超过 64MB/30 分钟播放上限";
            break;
        }
        if (!parser->Feed(reinterpret_cast<const uint8_t*>(buffer.data()), count)) {
            if (failure.empty()) {
                failure = parser->error();
            }
            break;
        }
    }
    http->Close();
    if (Cancelled(id)) {
        return;
    }
    if (failure.empty() && (!parser->Finish() || packets == 0)) {
        failure = packets == 0 ? "音频不是可播放的 Ogg/Opus 流" : parser->error();
    }
    if (!failure.empty()) {
        SetState(id, "error", failure);
        return;
    }
    const auto drain_start = std::chrono::steady_clock::now();
    while (!Cancelled(id) && !audio_.IsPlaybackDrained()) {
        if (std::chrono::steady_clock::now() - drain_start > std::chrono::seconds(5)) {
            SetState(id, "error", "音乐播放结束等待超时");
            return;
        }
        if (!playing && audio_.PlayedMusicFrames(generation) > 0) {
            playing = true;
            SetState(id, "playing");
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    // 队列消费后，仍为 I2S DMA 保留尾帧时间（当前 DMA 6×240@24kHz）。
    vTaskDelay(pdMS_TO_TICKS(80));
    if (!Cancelled(id)) {
        SetState(id, "completed");
    }
}
