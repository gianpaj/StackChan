/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include <smooth_ui_toolkit.hpp>
#include <uitk/short_namespace.hpp>
#include <mooncake_log.h>
#include <hal/hal.h>
#if !CONFIG_STACKCHAN_LOCAL_ITO
#include <mooncake.h>
#include <apps/apps.h>
#endif

#ifdef CONFIG_STACKCHAN_LOCAL_ITO
#include "itofs_service.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_private/startup_internal.h"
#include "esp_bit_defs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

#if !CONFIG_STACKCHAN_LOCAL_ITO
using namespace mooncake;
#endif
using namespace smooth_ui_toolkit;

#ifdef CONFIG_STACKCHAN_LOCAL_ITO
static esp_err_t early_hot_result = ESP_ERR_INVALID_STATE;
#if CONFIG_STACKCHAN_CONVERSATION
bool stackchan_reserve_local_control_stacks();
#endif
// Allocate before C++ constructors and IDF's DMA pool divide internal SRAM.
ESP_SYSTEM_INIT_FN(reserve_ito_hot, CORE, BIT(0), 106)
{
    early_hot_result = itofs_service_reserve_hot(239948);
#if CONFIG_STACKCHAN_CONVERSATION
    if (early_hot_result == ESP_OK && !stackchan_reserve_local_control_stacks()) {
        early_hot_result = ESP_ERR_NO_MEM;
    }
#endif
    // Keep ordinary UI objects in PSRAM; DMA and task stacks request internal RAM.
    heap_caps_malloc_extmem_enable(0);
    return ESP_OK;
}
#endif

extern "C" void app_main(void)
{
    // Setup logger
    mclog::set_level(mclog::level_info);
    mclog::set_time_format(mclog::time_format_unix_milliseconds);

#ifdef CONFIG_STACKCHAN_LOCAL_ITO
    ESP_LOGI("local_ito", "after early speech reservations: internal free=%u largest=%u",
             unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
    if (early_hot_result != ESP_OK) {
        ESP_LOGE("local_ito", "Cannot reserve internal speech workspaces/stacks: %s",
                 esp_err_to_name(early_hot_result));
        for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
    }
#endif

    // HAL init
    GetHAL().init();

    // Setup ui hal
    ui_hal::on_delay([](uint32_t ms) { GetHAL().delay(ms); });
    ui_hal::on_get_tick([]() { return GetHAL().millis(); });

#if !CONFIG_STACKCHAN_LOCAL_ITO
    const bool skip_mooncake =
        GetHAL().getXiaozhiConfig().startAiAgentOnBoot && GetHAL().getWarmRebootTarget() < 0;

    if (!skip_mooncake) {
        // Install apps
        GetMooncake().installApp(std::make_unique<AppLauncher>());
        GetMooncake().installApp(std::make_unique<AppAiAgent>());
        GetMooncake().installApp(std::make_unique<AppAvatar>());
        GetMooncake().installApp(std::make_unique<AppEspnowControl>());
        GetMooncake().installApp(std::make_unique<AppAppCenter>());
        GetMooncake().installApp(std::make_unique<AppEzdata>());
        GetMooncake().installApp(std::make_unique<AppDance>());
        GetMooncake().installApp(std::make_unique<AppSetup>());

        // Main loop
        while (1) {
            GetHAL().feedTheDog();
            GetHAL().updateHeapStatusLog();

            GetMooncake().update();

            if (GetHAL().isXiaozhiStartRequested()) {
                break;
            }
        }

        // Uninstall all apps and destroy mooncake
        GetMooncake().uninstallAllApps();
        DestroyMooncake();
    }
#endif

    // Start xiaozhi, never returns
    GetHAL().startXiaozhi();
}
