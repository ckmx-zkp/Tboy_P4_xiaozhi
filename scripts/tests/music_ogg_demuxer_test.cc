#include "../../main/audio/demuxer/music_ogg_demuxer.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

using Bytes = std::vector<uint8_t>;
static int checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::cerr << "FAIL " << __LINE__ << ": " << #x << '\n'; std::exit(1); } } while (false)

void Put32(Bytes& b, size_t pos, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) b[pos + i] = uint8_t(value >> (i * 8));
}
Bytes Page(uint32_t seq, uint8_t flags, uint64_t granule, const Bytes& lace, const Bytes& body) {
    Bytes b(27, 0);
    std::memcpy(b.data(), "OggS", 4);
    b[5] = flags;
    Put32(b, 6, uint32_t(granule)); Put32(b, 10, uint32_t(granule >> 32));
    Put32(b, 14, 17); Put32(b, 18, seq); b[26] = uint8_t(lace.size());
    b.insert(b.end(), lace.begin(), lace.end()); b.insert(b.end(), body.begin(), body.end());
    uint32_t crc = 0;
    for (auto v : b) {
        crc ^= uint32_t(v) << 24;
        for (int bit = 0; bit < 8; ++bit) crc = (crc << 1) ^ ((crc & 0x80000000u) ? 0x04c11db7u : 0);
    }
    Put32(b, 22, crc); return b;
}
void Join(Bytes& a, const Bytes& b) { a.insert(a.end(), b.begin(), b.end()); }
Bytes Packet(uint32_t seq, uint8_t flags, uint64_t granule, const Bytes& b) {
    Bytes lace(b.size() / 255, 255); lace.push_back(uint8_t(b.size() % 255));
    return Page(seq, flags, granule, lace, b);
}
Bytes Head(int channels = 1) {
    Bytes b(19, 0); std::memcpy(b.data(), "OpusHead", 8);
    b[8] = 1; b[9] = channels; b[10] = 0x38; b[11] = 1;
    Put32(b, 12, 44100); return b;
}
Bytes Prefix(const Bytes& head = Head()) {
    Bytes tags(16, 0); std::memcpy(tags.data(), "OpusTags", 8);
    Bytes b = Packet(0, 2, 0, head); Join(b, Packet(1, 0, 0, tags)); return b;
}
Bytes Stream(const Bytes& audio = {0x98, 0}, const Bytes& head = Head()) {
    Bytes b = Prefix(head); Join(b, Packet(2, 4, 960, audio)); return b;
}
bool Feed(MusicOggDemuxer& p, const Bytes& b, size_t chunk) {
    for (size_t i = 0; i < b.size(); i += chunk)
        if (!p.Feed(b.data() + i, std::min(chunk, b.size() - i))) return false;
    return true;
}
void Reject(const Bytes& b) {
    MusicOggDemuxer p([](const uint8_t*, size_t, int) { return true; });
    CHECK(!Feed(p, b, 7) || !p.Finish()); CHECK(!p.error().empty());
    const auto error = p.error(); CHECK(!p.Feed(nullptr, 0)); CHECK(!p.Finish()); CHECK(p.error() == error);
}
int main() {
    const Bytes b = Stream();
    for (size_t chunk = 1; chunk <= b.size(); ++chunk) {
        int calls = 0; MusicOggDemuxer* current = nullptr;
        MusicOggDemuxer p([&](const uint8_t*, size_t n, int samples) {
            CHECK(n == 2 && samples == 480); CHECK(current->page_eos());
            CHECK(current->page_granule() == 960); ++calls; return true;
        });
        current = &p;
        CHECK(Feed(p, b, chunk)); CHECK(p.Finish()); CHECK(calls == 1);
        CHECK(p.pre_skip() == 312 && p.channels() == 1 && p.eos_granule() == 960);
    }
    for (size_t split = 0; split <= b.size(); ++split) {
        MusicOggDemuxer p([](const uint8_t*, size_t, int) { return true; });
        CHECK(p.Feed(b.data(), split)); CHECK(p.Feed(b.data() + split, b.size() - split)); CHECK(p.Finish());
    }
    const std::vector<std::pair<Bytes, int>> durations = {
        {{0x80, 0}, 60}, {{0x88, 0}, 120}, {{0x90, 0}, 240}, {{0x98, 0}, 480},
        {{0x00, 0}, 240}, {{0x08, 0}, 480}, {{0x10, 0}, 960}, {{0x18, 0}, 1440},
        {{0x99, 0, 0}, 960}, {{0x1b, 2, 0, 0}, 2880}, {{0x83, 48}, 2880},
        {{0x9a, 1, 0, 0}, 960}, {{0x9b, 0x82, 1, 0, 0}, 960}
    };
    for (const auto& item : durations) {
        Bytes head = Head(2); head[10] = head[11] = 0;
        Bytes stream = Prefix(head); Join(stream, Packet(2, 4, item.second * 2, item.first));
        MusicOggDemuxer p([&](const uint8_t*, size_t, int n) { CHECK(n == item.second); return true; });
        CHECK(Feed(p, stream, 1)); CHECK(p.Finish()); CHECK(p.channels() == 2);
    }
    Bytes large(8192, 0); large[0] = 0x83; large[1] = 10;
    Bytes stream = Prefix();
    Join(stream, Page(2, 0, UINT64_MAX, {255}, Bytes(large.begin(), large.begin() + 255)));
    Join(stream, Page(3, 1, UINT64_MAX, {}, {}));
    Join(stream, Packet(4, 5, 1200, Bytes(large.begin() + 255, large.end())));
    MusicOggDemuxer continued([&](const uint8_t* data, size_t size, int samples) {
        CHECK(size == large.size() && std::equal(large.begin(), large.end(), data)); CHECK(samples == 600); return true;
    });
    CHECK(Feed(continued, stream, 1)); CHECK(continued.Finish());
    CHECK(sizeof(MusicOggDemuxer) < 10000);
    for (size_t size = 0; size < b.size(); ++size) Reject(Bytes(b.begin(), b.begin() + size));
    for (const Bytes& bad : std::vector<Bytes>{{}, {0x03}, {0x83, 49}, {0x1b, 3}, {0x99, 0}, {0x9a, 252}, {0x9b, 0x42, 100}}) Reject(Stream(bad));
    Reject(Stream(Bytes(8193, 0)));
    Bytes bad = b; bad.back() ^= 0xff; Reject(bad);
    bad = b; bad.push_back(0); Reject(bad);
    for (int field : {0, 8, 9, 18}) {
        Bytes h = Head(); h[field] = 0xff; Reject(Stream({0x98, 0}, h));
    }
    stream = Prefix(); Join(stream, Packet(2, 5, 960, {0x98, 0})); Reject(stream);
    stream = Prefix(); Join(stream, Packet(2, 0, 960, {0x98, 0})); Reject(stream);
    stream = Prefix(); Join(stream, Packet(2, 4, 961, {0x98, 0})); Reject(stream);
    stream = Prefix(); Join(stream, Packet(2, 4, 1, {0x98, 0})); Reject(stream);
    stream = Prefix(); Join(stream, Page(2, 4, 1800, {2, 2}, {0x98, 0, 0x98, 0}));
    MusicOggDemuxer trimmed([](const uint8_t*, size_t, int n) { return n == 480; });
    CHECK(Feed(trimmed, stream, 1)); CHECK(trimmed.Finish()); CHECK(trimmed.total_samples() == 960);
    MusicOggDemuxer cancelled([](const uint8_t*, size_t, int) { return false; });
    CHECK(!cancelled.Feed(b.data(), b.size())); CHECK(!cancelled.Finish());
    Bytes tags(100016, 0); std::memcpy(tags.data(), "OpusTags", 8); Put32(tags, 8, 100000);
    stream = Packet(0, 2, 0, Head());
    Join(stream, Page(1, 0, UINT64_MAX, Bytes(255, 255), Bytes(tags.begin(), tags.begin() + 65025)));
    Join(stream, Packet(2, 1, 0, Bytes(tags.begin() + 65025, tags.end())));
    Join(stream, Packet(3, 4, 960, {0x98, 0}));
    MusicOggDemuxer metadata([](const uint8_t*, size_t, int) { return true; });
    CHECK(Feed(metadata, stream, 1)); CHECK(metadata.Finish());
    std::cout << "music_ogg_demuxer_test: " << checks << " checks passed\n";
}
