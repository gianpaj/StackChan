#include "oido_recognizer.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include "esp_partition.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"
#include "tasr_nemo.h"
#include "tinyasr.h"

extern "C" int tasr_wpf;

namespace oido_recognizer {
namespace {

constexpr size_t kModelBytes = 5931424;
constexpr size_t kMaxSamples = 10 * 16000;
constexpr size_t kTextBytes = 2048;
constexpr size_t kMinimumHot = 224 * 1024;
constexpr size_t kMinimumPsram = 2 * 1024 * 1024;
constexpr int64_t kProgressIntervalUs = 100000;
constexpr uint8_t kModelSha256[] = {
    0x9e, 0x8b, 0x40, 0x91, 0x3f, 0x53, 0x2f, 0xb4,
    0x4b, 0x71, 0x6a, 0x1f, 0x88, 0x2f, 0x38, 0xb3,
    0xb2, 0x1e, 0x22, 0x07, 0x13, 0xe4, 0x17, 0x89,
    0x9a, 0xba, 0x32, 0xac, 0xe8, 0xc3, 0x50, 0x66,
};

struct Arena {
    uint8_t* base;
    size_t capacity;
    size_t used = 0;

    void* allocate(size_t bytes)
    {
        const size_t offset = (used + 15) & ~size_t(15);
        bytes = bytes ? bytes : 16;
        if (offset > capacity || bytes > capacity - offset) return nullptr;
        void* result = base + offset;
        std::memset(result, 0, bytes);
        used = offset + bytes;
        return result;
    }

    bool contains(const void* pointer) const
    {
        const uintptr_t address = reinterpret_cast<uintptr_t>(pointer);
        const uintptr_t start = reinterpret_cast<uintptr_t>(base);
        return address >= start && address - start < capacity;
    }
};

struct Run {
    Arena hot;
    Arena psram;
    Callbacks callbacks;
    Metrics metrics;
    int64_t last_progress = 0;
    bool allocation_failed = false;
    bool invalid_free = false;
    bool mapped = false;
    bool hooks_installed = false;
    esp_partition_mmap_handle_t mapping = 0;
    const uint8_t* blob = nullptr;
    tasr_nemo_t* model = nullptr;
    tasr_nemo_stream_t* stream = nullptr;
    void* (*saved_alloc)(size_t, int) = nullptr;
    void (*saved_free)(void*) = nullptr;
    void (*saved_parallel)(tasr_job_fn, void*, int) = nullptr;
    uint32_t saved_mask = 0;
    int saved_wpf = 0;
};

std::atomic_flag busy = ATOMIC_FLAG_INIT;
Run* active_run = nullptr;

bool cancelled(const Run& run)
{
    return run.callbacks.cancelled && run.callbacks.cancelled();
}

void progress(Run& run)
{
    const int64_t now = esp_timer_get_time();
    if (now - run.last_progress < kProgressIntervalUs) return;
    run.last_progress = now;
    if (run.callbacks.progress) run.callbacks.progress();
    else vTaskDelay(1);
}

void* allocate(size_t bytes, int kind)
{
    if (!active_run) return nullptr;
    ++active_run->metrics.allocations;
    void* result = nullptr;
    if (kind == 0 || kind == 1)
        result = (kind == 1 ? active_run->hot : active_run->psram).allocate(bytes);
    if (!result) active_run->allocation_failed = true;
    return result;
}

void release(void* pointer)
{
    /* The whole phase is released together. Upstream destructors still run so
     * no recognizer object remains active when the caller returns its lease. */
    if (pointer && active_run && !active_run->hot.contains(pointer) &&
        !active_run->psram.contains(pointer)) active_run->invalid_free = true;
}

void serial_parallel(tasr_job_fn function, void* context, int count)
{
    function(context, 0, count, 0);
    if (active_run) progress(*active_run);
}

bool overlaps(const void* first, size_t first_bytes, const void* second, size_t second_bytes)
{
    const uintptr_t a = reinterpret_cast<uintptr_t>(first);
    const uintptr_t b = reinterpret_cast<uintptr_t>(second);
    if (first_bytes > UINTPTR_MAX - a || second_bytes > UINTPTR_MAX - b) return true;
    return a < b + second_bytes && b < a + first_bytes;
}

bool touches_workspace(const itofs_workspace_t& workspace, const void* pointer, size_t bytes)
{
    return overlaps(pointer, bytes, workspace.hot, workspace.hot_bytes) ||
           overlaps(pointer, bytes, workspace.psram, workspace.psram_bytes);
}

esp_err_t verify_model(Run& run)
{
    /* The upstream parser expects a trusted blob. Check the entire approved
     * model before parsing any dimensions or tensor offsets. */
    mbedtls_sha256_context hash;
    mbedtls_sha256_init(&hash);
    int result = mbedtls_sha256_starts(&hash, 0);
    esp_err_t error = ESP_OK;
    for (size_t offset = 0; result == 0 && offset < kModelBytes; offset += 65536) {
        if (cancelled(run)) { error = kCancelled; break; }
        const size_t count = std::min(size_t(65536), kModelBytes - offset);
        result = mbedtls_sha256_update(&hash, run.blob + offset, count);
        progress(run);
    }
    uint8_t digest[32];
    if (error == ESP_OK && result == 0) result = mbedtls_sha256_finish(&hash, digest);
    mbedtls_sha256_free(&hash);
    if (error != ESP_OK) return error;
    if (result != 0) return ESP_FAIL;
    return std::memcmp(digest, kModelSha256, sizeof digest) == 0 ? ESP_OK : ESP_ERR_INVALID_CRC;
}

esp_err_t run_recognition(Run& run, const int16_t* pcm, size_t samples, char* output)
{
    const int64_t setup_start = esp_timer_get_time();
    if (cancelled(run)) return kCancelled;
    const esp_partition_t* partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "oido");
    if (!partition) return ESP_ERR_NOT_FOUND;
    if (partition->size < kModelBytes) return ESP_ERR_INVALID_SIZE;
    const void* mapping = nullptr;
    esp_err_t error = esp_partition_mmap(partition, 0, kModelBytes,
        ESP_PARTITION_MMAP_DATA, &mapping, &run.mapping);
    if (error != ESP_OK) return error;
    run.mapped = true;
    run.blob = static_cast<const uint8_t*>(mapping);
    error = verify_model(run);
    if (error != ESP_OK) return error;
    if (cancelled(run)) return kCancelled;

    run.saved_alloc = tasr_alloc;
    run.saved_free = tasr_free;
    run.saved_parallel = tasr_parallel;
    run.saved_mask = tasr_par_mask;
    run.saved_wpf = tasr_wpf;
    active_run = &run;
    tasr_alloc = allocate;
    tasr_free = release;
    tasr_parallel = serial_parallel;
    tasr_par_mask = UINT32_MAX;
    tasr_wpf = 0;
    run.hooks_installed = true;

    run.model = tasr_nemo_load(run.blob, kModelBytes);
    if (!run.model) return run.allocation_failed ? ESP_ERR_NO_MEM : ESP_ERR_INVALID_RESPONSE;
    if (!tasr_nemo_stream_supported(run.model)) return ESP_ERR_NOT_SUPPORTED;
    progress(run);
    if (cancelled(run)) return kCancelled;
    run.stream = tasr_nemo_stream_new(run.model, 8, 64, nullptr);
    if (!run.stream) return run.allocation_failed ? ESP_ERR_NO_MEM : ESP_ERR_INVALID_RESPONSE;
    run.metrics.setup_us = esp_timer_get_time() - setup_start;

    const int64_t feed_start = esp_timer_get_time();
    for (size_t offset = 0; offset < samples; offset += 320) {
        if (cancelled(run)) return kCancelled;
        const int count = static_cast<int>(std::min(size_t(320), samples - offset));
        run.metrics.encoder_frames = tasr_nemo_stream_feed(run.stream, pcm + offset, count);
        if (run.metrics.encoder_frames < 0) return ESP_FAIL;
        progress(run);
    }
    run.metrics.feed_us = esp_timer_get_time() - feed_start;
    if (cancelled(run)) return kCancelled;
    const int64_t finish_start = esp_timer_get_time();
    run.metrics.encoder_frames = tasr_nemo_stream_finish(run.stream, output, kTextBytes);
    run.metrics.finish_us = esp_timer_get_time() - finish_start;
    progress(run);
    if (cancelled(run)) return kCancelled;
    return run.metrics.encoder_frames < 0 ? ESP_FAIL : ESP_OK;
}

void close_run(Run& run)
{
    if (run.stream) tasr_nemo_stream_free(run.stream);
    if (run.model) tasr_nemo_free(run.model);
    if (run.hooks_installed) {
        tasr_alloc = run.saved_alloc;
        tasr_free = run.saved_free;
        tasr_parallel = run.saved_parallel;
        tasr_par_mask = run.saved_mask;
        tasr_wpf = run.saved_wpf;
        active_run = nullptr;
    }
    if (run.mapped) esp_partition_munmap(run.mapping);
    run.stream = nullptr;
    run.model = nullptr;
    run.blob = nullptr;
}

}  // namespace

esp_err_t recognize(const itofs_workspace_t& workspace, const int16_t* pcm, size_t samples,
                    char* output, size_t output_capacity, const Callbacks& callbacks,
                    Metrics* metrics)
{
    if (!pcm || !samples || samples > kMaxSamples || !output) return ESP_ERR_INVALID_ARG;
    if (output_capacity < kTextBytes || output_capacity > static_cast<size_t>(INT32_MAX))
        return ESP_ERR_INVALID_SIZE;
    if (!workspace.hot || !workspace.psram || workspace.hot_bytes < kMinimumHot ||
        workspace.psram_bytes < kMinimumPsram) return ESP_ERR_NO_MEM;
    if ((reinterpret_cast<uintptr_t>(workspace.hot) & 15) ||
        (reinterpret_cast<uintptr_t>(workspace.psram) & 15) ||
        overlaps(workspace.hot, workspace.hot_bytes, workspace.psram, workspace.psram_bytes) ||
        touches_workspace(workspace, pcm, samples * sizeof(int16_t)) ||
        touches_workspace(workspace, output, output_capacity)) return ESP_ERR_INVALID_ARG;
    if (busy.test_and_set(std::memory_order_acquire)) return ESP_ERR_INVALID_STATE;
    if (metrics) *metrics = {};
    output[0] = '\0';

    Run run{{static_cast<uint8_t*>(workspace.hot), workspace.hot_bytes},
            {static_cast<uint8_t*>(workspace.psram), workspace.psram_bytes}, callbacks, {}};
    esp_err_t error = run_recognition(run, pcm, samples, output);
    close_run(run);
    run.metrics.hot_used = run.hot.used;
    run.metrics.psram_used = run.psram.used;
    if (run.invalid_free && error == ESP_OK) error = ESP_FAIL;
    if (metrics) *metrics = run.metrics;
    if (error != ESP_OK) output[0] = '\0';
    busy.clear(std::memory_order_release);
    return error;
}

}  // namespace oido_recognizer
