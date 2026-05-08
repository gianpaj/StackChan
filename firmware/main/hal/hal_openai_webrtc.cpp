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

struct OpenAiContext {
    std::unique_ptr<WebSocket> ws;
    SemaphoreHandle_t           ws_mutex;
    bool                        session_ready        = false;
    bool                        speaking             = false;
    int                         speaking_modifier_id = -1;
    uint32_t                    speaking_stop_ms     = 0;  // tick when AI stopped speaking
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

    _send_json(ctx, doc);
    ESP_LOGI(_tag, "session.update sent");
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
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(20));

        // PWR button: toggle connection on/off
        if (Board::GetInstance().IsPowerButtonPressed()) {
            if (connected) {
                ESP_LOGI(_tag, "PWR button: disconnecting");
                connected = false;
                ctx.session_ready = false;
                xSemaphoreTake(ctx.ws_mutex, portMAX_DELAY);
                ctx.ws.reset();
                xSemaphoreGive(ctx.ws_mutex);
                _set_status_led(false);
            } else {
                ESP_LOGI(_tag, "PWR button: reconnecting");
                connected = true;
                last_reconnect = 0;  // trigger immediate reconnect below
            }
        }

        if (connected && (!ctx.ws || !ctx.ws->IsConnected())) {
            uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
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
