#ifndef _LED_MOOD_CONTROLLER_H_
#define _LED_MOOD_CONTROLLER_H_

#include "mcp_server.h"
#include "led/circular_strip.h"

#include <esp_log.h>
#include <esp_random.h>

#include <algorithm>
#include <cstdio>
#include <string>

// 两颗 WS2812：话术切 12 星座 / 表情。颜色在色相附近随机，不接 GetLed()，避免被 listening 状态灯覆盖。
class LedMoodController {
public:
    explicit LedMoodController(CircularStrip* strip) : strip_(strip) {
        if (strip_ == nullptr) {
            return;
        }
        ApplyEmotion("neutral");
        RegisterTools();
    }

    bool ApplyEmotion(const std::string& raw) {  // 眼睛 MCP 也会调，跟着变色
        const std::string key = LowerAscii(raw);
        struct Item {
            const char* id;
            const char* aliases;
            int hue;
            uint8_t sat;
            uint8_t val;
        };
        static const Item kItems[] = {
            {"neutral", "neutral,normal,calm,gentle,中性,正常,平静,默认", 195, 80, 90},
            {"happy", "happy,开心,高兴,喜,愉快,微笑", 48, 220, 170},
            {"angry", "angry,生气,愤怒,怒,发火", 0, 240, 170},
            {"sad", "sad,难过,伤心,哀,悲伤,郁闷", 220, 200, 130},
            {"joy", "joy,joyful,excited,兴奋,快乐,乐,狂欢,嗨", 300, 210, 180},
        };
        for (const auto& item : kItems) {
            std::string aliases = item.aliases;
            size_t start = 0;
            while (start < aliases.size()) {
                size_t comma = aliases.find(',', start);
                if (comma == std::string::npos) {
                    comma = aliases.size();
                }
                if (key == aliases.substr(start, comma - start)) {
                    mode_ = "emotion";
                    emotion_ = item.id;
                    zodiac_.clear();
                    ShowHue(item.hue + static_cast<int>(esp_random() % 17) - 8,
                            Jitter(item.sat, 20), Jitter(item.val, 20));
                    return true;
                }
                start = comma + 1;
            }
        }
        return false;
    }

private:
    CircularStrip* strip_ = nullptr;
    std::string mode_ = "emotion";
    std::string zodiac_ = "";
    std::string emotion_ = "neutral";
    StripColor led0_{};
    StripColor led1_{};

    static std::string LowerAscii(std::string s) {
        for (char& c : s) {
            if (c >= 'A' && c <= 'Z') {
                c = static_cast<char>(c - 'A' + 'a');
            }
        }
        return s;
    }

    static uint8_t ClampU8(int v) {
        return static_cast<uint8_t>(std::clamp(v, 0, 255));
    }

    static uint8_t Jitter(uint8_t base, int span) {
        int delta = static_cast<int>(esp_random() % (span * 2 + 1)) - span;
        return ClampU8(static_cast<int>(base) + delta);
    }

    static StripColor Hsv(int hue, uint8_t sat, uint8_t val) {
        hue = ((hue % 360) + 360) % 360;
        uint8_t region = static_cast<uint8_t>(hue / 60);
        uint8_t remainder = static_cast<uint8_t>((hue % 60) * 255 / 60);
        uint8_t p = static_cast<uint8_t>(val * (255 - sat) / 255);
        uint8_t q = static_cast<uint8_t>(val * (255 - sat * remainder / 255) / 255);
        uint8_t t = static_cast<uint8_t>(val * (255 - sat * (255 - remainder) / 255) / 255);
        switch (region) {
            case 0:  return {val, t, p};
            case 1:  return {q, val, p};
            case 2:  return {p, val, t};
            case 3:  return {p, q, val};
            case 4:  return {t, p, val};
            default: return {val, p, q};
        }
    }

    void ShowPair(StripColor a, StripColor b) {
        led0_ = a;
        led1_ = b;
        strip_->SetSingleColor(0, a);
        strip_->SetSingleColor(1, b);
        ESP_LOGI("LedMood", "LED %s/%s  #%02X%02X%02X  #%02X%02X%02X",
                 mode_.c_str(),
                 mode_ == "zodiac" ? zodiac_.c_str() : emotion_.c_str(),
                 a.red, a.green, a.blue, b.red, b.green, b.blue);
    }

    void ShowHue(int hue, uint8_t sat, uint8_t val) {
        // 两颗灯同色系、略偏色相，方便看出都在亮
        ShowPair(Hsv(hue, sat, val), Hsv(hue + 18, Jitter(sat, 20), Jitter(val, 18)));
    }

    bool ApplyZodiac(const std::string& raw) {
        const std::string key = LowerAscii(raw);
        struct Item {
            const char* id;
            const char* aliases;
            int hue;
        };
        static const Item kItems[] = {
            {"aries", "aries,baiyang,白羊,白羊座", 8},
            {"taurus", "taurus,jinniu,金牛,金牛座", 120},
            {"gemini", "gemini,shuangzi,双子,双子座", 52},
            {"cancer", "cancer,juxie,巨蟹,巨蟹座", 190},
            {"leo", "leo,shizi,狮子,狮子座", 36},
            {"virgo", "virgo,chunv,处女,处女座", 90},
            {"libra", "libra,tiancheng,天秤,天秤座,天平,天平座", 320},
            {"scorpio", "scorpio,tianxie,天蝎,天蝎座", 345},
            {"sagittarius", "sagittarius,sheshou,射手,射手座,人马,人马座", 270},
            {"capricorn", "capricorn,mojie,摩羯,摩羯座,山羊,山羊座", 160},
            {"aquarius", "aquarius,shuiping,水瓶,水瓶座", 200},
            {"pisces", "pisces,shuangyu,双鱼,双鱼座", 230},
        };
        for (const auto& item : kItems) {
            std::string aliases = item.aliases;
            size_t start = 0;
            while (start < aliases.size()) {
                size_t comma = aliases.find(',', start);
                if (comma == std::string::npos) {
                    comma = aliases.size();
                }
                if (key == aliases.substr(start, comma - start)) {
                    mode_ = "zodiac";
                    zodiac_ = item.id;
                    emotion_.clear();
                    ShowHue(item.hue + static_cast<int>(esp_random() % 21) - 10,
                            Jitter(210, 25), Jitter(150, 25));
                    return true;
                }
                start = comma + 1;
            }
        }
        return false;
    }

    std::string StateJson() const {
        char buf[192];
        snprintf(buf, sizeof(buf),
                 "{\"mode\":\"%s\",\"zodiac\":\"%s\",\"emotion\":\"%s\","
                 "\"led0\":\"#%02X%02X%02X\",\"led1\":\"#%02X%02X%02X\"}",
                 mode_.c_str(), zodiac_.c_str(), emotion_.c_str(),
                 led0_.red, led0_.green, led0_.blue,
                 led1_.red, led1_.green, led1_.blue);
        return buf;
    }

    void RegisterTools() {
        auto& mcp = McpServer::GetInstance();

        mcp.AddTool(
            "self.led.set_zodiac",
            "本设备有两颗 WS2812 氛围灯。用户说到十二星座、变成某座、按星座亮灯时必须立刻调用本工具，"
            "不要只口头答应。sign 可用英文或中文：aries/白羊、taurus/金牛、gemini/双子、cancer/巨蟹、"
            "leo/狮子、virgo/处女、libra/天秤、scorpio/天蝎、sagittarius/射手、capricorn/摩羯、"
            "aquarius/水瓶、pisces/双鱼。每次颜色会在该星座色系内随机。",
            PropertyList({Property("sign", kPropertyTypeString)}),
            [this](const PropertyList& properties) -> ReturnValue {
                auto sign = properties["sign"].value<std::string>();
                if (!ApplyZodiac(sign)) {
                    throw std::runtime_error("unknown zodiac: " + sign);
                }
                return StateJson();
            });

        mcp.AddTool(
            "self.led.set_emotion",
            "只改两颗 WS2812 颜色，不改眼睛。"
            "只要灯变色、心情灯、氛围灯时调用。换脸请用 self.eye.set_emotion（会顺带改灯）。"
            "emotion：neutral/happy/angry/sad/joy。",
            PropertyList({Property("emotion", kPropertyTypeString, "neutral")}),
            [this](const PropertyList& properties) -> ReturnValue {
                auto emotion = properties["emotion"].value<std::string>();
                if (!ApplyEmotion(emotion)) {
                    throw std::runtime_error("unknown emotion: " + emotion);
                }
                return StateJson();
            });

        mcp.AddTool(
            "self.led.off",
            "关掉两颗 WS2812。用户说关灯、熄灭氛围灯、不要亮灯时调用。",
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                mode_ = "off";
                zodiac_.clear();
                emotion_.clear();
                ShowPair({0, 0, 0}, {0, 0, 0});
                return StateJson();
            });

        mcp.AddTool(
            "self.led.get_state",
            "查询当前氛围灯是星座还是表情，以及两颗灯颜色。",
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                return StateJson();
            });
    }
};

#endif  // _LED_MOOD_CONTROLLER_H_
