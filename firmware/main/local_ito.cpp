#include "sdkconfig.h"

#if CONFIG_STACKCHAN_LOCAL_ITO
#include "itofs_service.h"
#include "hal/board/local_ito_board.h"
#include <hal/hal.h>
#include <stackchan/stackchan.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_attr.h>
#include <esp_system.h>
#include <hal/usb_serial_jtag_ll.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstdio>
#include <cstring>

namespace {
constexpr const char* tag = "local_ito";
esp_err_t result(bool ok) { return ok ? ESP_OK : ESP_FAIL; }

void report_memory(const char* stage)
{
    ESP_LOGI(tag, "%s: internal free=%u largest=%u min=%u; PSRAM free=%u largest=%u min=%u", stage,
             unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
             unsigned(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)),
             unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
             unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)),
             unsigned(heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM)));
}

esp_err_t probe_workspace()
{
    void* hot = nullptr;
    size_t bytes = 0;
    auto error = itofs_service_borrow_hot(&hot, &bytes);
    if (error != ESP_OK) return error;
    // Model the next engine's exclusive ownership, without changing flash.
    std::memset(hot, 0xa5, bytes);
    constexpr size_t psram_bytes = 3 * 1024 * 1024;
    auto* psram = static_cast<uint8_t*>(heap_caps_malloc(psram_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (psram) {
        std::memset(psram, 0x5a, psram_bytes);
        volatile uint8_t* check = psram;
        for (size_t i = 0; i < psram_bytes; ++i) {
            if (check[i] != 0x5a) { error = ESP_FAIL; break; }
        }
        heap_caps_free(psram);
    } else {
        error = ESP_ERR_NO_MEM;
    }
    const auto returned = itofs_service_return_hot(hot);
    if (error == ESP_OK) error = returned;
    if (error == ESP_OK) printf("WORKSPACE internal=%u psram=%u checked\n", unsigned(bytes), unsigned(psram_bytes));
    return error;
}

void command(const char* line)
{
    esp_err_t error = ESP_OK;
    if (!std::strcmp(line, "memory")) {
        report_memory("console");
        printf("OK memory\n");
        return;
    }
    if (!std::strcmp(line, "reboot")) {
        printf("rebooting\n");
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_restart();
    }
    if (!std::strcmp(line, "tts stop")) {
        error = itofs_service_stop(5000);
        report_memory(error == ESP_OK ? "tts stopped" : "tts stop failed");
    } else if (!std::strcmp(line, "tts resume")) {
        error = itofs_service_resume();
        report_memory(error == ESP_OK ? "tts resumed" : "tts resume failed");
    } else if (!std::strcmp(line, "tts probe")) {
        error = probe_workspace();
        report_memory("workspace returned");
    } else {
        error = itofs_service_command(line);
        if (error != ESP_OK) printf("ERROR command: %s\n", esp_err_to_name(error));
        return;
    }
    printf("%s %s: %s\n", error == ESP_OK ? "OK" : "ERROR", line, esp_err_to_name(error));
}

void console_task(void*)
{
    static EXT_RAM_BSS_ATTR char line[4096];
    size_t length = 0;
    bool overflow = false;
    for (;;) {
        uint8_t input[64];
        size_t count = 0;
        while (count < sizeof(input) && usb_serial_jtag_ll_rxfifo_data_available()) {
            count += usb_serial_jtag_ll_read_rxfifo(input + count, sizeof(input) - count);
        }
        for (size_t i = 0; i < count; ++i) {
            if (input[i] == '\r' || input[i] == '\n') {
                line[length] = 0;
                if (overflow) printf("ERROR command exceeds 4095 bytes\n");
                else if (length) command(line);
                length = 0;
                overflow = false;
            } else if (length < sizeof(line) - 1) {
                line[length++] = char(input[i]);
            } else {
                overflow = true;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
}

void stackchan_start_local_ito()
{
    report_memory("before avatar");
    ESP_ERROR_CHECK(result(local_ito_board::prepare_avatar()));
    report_memory("after avatar");
    itofs_service_config_t config = {};
    config.open = [](void*, int sr) { return result(local_ito_board::initialize_audio(sr) && local_ito_board::set_volume(35)); };
    config.write = [](void*, const int16_t* pcm, size_t count) {
        return result(local_ito_board::write_mono(pcm, count));
    };
    config.drain = [](void*) { return result(local_ito_board::drain()); };
    config.flush = [](void*) { return result(local_ito_board::flush()); };
    config.mute = [](void*, bool muted) { return result(local_ito_board::mute(muted)); };
    config.set_volume = [](void*, int volume) { return result(local_ito_board::set_volume(volume)); };
    config.playing = [](void*, bool playing) {
        local_ito_board::set_speaking(playing);
        ESP_LOGI(tag, "speaker playing=%d", playing);
    };
    // Let the subscribed IDLE task run during silent self-tests and calibration.
    // ESP-IDF owns the watchdog; Ito does not reconfigure RWDT.
    config.watchdog_feed = [](void*) {
        static TickType_t last_yield[portNUM_PROCESSORS] = {};
        const auto core = xPortGetCoreID();
        const TickType_t tick = xTaskGetTickCount();
        if (TickType_t(tick - last_yield[core]) >= pdMS_TO_TICKS(1000)) {
            last_yield[core] = tick;
            vTaskDelay(1);
        }
    };
    // The app console stays available while inference memory is lent to STT.
    config.start_console = false;
    const auto error = itofs_service_start(&config);
    if (error != ESP_OK) {
        ESP_LOGE(tag, "Speech startup failed: %s; remaining in the avatar screen", esp_err_to_name(error));
    }
    if (xTaskCreatePinnedToCore(console_task, "local_console", 4096, nullptr, 4, nullptr, 0) != pdPASS) {
        ESP_LOGE(tag, "Cannot start the USB control task");
    }
    for (;;) {
        GetHAL().feedTheDog();
        {
            LvglLockGuard lock;
            GetStackChan().update();
        }
        GetHAL().updateHeapStatusLog();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
#endif
