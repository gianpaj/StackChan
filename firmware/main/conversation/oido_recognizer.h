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

/* One worker owns a stopped-Ito workspace until close() returns. begin() checks
 * the complete model before capture; feed() consumes 1..320 mono 16 kHz samples
 * before returning, with a total bound of 160000 samples. PCM and transcript
 * storage must be outside both workspace ranges. finish() needs 2048 bytes.
 *
 * The session holds the process-wide Oido allocator and parallel hooks. Another
 * session is rejected until close(). Call its methods on one worker only.
 * Cancellation is cooperative between feed calls and before/after finish.
 * progress runs after completed work, must yield, and may feed the watchdog.
 * Without a progress callback the adapter yields itself. Use at least 12 KiB
 * of internal worker stack and measure its high-water use.
 *
 * begin() failures clean up immediately. feed()/finish() failures require close()
 * before the caller returns the lease. close() is idempotent, reports metrics
 * even after a failed begin(), and restores hooks and mappings before return.
 */
class Session {
public:
    Session() = default;
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    esp_err_t begin(const itofs_workspace_t& workspace, const Callbacks& callbacks = {});
    esp_err_t feed(const int16_t* pcm, size_t samples);
    esp_err_t finish(char* output, size_t output_capacity);
    esp_err_t close(Metrics* metrics = nullptr);

private:
    bool owns_run_ = false;
    esp_err_t result_ = ESP_OK;
    Metrics metrics_{};
};

/* Batch compatibility wrapper with chunk 8/context 64, greedy decoding, no LM,
 * and flash-resident Oido v2b weights. Rejected calls leave output and metrics
 * unchanged. Admitted errors leave output empty. All state closes before return.
 */
esp_err_t recognize(const itofs_workspace_t& workspace, const int16_t* pcm,
                    size_t samples, char* output, size_t output_capacity,
                    const Callbacks& callbacks = {}, Metrics* metrics = nullptr);

}  // namespace oido_recognizer
