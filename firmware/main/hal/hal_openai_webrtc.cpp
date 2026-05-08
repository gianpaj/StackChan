/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 * SPDX-License-Identifier: MIT
 *
 * OpenAI Realtime API over WebSocket.
 *
 * Flow:
 *   1. Connect to wss://api.openai.com/v1/realtime?model=gpt-realtime-2
 *      with Authorization header.
 *   2. Server sends session.created → send session.update to configure voice/instructions.
 *   3. Mic loop: read PCM16 from CoreS3AudioCodec → base64 → input_audio_buffer.append
 *   4. Server streams response.audio.delta (base64 PCM16) → decode → write to speaker.
 *   5. Avatar mouth/emotion driven by response.audio.delta presence + response events.
 */
#include "hal.h"
#include "board/hal_bridge.h"
#include <mooncake_log.h>
#include <mooncake.h>
#include <stackchan/stackchan.h>
#include <stackchan/modifiers/modifiers.h>
#include <apps/common/common.h>
#include <assets/assets.h>
#include <web_socket.h>
#include <board.h>
#include <display.h>
#include <audio_codec.h>
#include <wifi_station.h>
#include <settings.h>
#include <ArduinoJson.hpp>
#include <mbedtls/base64.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_log.h>
#include <esp_system.h>
#include <cstring>

static const char* _tag = "OpenAI-WS";

static void _set_status_led(bool connected)
{
    // Green (0,50,0) = connected/mic active. Blue (0,0,50) = disconnected.
    if (connected) {
        GetHAL().showRgbColor(0, 50, 0);
    } else {
        GetHAL().showRgbColor(0, 0, 50);
    }
    GetHAL().refreshRgb();
}

static constexpr char _ws_url[]      = "wss://api.openai.com/v1/realtime?model=gpt-realtime-2";
static constexpr char _api_key_ns[]  = "openai";
static constexpr char _api_key_key[] = "api_key";

// Audio parameters — must match CoreS3AudioCodec config
static constexpr int _sample_rate  = 24000;
static constexpr int _mic_chunk_ms = 60;   // send 60 ms of audio per packet
static constexpr int _mic_samples  = (_sample_rate * _mic_chunk_ms) / 1000;  // 1440

/* -------------------------------------------------------------------------- */
/*                              Base64 helpers                                */
/* -------------------------------------------------------------------------- */

static std::string base64_encode(const uint8_t* data, size_t len)
{
    size_t out_len = 0;
    mbedtls_base64_encode(nullptr, 0, &out_len, data, len);
    std::string out(out_len, '\0');
    mbedtls_base64_encode((unsigned char*)out.data(), out_len, &out_len, data, len);
    out.resize(out_len);
    while (!out.empty() && out.back() == '\0') out.pop_back();
    return out;
}

static std::vector<uint8_t> base64_decode(const std::string& b64)
{
    size_t out_len = 0;
    mbedtls_base64_decode(nullptr, 0, &out_len, (const unsigned char*)b64.data(), b64.size());
    std::vector<uint8_t> out(out_len);
    mbedtls_base64_decode(out.data(), out_len, &out_len, (const unsigned char*)b64.data(), b64.size());
    out.resize(out_len);
    return out;
}

/* -------------------------------------------------------------------------- */
/*                            OpenAI session task                             */
/* -------------------------------------------------------------------------- */

struct PendingToolCall {
    std::string call_id;
    std::string name;
    std::string args;  // accumulated from deltas
};

struct OpenAiContext {
    std::unique_ptr<WebSocket> ws;
    SemaphoreHandle_t           ws_mutex;
    bool                        session_ready        = false;
    bool                        speaking             = false;
    int                         speaking_modifier_id = -1;
    uint32_t                    speaking_stop_ms     = 0;
    PendingToolCall             pending_tool;
    bool                        has_pending_tool     = false;
};

static void _send_json(OpenAiContext& ctx, const ArduinoJson::JsonDocument& doc)
{
    std::string payload;
    ArduinoJson::serializeJson(doc, payload);
    xSemaphoreTake(ctx.ws_mutex, portMAX_DELAY);
    if (ctx.ws && ctx.ws->IsConnected()) {
        ctx.ws->Send(payload);
    }
    xSemaphoreGive(ctx.ws_mutex);
}

static void _configure_session(OpenAiContext& ctx)
{
    Settings settings(_api_key_ns, false);
    auto instructions = settings.GetString("instructions",
        "You are StackChan, a friendly robot companion. Keep responses short and conversational.");

    ArduinoJson::JsonDocument doc;
    doc["type"] = "session.update";
    auto session = doc["session"].to<ArduinoJson::JsonObject>();
    session["type"]         = "realtime";
    session["instructions"] = instructions;

    auto audio_in = session["audio"]["input"].to<ArduinoJson::JsonObject>();
    auto turn     = audio_in["turn_detection"].to<ArduinoJson::JsonObject>();
    turn["type"]                = "server_vad";
    turn["threshold"]           = 0.8f;
    turn["silence_duration_ms"] = 600;
    turn["interrupt_response"]  = true;

    // Tools — mirrors hal_mcp.cpp for the Xiaozhi path
    session["tool_choice"] = "auto";
    auto tools = session["tools"].to<ArduinoJson::JsonArray>();

    {   // set_led_color
        auto t = tools.add<ArduinoJson::JsonObject>();
        t["type"] = "function";
        t["name"] = "set_led_color";
        t["description"] = "Set the color of the robot's built-in LED lights (0-168 each). Examples: Red=(168,0,0), Green=(0,168,0), Blue=(0,0,168), Off=(0,0,0).";
        auto p = t["parameters"].to<ArduinoJson::JsonObject>();
        p["type"] = "object";
        p["properties"]["red"]["type"]   = "integer";
        p["properties"]["green"]["type"] = "integer";
        p["properties"]["blue"]["type"]  = "integer";
        p["required"].add("red"); p["required"].add("green"); p["required"].add("blue");
    }
    {   // set_head_angles
        auto t = tools.add<ArduinoJson::JsonObject>();
        t["type"] = "function";
        t["name"] = "set_head_angles";
        t["description"] = "Move the robot head. Yaw: horizontal (-128=left, 128=right). Pitch: vertical (0=level, 90=up). Speed: 100-1000 (150=natural).";
        auto p = t["parameters"].to<ArduinoJson::JsonObject>();
        p["type"] = "object";
        p["properties"]["yaw"]["type"]   = "integer";
        p["properties"]["pitch"]["type"] = "integer";
        p["properties"]["speed"]["type"] = "integer";
        p["required"].add("yaw"); p["required"].add("pitch"); p["required"].add("speed");
    }
    {   // get_head_angles
        auto t = tools.add<ArduinoJson::JsonObject>();
        t["type"] = "function";
        t["name"] = "get_head_angles";
        t["description"] = "Get current yaw/pitch head position in degrees.";
        auto p = t["parameters"].to<ArduinoJson::JsonObject>();
        p["type"] = "object";
        p["properties"].to<ArduinoJson::JsonObject>();  // required empty object
    }
    {   // set_volume
        auto t = tools.add<ArduinoJson::JsonObject>();
        t["type"] = "function";
        t["name"] = "set_volume";
        t["description"] = "Set the speaker volume (0-100).";
        auto p = t["parameters"].to<ArduinoJson::JsonObject>();
        p["type"] = "object";
        p["properties"]["volume"]["type"] = "integer";
        p["required"].add("volume");
    }
    {   // get_battery
        auto t = tools.add<ArduinoJson::JsonObject>();
        t["type"] = "function";
        t["name"] = "get_battery";
        t["description"] = "Get current battery level (0-100%) and charging status.";
        auto p = t["parameters"].to<ArduinoJson::JsonObject>();
        p["type"] = "object";
        p["properties"].to<ArduinoJson::JsonObject>();  // required empty object
    }

    _send_json(ctx, doc);
    ESP_LOGI(_tag, "session.update sent");
}

static void _execute_tool(OpenAiContext& ctx, const PendingToolCall& call)
{
    ESP_LOGI(_tag, "tool call: %s args=%s", call.name.c_str(), call.args.c_str());

    std::string result = R"({"success":true})";

    ArduinoJson::JsonDocument args;
    bool args_ok = ArduinoJson::deserializeJson(args, call.args) == ArduinoJson::DeserializationError::Ok;

    if (call.name == "set_led_color") {
        int r = args_ok ? (int)(args["red"]   | 0) : 0;
        int g = args_ok ? (int)(args["green"] | 0) : 0;
        int b = args_ok ? (int)(args["blue"]  | 0) : 0;
        LvglLockGuard lock;
        GetStackChan().leftNeonLight().setColor(r, g, b);
        GetStackChan().rightNeonLight().setColor(r, g, b);

    } else if (call.name == "set_head_angles") {
        int yaw   = args_ok ? (int)(args["yaw"]   | 0)   : 0;
        int pitch = args_ok ? (int)(args["pitch"] | 0)   : 0;
        int speed = args_ok ? (int)(args["speed"] | 150) : 150;
        LvglLockGuard lock;
        auto& motion = GetStackChan().motion();
        motion.yawServo().moveWithSpeed(yaw * 10, speed);
        motion.pitchServo().moveWithSpeed(pitch * 10, speed);

    } else if (call.name == "get_head_angles") {
        LvglLockGuard lock;
        auto& motion = GetStackChan().motion();
        int yaw   = motion.yawServo().getCurrentAngle()   / 10;
        int pitch = motion.pitchServo().getCurrentAngle() / 10;
        char buf[48];
        snprintf(buf, sizeof(buf), R"({"yaw":%d,"pitch":%d})", yaw, pitch);
        result = buf;

    } else if (call.name == "set_volume") {
        int vol = args_ok ? (int)(args["volume"] | 70) : 70;
        GetHAL().setSpeakerVolume(static_cast<uint8_t>(vol));

    } else if (call.name == "get_battery") {
        int level = 0;
        bool charging = false, discharging = false;
        Board::GetInstance().GetBatteryLevel(level, charging, discharging);
        char buf[64];
        snprintf(buf, sizeof(buf), R"({"level":%d,"charging":%s,"discharging":%s})",
                 level, charging ? "true" : "false", discharging ? "true" : "false");
        result = buf;

    } else {
        ESP_LOGW(_tag, "unknown tool: %s", call.name.c_str());
        result = R"({"error":"unknown tool"})";
    }

    // Send function_call_output
    {
        ArduinoJson::JsonDocument resp;
        resp["type"] = "conversation.item.create";
        auto item = resp["item"].to<ArduinoJson::JsonObject>();
        item["type"]    = "function_call_output";
        item["call_id"] = call.call_id;
        item["output"]  = result;
        _send_json(ctx, resp);
    }
    // Ask the model to continue
    {
        ArduinoJson::JsonDocument cont;
        cont["type"] = "response.create";
        _send_json(ctx, cont);
    }
    ESP_LOGI(_tag, "tool result sent: %s", result.c_str());
}

static void _handle_message(OpenAiContext& ctx, const std::string& payload)
{
    ArduinoJson::JsonDocument doc;
    if (ArduinoJson::deserializeJson(doc, payload) != ArduinoJson::DeserializationError::Ok) {
        return;
    }

    const char* type = doc["type"] | "";
    ESP_LOGD(_tag, "rx event: %s", type);

    if (strcmp(type, "session.created") == 0) {
        ESP_LOGI(_tag, "session.created — configuring");
        ctx.session_ready = true;
        _configure_session(ctx);

    } else if (strcmp(type, "session.updated") == 0) {
        ESP_LOGI(_tag, "session.updated — ready for conversation");

    } else if (strcmp(type, "input_audio_buffer.speech_started") == 0) {
        ESP_LOGI(_tag, "speech detected (VAD)");

    } else if (strcmp(type, "input_audio_buffer.speech_stopped") == 0) {
        ESP_LOGI(_tag, "speech ended (VAD) — waiting for response");

    } else if (strcmp(type, "response.created") == 0) {
        ESP_LOGI(_tag, "response started");

    } else if (strcmp(type, "response.output_item.added") == 0) {
        const char* item_type = doc["item"]["type"] | "";
        if (strcmp(item_type, "function_call") == 0) {
            ctx.pending_tool.call_id = doc["item"]["call_id"] | "";
            ctx.pending_tool.name    = doc["item"]["name"]    | "";
            ctx.pending_tool.args    = "";
            ctx.has_pending_tool     = true;
            ESP_LOGI(_tag, "tool call start: %s (%s)", ctx.pending_tool.name.c_str(), ctx.pending_tool.call_id.c_str());
        }

    } else if (strcmp(type, "response.function_call_arguments.delta") == 0) {
        if (ctx.has_pending_tool) {
            ctx.pending_tool.args += doc["delta"] | "";
        }

    } else if (strcmp(type, "response.function_call_arguments.done") == 0) {
        if (ctx.has_pending_tool) {
            ctx.has_pending_tool = false;
            _execute_tool(ctx, ctx.pending_tool);
        }

    } else if (strcmp(type, "response.output_audio.delta") == 0) {
        const char* audio_b64 = doc["delta"] | "";
        if (*audio_b64) {
            auto raw = base64_decode(audio_b64);
            ESP_LOGD(_tag, "audio delta: %u bytes PCM", (unsigned)raw.size());
            if (!raw.empty()) {
                auto* codec = Board::GetInstance().GetAudioCodec();
                if (codec) {
                    if (!ctx.speaking) {
                        ctx.speaking = true;
                        ESP_LOGI(_tag, "speaking START");
                        LvglLockGuard lock;
                        ctx.speaking_modifier_id =
                            GetStackChan().addModifier(std::make_unique<stackchan::SpeakingModifier>());
                    }
                    codec->EnableOutput(true);
                    std::vector<int16_t> samples(raw.size() / sizeof(int16_t));
                    memcpy(samples.data(), raw.data(), samples.size() * sizeof(int16_t));
                    codec->OutputData(samples);
                } else {
                    ESP_LOGE(_tag, "no audio codec!");
                }
            }
        }

    } else if (strcmp(type, "response.output_audio_transcript.delta") == 0) {
        ESP_LOGI(_tag, "transcript: %s", doc["delta"] | "");

    } else if (strcmp(type, "response.output_audio.done") == 0) {
        ESP_LOGI(_tag, "response.output_audio.done");
        if (ctx.speaking) {
            ctx.speaking = false;
            ctx.speaking_stop_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
            ESP_LOGI(_tag, "speaking STOP");
            LvglLockGuard lock;
            if (ctx.speaking_modifier_id >= 0) {
                GetStackChan().removeModifier(ctx.speaking_modifier_id);
                ctx.speaking_modifier_id = -1;
            }
        }

    } else if (strcmp(type, "response.done") == 0) {
        ESP_LOGI(_tag, "response.done");
        if (ctx.speaking) {
            ctx.speaking = false;
            ctx.speaking_stop_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
            ESP_LOGI(_tag, "speaking STOP (response.done)");
            LvglLockGuard lock;
            if (ctx.speaking_modifier_id >= 0) {
                GetStackChan().removeModifier(ctx.speaking_modifier_id);
                ctx.speaking_modifier_id = -1;
            }
        }

    } else if (strcmp(type, "error") == 0) {
        ESP_LOGE(_tag, "OpenAI error: %s", doc["error"]["message"] | "unknown");

    } else {
        ESP_LOGW(_tag, "unhandled event: %s", type);
    }
}

static void _mic_send_task(void* param)
{
    auto* ctx = static_cast<OpenAiContext*>(param);

    auto* codec = Board::GetInstance().GetAudioCodec();
    if (!codec) {
        ESP_LOGE(_tag, "No audio codec");
        vTaskDelete(nullptr);
        return;
    }
    codec->EnableInput(true);

    // Buffer sized for all channels; InputData fills it via Read()
    std::vector<int16_t> buf(_mic_samples * codec->input_channels());
    uint32_t mic_packets = 0;

    while (true) {
        // Blocks until samples are ready
        codec->InputData(buf);

        // Channel 0 only — channel 1 is AEC reference on CoreS3
        std::vector<int16_t> mono(_mic_samples);
        if (codec->input_channels() > 1) {
            for (int i = 0; i < _mic_samples; i++) {
                mono[i] = buf[i * codec->input_channels()];
            }
        } else {
            mono = buf;
        }

        uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        bool in_cooldown = !ctx->speaking && (now_ms - ctx->speaking_stop_ms) < 500;
        if (!ctx->session_ready || !ctx->ws || !ctx->ws->IsConnected() ||
            ctx->speaking || in_cooldown) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        std::string b64 = base64_encode(reinterpret_cast<const uint8_t*>(mono.data()),
                                        mono.size() * sizeof(int16_t));

        ArduinoJson::JsonDocument doc;
        doc["type"]  = "input_audio_buffer.append";
        doc["audio"] = b64;

        std::string send_payload;
        ArduinoJson::serializeJson(doc, send_payload);

        xSemaphoreTake(ctx->ws_mutex, portMAX_DELAY);
        if (ctx->ws && ctx->ws->IsConnected()) {
            ctx->ws->Send(send_payload);
            mic_packets++;
            if (mic_packets % 50 == 1) {  // log every ~3 s (50 × 60 ms)
                ESP_LOGI(_tag, "mic tx: %lu packets sent", (unsigned long)mic_packets);
            }
        }
        xSemaphoreGive(ctx->ws_mutex);
    }
}

static void _openai_ws_task(void* param)
{
    OpenAiContext ctx;
    ctx.ws_mutex = xSemaphoreCreateMutex();

    Settings settings(_api_key_ns, false);
    std::string api_key = settings.GetString(_api_key_key, "");
    if (api_key.empty()) {
        ESP_LOGE(_tag, "OpenAI API key not set. Store it in NVS: ns='openai', key='api_key'");
        vTaskDelete(nullptr);
        return;
    }

    auto& board  = Board::GetInstance();
    auto network = board.GetNetwork();

    auto reconnect = [&]() {
        ctx.session_ready = false;
        ctx.speaking      = false;
        ctx.ws.reset();

        ctx.ws = network->CreateWebSocket(1);
        if (!ctx.ws) {
            ESP_LOGE(_tag, "Failed to create WebSocket");
            return;
        }

        std::string auth = std::string("Bearer ") + api_key;
        ctx.ws->SetHeader("Authorization", auth.c_str());

        ctx.ws->OnConnected([&ctx]() {
            ESP_LOGI(_tag, "Connected to OpenAI Realtime");
            _set_status_led(true);
        });

        ctx.ws->OnDisconnected([&ctx]() {
            ESP_LOGW(_tag, "Disconnected from OpenAI Realtime");
            ctx.session_ready = false;
            _set_status_led(false);
        });

        ctx.ws->OnData([&ctx](const char* data, size_t len, bool binary) {
            if (!binary) {
                _handle_message(ctx, std::string(data, len));
            }
        });

        if (!ctx.ws->Connect(_ws_url)) {
            ESP_LOGE(_tag, "Failed to connect to %s", _ws_url);
        }
    };

    reconnect();

    // Start mic sender task
    xTaskCreatePinnedToCore(_mic_send_task, "openai_mic", 4096, &ctx, 3, nullptr, 1);

    bool connected = true;
    uint32_t last_reconnect = 0;
    // Double-press detection: 1st press is "pending" until either a 2nd press arrives
    // within kDoublePressWindowMs (→ go home / reboot) or the window elapses (→ toggle WS).
    constexpr uint32_t kDoublePressWindowMs = 500;
    bool press_pending = false;
    uint32_t first_press_ms = 0;

    auto toggle_connection = [&]() {
        if (connected) {
            ESP_LOGI(_tag, "PWR single-press: disconnecting");
            connected = false;
            ctx.session_ready = false;
            xSemaphoreTake(ctx.ws_mutex, portMAX_DELAY);
            ctx.ws.reset();
            xSemaphoreGive(ctx.ws_mutex);
            _set_status_led(false);
        } else {
            ESP_LOGI(_tag, "PWR single-press: reconnecting");
            connected = true;
            last_reconnect = 0;
        }
    };

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(20));
        const uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

        if (Board::GetInstance().IsPowerButtonPressed()) {
            if (press_pending && (now - first_press_ms) < kDoublePressWindowMs) {
                ESP_LOGI(_tag, "PWR double-press: rebooting to launcher");
                vTaskDelay(pdMS_TO_TICKS(50));  // let log flush
                esp_restart();
            } else {
                press_pending = true;
                first_press_ms = now;
            }
        }

        // Single-press fires after the double-press window expires.
        if (press_pending && (now - first_press_ms) >= kDoublePressWindowMs) {
            press_pending = false;
            toggle_connection();
        }

        if (connected && (!ctx.ws || !ctx.ws->IsConnected())) {
            if (now - last_reconnect > 5000) {
                last_reconnect = now;
                ESP_LOGI(_tag, "Reconnecting...");
                reconnect();
            }
        }
    }
}

/* -------------------------------------------------------------------------- */
/*                        HAL entry point                                     */
/* -------------------------------------------------------------------------- */

static void _stackchan_update_task_openai(void* param)
{
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(20));
        LvglLockGuard lock;
        GetStackChan().update();
    }
}

void Hal::startOpenAi()
{
    mclog::tagInfo(_tag, "start OpenAI Realtime");

    // Seed NVS from build-time key if NVS is still empty
    {
        const char* built_in_key = CONFIG_OPENAI_API_KEY;
        if (built_in_key[0] != '\0') {
            Settings settings(_api_key_ns, true);
            if (settings.GetString(_api_key_key, "").empty()) {
                settings.SetString(_api_key_key, built_in_key);
                ESP_LOGI(_tag, "API key written to NVS from build config");
            }
        }
    }

    auto& motion = GetStackChan().motion();
    motion.setAutoAngleSyncEnabled(true);
    motion.setAutoTorqueReleaseEnabled(true);

    // Tear down boot logo and create avatar (adds blink, breath, head-pet, IMU modifiers)
    {
        LvglLockGuard lock;
        GetHAL().bootLogo.reset();
        auto* display = Board::GetInstance().GetDisplay();
        if (display) {
            display->SetupUI();
        }
    }

    // Blue = disconnected until WS connects
    _set_status_led(false);

    startNetwork([](std::string_view msg) {
        mclog::tagInfo(_tag, "{}", msg);
    });

    // Avatar + servo update task (same as xiaozhi path)
    xTaskCreatePinnedToCore(_stackchan_update_task_openai, "sc_update", 4096, nullptr, 3, nullptr, 1);

    // OpenAI WebSocket task — never returns
    xTaskCreatePinnedToCore(_openai_ws_task, "openai_ws", 8192, nullptr, 4, nullptr, 0);

    // Park this task
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
