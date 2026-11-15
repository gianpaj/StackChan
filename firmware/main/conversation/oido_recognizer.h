#pragma once

#include <cstddef>
#include <cstdint>
#include "esp_err.h"
#include "itofs_service.h"

namespace oido_recognizer {

constexpr esp_err_t kCancelled = ESP_ERR_NOT_FINISHED;

struct Callbacks {
    bool (*cancelled)() = nullptr;
    void (*progress)() = nullptr;
};

struct Metrics {
    size_t hot_used = 0;
    size_t psram_used = 0;
    size_t allocations = 0;
    int encoder_frames = 0;
    int64_t setup_us = 0;
    int64_t feed_us = 0;
    int64_t finish_us = 0;
};

/* Recognize at most 10 seconds of signed mono 16 kHz PCM with Oido v2b,
 * chunk 8/context 64, greedy decoding and flash-resident weights, without LM.
 * The caller owns a stopped-Ito lease and PCM storage outside its ranges.
 * This call frees all recognizer state and restores global hooks before return;
 * only then may the caller return the lease and resume Ito.
 * Use at least 12 KiB of internal caller stack and measure its high-water use.
 * Cancellation is cooperative between 320-sample feed calls and before finish.
 * progress runs on this worker after completed work; it may feed the active
 * phase watchdog and must yield. Without a callback this call yields itself.
 * Output needs at least 2048 bytes outside the workspace. Once admitted, errors
 * and cancellation leave it empty. Rejected calls leave output and metrics alone.
 * Concurrent calls are rejected.
 */
esp_err_t recognize(const itofs_workspace_t& workspace, const int16_t* pcm,
                    size_t samples, char* output, size_t output_capacity,
                    const Callbacks& callbacks = {}, Metrics* metrics = nullptr);

}  // namespace oido_recognizer
