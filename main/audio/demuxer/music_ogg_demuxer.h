#ifndef MUSIC_OGG_DEMUXER_H_
#define MUSIC_OGG_DEMUXER_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

class MusicOggDemuxer {
public:
    using PacketCallback = std::function<bool(const uint8_t*, size_t, int)>;
    static constexpr size_t kMaxPacketBytes = 8192;
    static constexpr size_t kMaxTagsBytes = 1024 * 1024;
    static constexpr int kDecodeRate = 24000;
    static constexpr uint64_t kUnknownGranule = UINT64_MAX;

    explicit MusicOggDemuxer(PacketCallback callback);
    bool Feed(const uint8_t* data, size_t size);
    bool Finish();
    const std::string& error() const { return error_; }
    int channels() const { return channels_; }
    int pre_skip() const { return pre_skip_; }
    int output_gain() const { return output_gain_; }
    bool eos() const { return eos_; }
    uint64_t eos_granule() const { return eos_granule_; }
    // 回调发生在页 CRC 校验前；末页裁剪使用此处的 48kHz granule。
    bool page_eos() const { return (header_[5] & 4) != 0; }
    uint64_t page_granule() const { return page_granule_; }
    uint64_t total_samples() const { return total_samples_; }

private:
    enum class State { Header, Lacing, Body };
    enum class Packet { Head, Tags, Audio };
    enum class Tags { Signature, VendorLength, Vendor, Count, CommentLength, Comment, Padding };
    bool Fail(const char* message);
    bool ParseHeader();
    bool CompletePage();
    bool Append(const uint8_t* data, size_t size);
    bool CompletePacket();
    bool TagByte(uint8_t byte);
    int PacketSamples() const;
    void UpdateCrc(const uint8_t* data, size_t size);

    PacketCallback callback_;
    std::string error_;
    std::array<uint8_t, 27> header_{};
    std::array<uint8_t, 255> lacing_{};
    std::array<uint8_t, kMaxPacketBytes> packet_{};
    State state_ = State::Header;
    Packet packet_kind_ = Packet::Head;
    Tags tags_state_ = Tags::Signature;
    size_t header_used_ = 0;
    size_t lacing_used_ = 0;
    size_t segment_ = 0;
    size_t segment_used_ = 0;
    size_t packet_size_ = 0;
    uint32_t crc_ = 0;
    uint32_t serial_ = 0;
    uint32_t next_sequence_ = 0;
    uint32_t tag_value_ = 0;
    unsigned tag_value_bytes_ = 0;
    uint32_t tag_remaining_ = 0;
    uint32_t comments_remaining_ = 0;
    uint64_t page_granule_ = kUnknownGranule;
    uint64_t last_granule_ = kUnknownGranule;
    uint64_t eos_granule_ = kUnknownGranule;
    uint64_t total_samples_ = 0;
    int channels_ = 0;
    int pre_skip_ = 0;
    int output_gain_ = 0;
    bool started_ = false;
    bool packet_pending_ = false;
    bool audio_seen_ = false;
    bool page_audio_ = false;
    bool eos_ = false;
    bool finished_ = false;
};

#endif
