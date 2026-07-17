#ifndef __EYE_CONTROLLER_H__
#define __EYE_CONTROLLER_H__

#include "mcp_server.h"
#include "pet_eye_display.h"

/**
 * Exposes the AI Pet eye as MCP tools so the cloud LLM can drive it via
 * tool_call instead of replying "I can't do that". Same pattern as
 * LampController: register plain (non user-only) tools at board init; the
 * server pulls them with tools/list on each session.
 *
 * Voice examples that should trigger these:
 *   "小智小智，向上看"   -> self.eye.look {direction:"up"}
 *   "眨眨眼"             -> self.eye.blink
 *   "开心一点"           -> self.eye.set_emotion {emotion:"happy"}
 */
class EyeController {
public:
    explicit EyeController(PetEyeDisplay* display) : display_(display) {
        if (display_ == nullptr) {
            return;
        }
        auto& mcp_server = McpServer::GetInstance();

        mcp_server.AddTool(
            "self.eye.look",
            "Make the pet eye look in a direction. Call this when the user asks the pet to "
            "look up/down/left/right, raise its head, glance somewhere, or return its gaze "
            "to center. direction must be one of: up, down, left, right, center.",
            PropertyList({
                Property("direction", kPropertyTypeString, "center"),
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                std::string dir = properties["direction"].value<std::string>();
                display_->SetGaze(dir.c_str());
                return true;
            });

        mcp_server.AddTool(
            "self.eye.blink",
            "Make the pet eye blink once. Call this when the user asks the pet to blink, "
            "wink, or 眨眼.",
            PropertyList(),
            [this](const PropertyList& properties) -> ReturnValue {
                display_->BlinkOnce();
                return true;
            });

        mcp_server.AddTool(
            "self.eye.close",
            "Close the pet eye and keep it closed (sleep). Call this when the user asks the "
            "pet to close its eyes, go to sleep, or rest its eyes. Use self.eye.open to wake.",
            PropertyList(),
            [this](const PropertyList& properties) -> ReturnValue {
                display_->SetClosed(true);
                return true;
            });

        mcp_server.AddTool(
            "self.eye.open",
            "Open the pet eye again after it was closed. Call this when the user asks the pet "
            "to open its eyes, wake up, or stop sleeping.",
            PropertyList(),
            [this](const PropertyList& properties) -> ReturnValue {
                display_->SetClosed(false);
                return true;
            });

        mcp_server.AddTool(
            "self.eye.set_emotion",
            "Set the pet eye's expression. Call this when the user asks the pet to be "
            "happy, angry, sad, joyful/excited, or back to normal. emotion must be one of: "
            "neutral, happy, angry, sad, joy.",
            PropertyList({
                Property("emotion", kPropertyTypeString, "neutral"),
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                std::string emo = properties["emotion"].value<std::string>();
                display_->SetEmotion(emo.c_str());
                return true;
            });
    }

private:
    PetEyeDisplay* display_ = nullptr;
};

#endif // __EYE_CONTROLLER_H__
