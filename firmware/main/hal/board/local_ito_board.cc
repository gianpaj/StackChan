#include "sdkconfig.h"

#if CONFIG_STACKCHAN_LOCAL_ITO
#include "local_ito_board.h"
#include "cores3_audio_codec.h"
#include "hal_bridge.h"
#include <board.h>
#include <display.h>
#include <hal/hal.h>
#include <assets/lang_config.h>
#include <stackchan/stackchan.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_timer.h>
#include <esp_log.h>
#include <mutex>
#include <algorithm>

namespace local_ito_board {
namespace {
std::mutex audio_mutex;
CoreS3AudioCodec* codec = nullptr;
uint32_t playback_epoch = 0;
CaptureStats microphone_stats;
int64_t capture_started_us = 0;
}

bool prepare_avatar()
{
    auto display = Board::GetInstance().GetDisplay();
    if (!display) {
        return false;
    }
    display->SetupUI();
    {
        LvglLockGuard lock;
        if (!GetStackChan().hasAvatar()) {
            return false;
        }
        GetHAL().bootLogo.reset();
    }
    display->SetStatus(Lang::Strings::STANDBY);
    return true;
}

bool initialize_audio(int sample_rate)
{
    std::lock_guard<std::mutex> lock(audio_mutex);
    codec = static_cast<CoreS3AudioCodec*>(Board::GetInstance().GetAudioCodec());
    return codec && codec->LocalOpen(sample_rate);
}

bool write_mono(const int16_t* pcm, size_t samples)
{
    if (!pcm || samples == 0) {
        return false;
    }
    uint32_t epoch;
    {
        std::lock_guard<std::mutex> lock(audio_mutex);
        if (!codec) {
            return false;
        }
        epoch = playback_epoch;
    }
    while (samples > 0) {
        const size_t chunk = std::min(samples, static_cast<size_t>(AUDIO_CODEC_DMA_FRAME_NUM));
        {
            std::lock_guard<std::mutex> lock(audio_mutex);
            if (epoch != playback_epoch || !codec->LocalWrite(pcm, chunk)) {
                return false;
            }
        }
        pcm += chunk;
        samples -= chunk;
    }
    return true;
}

bool drain()
{
    uint32_t epoch;
    {
        std::lock_guard<std::mutex> lock(audio_mutex);
        if (!codec) {
            return false;
        }
        epoch = playback_epoch;
    }
    while (true) {
        {
            std::lock_guard<std::mutex> lock(audio_mutex);
            if (epoch != playback_epoch) {
                return false;
            }
            if (!codec->LocalPlaying()) {
                return true;
            }
        }
        vTaskDelay(1);
    }
}

bool flush()
{
    std::lock_guard<std::mutex> lock(audio_mutex);
    ++playback_epoch;
    return codec && codec->LocalFlush();
}

bool mute(bool muted)
{
    std::lock_guard<std::mutex> lock(audio_mutex);
    return codec && codec->LocalMute(muted);
}

bool set_volume(int volume)
{
    std::lock_guard<std::mutex> lock(audio_mutex);
    return codec && codec->LocalSetVolume(volume);
}

void set_speaking(bool speaking)
{
    Board::GetInstance().GetDisplay()->SetStatus(speaking ? Lang::Strings::SPEAKING : Lang::Strings::STANDBY);
}

bool start_capture()
{
    std::lock_guard<std::mutex> lock(audio_mutex);
    if (!codec || microphone_stats.active) {
        return false;
    }
    ++playback_epoch;
    microphone_stats = {};
    if (!codec->LocalStartCapture()) {
        return false;
    }
    capture_started_us = esp_timer_get_time();
    microphone_stats.active = true;
    Board::GetInstance().GetDisplay()->SetStatus(Lang::Strings::LISTENING);
    return true;
}

bool read_capture_mono(int16_t* pcm, size_t samples)
{
    std::lock_guard<std::mutex> lock(audio_mutex);
    if (!microphone_stats.active || !pcm || samples == 0 || samples > 320 ||
        samples > 160000 - microphone_stats.samples) {
        return false;
    }
    if (!codec->LocalReadCapture(pcm, samples)) {
        return false;
    }
    for (size_t i = 0; i < samples; ++i) {
        const int32_t sample = pcm[i];
        const uint16_t magnitude = sample < 0 ? -sample : sample;
        microphone_stats.peak = std::max(microphone_stats.peak, magnitude);
        if (sample == -32768 || sample == 32767) {
            ++microphone_stats.clipped_samples;
        }
    }
    microphone_stats.samples += samples;
    return true;
}

bool end_capture()
{
    std::lock_guard<std::mutex> lock(audio_mutex);
    if (!codec) {
        return false;
    }
    if (microphone_stats.active) {
        microphone_stats.duration_ms = (esp_timer_get_time() - capture_started_us) / 1000;
    }
    Board::GetInstance().GetDisplay()->SetStatus(Lang::Strings::STANDBY);
    const bool ok = codec->LocalEndCapture();
    microphone_stats.active = false;
    ESP_LOGI("microphone", "samples=%u peak=%u clipped=%u elapsed_ms=%u cleanup=%d",
             static_cast<unsigned>(microphone_stats.samples), microphone_stats.peak,
             static_cast<unsigned>(microphone_stats.clipped_samples),
             static_cast<unsigned>(microphone_stats.duration_ms), ok);
    return ok;
}

CaptureStats capture_stats()
{
    std::lock_guard<std::mutex> lock(audio_mutex);
    auto stats = microphone_stats;
    if (stats.active) {
        stats.duration_ms = (esp_timer_get_time() - capture_started_us) / 1000;
    }
    return stats;
}

}  // namespace local_ito_board
#endif
