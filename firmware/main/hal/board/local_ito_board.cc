#include "sdkconfig.h"

#if CONFIG_STACKCHAN_LOCAL_ITO
#include "local_ito_board.h"
#include "cores3_audio_codec.h"
#include "hal_bridge.h"
#include <board.h>
#include <display.h>
#include <hal/hal.h>
#include <assets/lang_config.h>
#include <apps/common/common.h>
#include <stackchan/stackchan.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mutex>
#include <algorithm>

namespace local_ito_board {
namespace {
std::mutex audio_mutex;
CoreS3AudioCodec* codec = nullptr;
uint32_t playback_epoch = 0;
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
        view::create_home_indicator([]() { GetHAL().requestWarmReboot(0); }, 0x81DBBD, 0x134233);
        view::create_status_bar(0x81DBBD, 0x134233);
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

}  // namespace local_ito_board
#endif
