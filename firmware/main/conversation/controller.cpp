#include "controller.h"
#include "oido_recognizer.h"
#include "hal/board/local_ito_board.h"
#include "itofs_service.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace local_conversation {
namespace {
constexpr size_t max_samples = 160000;
constexpr size_t worker_stack = 12288;
enum class Phase { idle, uploading, preparing, recording, recognizing, restoring, fault };
std::atomic<Phase> phase{Phase::idle};
std::atomic<bool> cancelled{false}, submitted{false};
TaskHandle_t worker = nullptr;
StackType_t* worker_storage = nullptr;
StaticTask_t worker_control;
int16_t* pcm = nullptr;
size_t samples = 0, limit = 0;
int64_t upload_deadline = 0;
bool microphone = false;
unsigned turn = 0;
static EXT_RAM_BSS_ATTR char transcript[2048];

const char* name(Phase p)
{
    switch (p) {
    case Phase::idle: return "idle";
    case Phase::uploading: return "uploading";
    case Phase::preparing: return "preparing";
    case Phase::recording: return "recording";
    case Phase::recognizing: return "recognizing";
    case Phase::restoring: return "restoring";
    default: return "fault";
    }
}

bool is_cancelled() { return cancelled.load(); }
void progress()
{
    esp_task_wdt_reset();
    vTaskDelay(1);
}
void set_phase(Phase p)
{
    const auto current_turn = turn;
    phase.store(p);
    printf("TURN %u state=%s internal_free=%u internal_largest=%u psram_free=%u psram_largest=%u\n",
           current_turn, name(p), unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
           unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
           unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
           unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)));
}
bool positive_count(const char* text, size_t maximum, size_t& value)
{
    if (!*text || *text == '-' || *text == '+') return false;
    errno = 0;
    char* end = nullptr;
    auto n = std::strtoul(text, &end, 10);
    if (errno || *end || n == 0 || n > maximum) return false;
    value = n;
    return true;
}

void run_turn()
{
    itofs_workspace_t workspace = {};
    printf("CONTROL_DMA free=%u largest=%u\n", unsigned(heap_caps_get_free_size(MALLOC_CAP_DMA)),
           unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_DMA)));
    auto error = itofs_service_stop(5000);
    bool stopped = error == ESP_OK;
    bool leased = false;
    bool audio_ok = true;
    transcript[0] = 0;
    if (error == ESP_OK) {
        error = itofs_service_borrow_memory(&workspace);
        leased = error == ESP_OK;
    }
    if (error == ESP_OK && microphone && !cancelled.load()) {
        const bool started = local_ito_board::start_capture();
        if (!started) {
            error = ESP_FAIL;
            // Failed setup performs cleanup; verify the stopped hardware state.
            audio_ok = local_ito_board::end_capture();
        } else {
            samples = 0;
            set_phase(Phase::recording);
            printf("LISTENING turn=%u rate=16000 max_samples=%u; send finishes, cancel discards\n",
                   turn, unsigned(limit));
            while (samples < limit && !cancelled.load() && !submitted.load()) {
                const auto count = std::min(size_t(320), limit - samples);
                if (!local_ito_board::read_capture_mono(pcm + samples, count)) {
                    error = ESP_FAIL;
                    break;
                }
                samples += count;
                progress();
            }
            // Always release RX and power, even after a failed read or cancellation.
            audio_ok = local_ito_board::end_capture();
            if (!audio_ok) error = ESP_FAIL;
            printf("CAPTURE turn=%u samples=%u duration_ms=%u capped=%d\n", turn,
                   unsigned(samples), unsigned(samples * 1000 / 16000), samples == limit);
        }
    }
    if (error == ESP_OK && !cancelled.load()) {
        set_phase(Phase::recognizing);
        progress();
        oido_recognizer::Callbacks callbacks{is_cancelled, progress};
        oido_recognizer::Metrics metrics;
        error = oido_recognizer::recognize(workspace, pcm, samples, transcript,
                                           sizeof(transcript), callbacks, &metrics);
        printf("STT turn=%u hot=%u psram=%u allocations=%u frames=%d setup_ms=%.1f feed_ms=%.1f finish_ms=%.1f\n",
               turn, unsigned(metrics.hot_used), unsigned(metrics.psram_used), unsigned(metrics.allocations),
               metrics.encoder_frames, metrics.setup_us / 1000.0, metrics.feed_us / 1000.0,
               metrics.finish_us / 1000.0);
    }
    // Recognition has returned and destroyed all model/stream users of the lease.
    set_phase(Phase::restoring);
    if (leased) {
        auto returned = itofs_service_return_memory(&workspace);
        if (returned != ESP_OK) { error = returned; audio_ok = false; }
    }
    // A timed-out stop can still own workers. Retry cooperatively before resuming.
    if (!stopped) {
        for (int attempt = 0; attempt < 6 && !stopped; ++attempt) {
            progress();
            auto retry = itofs_service_stop(5000);
            stopped = retry == ESP_OK;
            if (retry != ESP_ERR_TIMEOUT && retry != ESP_OK) break;
        }
    }
    progress();
    const auto resumed = stopped && audio_ok ? itofs_service_resume() : ESP_FAIL;
    progress();
    if (resumed != ESP_OK) {
        transcript[0] = 0;
        set_phase(Phase::fault);
        printf("ERROR conversation recovery: %s; reboot required\n", esp_err_to_name(resumed));
        return;
    }
    if (cancelled.load()) {
        transcript[0] = 0;
        printf("CANCELLED turn=%u\n", turn);
    } else if (error != ESP_OK) {
        transcript[0] = 0;
        printf("ERROR recognition turn=%u: %s\n", turn, esp_err_to_name(error));
    } else {
        printf("TRANSCRIPT turn=%u: %s\n", turn, transcript[0] ? transcript : "[no speech]");
    }
    printf("CONVERSATION stack_free=%u of=%u bytes\n",
           unsigned(uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t)), unsigned(worker_stack));
    set_phase(Phase::idle);
    printf("CONVERSATION READY: echo repeats the transcript; text <answer> speaks a reply\n");
}

void task(void*)
{
    // This task's subscription is reset only by this owner and recognizer progress.
    ESP_ERROR_CHECK(esp_task_wdt_add(nullptr));
    for (;;) {
        esp_task_wdt_reset();
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100))) run_turn();
    }
}

bool begin(bool mic, size_t count)
{
    auto expected = Phase::idle;
    if (!worker || !phase.compare_exchange_strong(expected, Phase::preparing)) {
        printf("ERROR conversation busy: %s\n", name(phase.load()));
        return false;
    }
    cancelled.store(false);
    submitted.store(false);
    microphone = mic;
    samples = 0;
    limit = count;
    transcript[0] = 0;
    ++turn;
    if (mic) xTaskNotifyGive(worker);
    else {
        upload_deadline = esp_timer_get_time() + 30000000;
        set_phase(Phase::uploading);
        printf("OK pcm begin turn=%u samples=%u\n", turn, unsigned(limit));
    }
    return true;
}
int unhex(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
}

bool reserve_worker_stack()
{
    if (!worker_storage) worker_storage = static_cast<StackType_t*>(
        heap_caps_malloc(worker_stack, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    return worker_storage != nullptr;
}

bool start()
{
    pcm = static_cast<int16_t*>(heap_caps_malloc(max_samples * sizeof(int16_t), MALLOC_CAP_SPIRAM));
    if (!pcm) return false;
    if (!worker_storage) { heap_caps_free(pcm); pcm = nullptr; return false; }
    worker = xTaskCreateStaticPinnedToCore(task, "conversation", worker_stack, nullptr, 4,
                                           worker_storage, &worker_control, 0);
    if (!worker) {
        heap_caps_free(pcm);
        pcm = nullptr;
        return false;
    }
    printf("LOCAL CONVERSATION: listen [1..10 seconds], send, cancel, echo, text <reply>\n");
    return true;
}

void poll()
{
    if (phase.load() == Phase::uploading && esp_timer_get_time() > upload_deadline) {
        set_phase(Phase::idle);
        printf("ERROR pcm upload timed out\n");
    }
}

bool command(const char* line)
{
    poll();
    auto p = phase.load();
    if (!std::strcmp(line, "listen") || !std::strncmp(line, "listen ", 7)) {
        size_t seconds = 10;
        if (line[6] && !positive_count(line + 7, 10, seconds)) printf("ERROR listen requires 1..10 seconds\n");
        else begin(true, seconds * 16000);
        return true;
    }
    if (!std::strncmp(line, "pcm begin ", 10)) {
        size_t count;
        if (!positive_count(line + 10, max_samples, count)) printf("ERROR pcm samples must be 1..160000\n");
        else begin(false, count);
        return true;
    }
    if (!std::strncmp(line, "pcm data ", 9)) {
        auto data = line + 9;
        const size_t n = std::strlen(data);
        if (p != Phase::uploading || !n || n % 4 || n / 4 > limit - samples) {
            printf("ERROR pcm data state, alignment or length\n");
            return true;
        }
        for (size_t i = 0; i < n; ++i) if (unhex(data[i]) < 0) {
            printf("ERROR pcm data requires hex PCM16LE\n");
            return true;
        }
        for (size_t i = 0; i < n; i += 4) {
            const unsigned low = unhex(data[i]) * 16 + unhex(data[i + 1]);
            const unsigned high = unhex(data[i + 2]) * 16 + unhex(data[i + 3]);
            pcm[samples++] = int16_t(low | high << 8);
        }
        upload_deadline = esp_timer_get_time() + 30000000;
        printf("OK pcm data samples=%u\n", unsigned(samples));
        return true;
    }
    if (!std::strcmp(line, "send")) {
        if (p == Phase::recording) { submitted.store(true); printf("OK send\n"); }
        else if (p == Phase::uploading && samples == limit) {
            phase.store(Phase::preparing);
            xTaskNotifyGive(worker);
            printf("OK send\n");
        } else printf("ERROR send needs a recording or complete PCM upload\n");
        return true;
    }
    if (!std::strcmp(line, "cancel") && p != Phase::idle && p != Phase::fault) {
        cancelled.store(true);
        if (p == Phase::uploading) { transcript[0] = 0; set_phase(Phase::idle); }
        printf("OK cancel turn=%u\n", turn);
        return true;
    }
    if (!std::strcmp(line, "echo")) {
        if (p != Phase::idle || cancelled.load() || !transcript[0]) printf("ERROR echo needs a completed transcript\n");
        else {
            // Never interpret recognized speech as a console command.
            static EXT_RAM_BSS_ATTR char speak[2054];
            std::snprintf(speak, sizeof(speak), "text %s", transcript);
            auto error = itofs_service_command(speak);
            if (error != ESP_OK) printf("ERROR echo: %s\n", esp_err_to_name(error));
        }
        return true;
    }
    if (!std::strcmp(line, "status")) {
        printf("CONVERSATION turn=%u state=%s\n", turn, name(p));
        return p != Phase::idle;
    }
    if (p != Phase::idle && std::strcmp(line, "memory") && std::strcmp(line, "reboot") &&
        std::strcmp(line, "cancel")) {
        printf("ERROR conversation busy: %s\n", name(p));
        return true;
    }
    return false;
}
}
