#include "music_ogg_demuxer.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>

namespace {
uint32_t Le32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
           (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
uint64_t Le64(const uint8_t* p) {
    return uint64_t(Le32(p)) | (uint64_t(Le32(p + 4)) << 32);
}
bool FrameSize(const uint8_t* p, size_t end, size_t& offset, size_t& length) {
    if (offset >= end) return false;
    length = p[offset++];
    if (length >= 252) {
        if (offset >= end) return false;
        length += size_t(p[offset++]) * 4;
    }
    return true;
}
}

MusicOggDemuxer::MusicOggDemuxer(PacketCallback callback)
    : callback_(std::move(callback)) {}

bool MusicOggDemuxer::Fail(const char* message) {
    if (error_.empty()) error_ = message;
    return false;
}

void MusicOggDemuxer::UpdateCrc(const uint8_t* data, size_t size) {
    // Ogg 使用非反射 CRC-32，初始值和最终异或均为 0。
    for (size_t i = 0; i < size; ++i) {
        crc_ ^= uint32_t(data[i]) << 24;
        for (int bit = 0; bit < 8; ++bit)
            crc_ = (crc_ << 1) ^ ((crc_ & 0x80000000u) ? 0x04c11db7u : 0u);
    }
}

bool MusicOggDemuxer::ParseHeader() {
    if (std::memcmp(header_.data(), "OggS", 4) != 0) return Fail("Invalid Ogg capture pattern");
    if (header_[4] != 0) return Fail("Unsupported Ogg version");
    if (header_[5] & 0xf8) return Fail("Invalid Ogg header flags");
    const uint32_t serial = Le32(header_.data() + 14);
    const uint32_t sequence = Le32(header_.data() + 18);
    const bool continued = (header_[5] & 1) != 0;
    if (!started_) {
        if (!(header_[5] & 2) || continued || sequence != 0)
            return Fail("Missing initial Ogg BOS page");
        serial_ = serial;
        started_ = true;
    } else if ((header_[5] & 2) || serial != serial_) {
        return Fail("Chained or multiplexed Ogg stream is unsupported");
    }
    if (sequence != next_sequence_) return Fail("Ogg page sequence mismatch");
    ++next_sequence_;
    if (continued != packet_pending_) return Fail("Ogg continuation mismatch");
    page_granule_ = Le64(header_.data() + 6);
    if (page_granule_ != kUnknownGranule && page_granule_ > uint64_t(INT64_MAX))
        return Fail("Invalid negative Ogg granule");
    if (packet_kind_ == Packet::Head && (header_[26] != 1 || page_granule_ != 0 || page_eos()))
        return Fail("OpusHead must occupy the initial page alone");
    crc_ = 0;
    UpdateCrc(header_.data(), 22);
    const uint8_t zeros[4] = {};
    UpdateCrc(zeros, 4);
    UpdateCrc(header_.data() + 26, 1);
    lacing_used_ = 0;
    segment_ = 0;
    segment_used_ = 0;
    page_audio_ = false;
    state_ = State::Lacing;
    return true;
}

bool MusicOggDemuxer::CompletePage() {
    if (crc_ != Le32(header_.data() + 22)) return Fail("Ogg CRC mismatch");
    if (packet_kind_ == Packet::Head) return Fail("OpusHead must complete on its BOS page");
    if (!page_eos() && !page_audio_ && packet_pending_ && page_granule_ != kUnknownGranule)
        return Fail("Unfinished Ogg packet page must have unknown granule");
    if (page_audio_) {
        if (page_granule_ == kUnknownGranule) return Fail("Missing audio granule");
        if (last_granule_ != kUnknownGranule && page_granule_ < last_granule_)
            return Fail("Ogg granule moved backwards");
        if (page_granule_ > total_samples_ * 2)
            return Fail("Nonzero Ogg granule origin is unsupported");
        last_granule_ = page_granule_;
    }
    if (page_eos()) {
        if (packet_pending_) return Fail("Incomplete packet at Ogg EOS");
        if (!audio_seen_ || !page_audio_ || page_granule_ == kUnknownGranule)
            return Fail("Ogg EOS without completed audio");
        if (page_granule_ < static_cast<uint64_t>(pre_skip_))
            return Fail("Ogg EOS precedes Opus pre-skip");
        eos_ = true;
        eos_granule_ = page_granule_;
    }
    header_used_ = 0;
    state_ = State::Header;
    return true;
}

bool MusicOggDemuxer::TagByte(uint8_t byte) {
    if (tags_state_ == Tags::Signature) {
        if (byte != static_cast<uint8_t>("OpusTags"[packet_size_]))
            return Fail("Missing OpusTags signature");
        if (packet_size_ == 7) tags_state_ = Tags::VendorLength;
        return true;
    }
    if (tags_state_ == Tags::Padding) return true;
    if (tags_state_ == Tags::Vendor || tags_state_ == Tags::Comment) {
        if (--tag_remaining_ == 0) {
            if (tags_state_ == Tags::Vendor) tags_state_ = Tags::Count;
            else tags_state_ = --comments_remaining_ ? Tags::CommentLength : Tags::Padding;
        }
        return true;
    }
    tag_value_ |= uint32_t(byte) << (8 * tag_value_bytes_++);
    if (tag_value_bytes_ != 4) return true;
    const uint32_t value = tag_value_;
    tag_value_ = 0;
    tag_value_bytes_ = 0;
    if (tags_state_ == Tags::Count) {
        if (value > (kMaxTagsBytes - packet_size_ - 1) / 4)
            return Fail("Too many OpusTags comments");
        comments_remaining_ = value;
        tags_state_ = value ? Tags::CommentLength : Tags::Padding;
    } else {
        if (value > kMaxTagsBytes - packet_size_ - 1)
            return Fail("OpusTags field exceeds metadata limit");
        tag_remaining_ = value;
        if (tags_state_ == Tags::VendorLength)
            tags_state_ = value ? Tags::Vendor : Tags::Count;
        else if (value) tags_state_ = Tags::Comment;
        else tags_state_ = --comments_remaining_ ? Tags::CommentLength : Tags::Padding;
    }
    return true;
}

bool MusicOggDemuxer::Append(const uint8_t* data, size_t size) {
    const size_t limit = packet_kind_ == Packet::Tags ? kMaxTagsBytes : kMaxPacketBytes;
    if (size > limit - packet_size_) return Fail("Ogg packet exceeds size limit");
    if (packet_kind_ == Packet::Tags) {
        for (size_t i = 0; i < size; ++i) {
            if (!TagByte(data[i])) return false;
            ++packet_size_;
        }
    } else {
        std::memcpy(packet_.data() + packet_size_, data, size);
        packet_size_ += size;
    }
    return true;
}

int MusicOggDemuxer::PacketSamples() const {
    if (packet_size_ == 0) return 0;
    const uint8_t toc = packet_[0];
    int per_frame;
    if (toc & 0x80) per_frame = (kDecodeRate << ((toc >> 3) & 3)) / 400;
    else if ((toc & 0x60) == 0x60) per_frame = (toc & 8) ? 480 : 240;
    else {
        const int duration = (toc >> 3) & 3;
        per_frame = duration == 3 ? 1440 : (kDecodeRate << duration) / 100;
    }
    const int code = toc & 3;
    int count = code == 0 ? 1 : 2;
    size_t offset = 1;
    size_t end = packet_size_;
    bool vbr = code == 2;
    if (code == 3) {
        if (offset >= end) return 0;
        const uint8_t control = packet_[offset++];
        count = control & 0x3f;
        if (!count || count > 48) return 0;
        vbr = (control & 0x80) != 0;
        if (control & 0x40) {
            uint8_t padding;
            do {
                if (offset >= end) return 0;
                padding = packet_[offset++];
                const size_t bytes = padding == 255 ? 254 : padding;
                if (bytes > end - offset) return 0;
                end -= bytes;
            } while (padding == 255);
        }
    }
    if (per_frame * count > 2880) return 0;
    if (vbr) {
        size_t declared = 0;
        for (int i = 0; i < count - 1; ++i) {
            size_t length;
            if (!FrameSize(packet_.data(), end, offset, length)) return 0;
            declared += length;
            if (declared > end - offset) return 0;
        }
        if (end - offset - declared > 1275) return 0;
    } else {
        const size_t bytes = end - offset;
        if (bytes % static_cast<size_t>(count) || bytes / count > 1275) return 0;
    }
    return per_frame * count;
}

bool MusicOggDemuxer::CompletePacket() {
    if (packet_kind_ == Packet::Head) {
        if (packet_size_ < 19 || std::memcmp(packet_.data(), "OpusHead", 8) != 0)
            return Fail("Invalid OpusHead signature or length");
        if (!packet_[8] || packet_[8] > 15 || (packet_[8] == 1 && packet_size_ != 19))
            return Fail("Unsupported OpusHead version or length");
        if ((packet_[9] != 1 && packet_[9] != 2) || packet_[18] != 0)
            return Fail("Only mono/stereo Opus mapping family 0 is supported");
        channels_ = packet_[9];
        pre_skip_ = packet_[10] | (int(packet_[11]) << 8);
        const int gain = packet_[16] | (int(packet_[17]) << 8);
        output_gain_ = gain >= 32768 ? gain - 65536 : gain;
        packet_kind_ = Packet::Tags;
    } else if (packet_kind_ == Packet::Tags) {
        if (tags_state_ != Tags::Padding) return Fail("Truncated OpusTags fields");
        if (segment_ + 1 != header_[26] || page_granule_ != 0)
            return Fail("OpusTags must finish its page with zero granule");
        packet_kind_ = Packet::Audio;
    } else {
        const int samples = PacketSamples();
        if (!samples) return Fail("Invalid Opus packet framing or duration");
        if (total_samples_ > std::numeric_limits<uint64_t>::max() / 2 - samples)
            return Fail("Opus sample counter overflow");
        total_samples_ += samples;
        page_audio_ = true;
        audio_seen_ = true;
        if (!callback_ || !callback_(packet_.data(), packet_size_, samples))
            return Fail("Opus packet callback cancelled");
    }
    packet_size_ = 0;
    packet_pending_ = false;
    return true;
}

bool MusicOggDemuxer::Feed(const uint8_t* data, size_t size) {
    if (!error_.empty()) return false;
    if (size && !data) return Fail("Null Ogg input");
    if (size && (finished_ || eos_)) return Fail("Data after Ogg EOS");
    while (size || state_ != State::Header) {
        if (eos_) return size ? Fail("Data after Ogg EOS") : true;
        if (state_ == State::Header) {
            if (!size) break;
            const size_t take = std::min(size, header_.size() - header_used_);
            std::memcpy(header_.data() + header_used_, data, take);
            header_used_ += take;
            data += take;
            size -= take;
            if (header_used_ < header_.size()) break;
            if (!ParseHeader()) return false;
        } else if (state_ == State::Lacing) {
            const size_t take = std::min(size, size_t(header_[26]) - lacing_used_);
            if (take) {
                std::memcpy(lacing_.data() + lacing_used_, data, take);
                UpdateCrc(data, take);
                lacing_used_ += take;
                data += take;
                size -= take;
            }
            if (lacing_used_ < header_[26]) break;
            state_ = State::Body;
        } else {
            if (segment_ == header_[26]) {
                if (!CompletePage()) return false;
                continue;
            }
            const size_t take = std::min(size, size_t(lacing_[segment_]) - segment_used_);
            if (take) {
                if (!Append(data, take)) return false;
                UpdateCrc(data, take);
                segment_used_ += take;
                data += take;
                size -= take;
            }
            if (segment_used_ < lacing_[segment_]) break;
            packet_pending_ = true;
            if (lacing_[segment_] < 255 && !CompletePacket()) return false;
            ++segment_;
            segment_used_ = 0;
        }
    }
    return true;
}

bool MusicOggDemuxer::Finish() {
    if (!error_.empty()) return false;
    if (state_ != State::Header || header_used_ || packet_pending_)
        return Fail("Truncated Ogg page or packet");
    if (!eos_) return Fail("Missing Ogg EOS");
    finished_ = true;
    return true;
}
