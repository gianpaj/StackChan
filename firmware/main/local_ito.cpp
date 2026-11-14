#include "sdkconfig.h"

#if CONFIG_STACKCHAN_LOCAL_ITO
#include "itofs_service.h"
#include "hal/board/local_ito_board.h"
#include <hal/hal.h>
#include <stackchan/stackchan.h>
#include <apps/common/common.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {
constexpr const char* tag = "local_ito";
esp_err_t result(bool ok) { return ok ? ESP_OK : ESP_FAIL; }

void report_memory(const char* stage)
{
    ESP_LOGI(tag, "%s: internal free=%u largest=%u; PSRAM free=%u largest=%u", stage,
             unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
             unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
             unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)));
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
    config.start_console = true;
    const auto error = itofs_service_start(&config);
    if (error != ESP_OK) {
        ESP_LOGE(tag, "Speech startup failed: %s; remaining in the avatar screen", esp_err_to_name(error));
    }
    for (;;) {
        GetHAL().feedTheDog();
        {
            LvglLockGuard lock;
            GetStackChan().update();
            view::update_home_indicator();
            view::update_status_bar();
        }
        GetHAL().updateHeapStatusLog();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
#endif
