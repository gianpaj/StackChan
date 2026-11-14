/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include <smooth_ui_toolkit.hpp>
#include <uitk/short_namespace.hpp>
#include <mooncake_log.h>
#include <mooncake.h>
#include <apps/apps.h>
#include <hal/hal.h>

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

using namespace mooncake;
using namespace smooth_ui_toolkit;

#ifdef CONFIG_STACKCHAN_LOCAL_ITO
static esp_err_t early_hot_result = ESP_ERR_INVALID_STATE;
// Allocate before C++ constructors and IDF's DMA pool divide internal SRAM.
ESP_SYSTEM_INIT_FN(reserve_ito_hot, CORE, BIT(0), 106)
{
    early_hot_result = itofs_service_reserve_hot(239948);
    return ESP_OK;
}
#endif

extern "C" void app_main(void)
{
    // Setup logger
    mclog::set_level(mclog::level_info);
    mclog::set_time_format(mclog::time_format_unix_milliseconds);

#ifdef CONFIG_STACKCHAN_LOCAL_ITO
    ESP_LOGI("local_ito", "after early hot reserve: internal free=%u largest=%u",
             unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
    if (early_hot_result != ESP_OK) {
        ESP_LOGE("local_ito", "Cannot reserve 239948 bytes of internal SRAM: %s",
                 esp_err_to_name(early_hot_result));
        for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
    }
#endif

    // HAL init
    GetHAL().init();

    // Setup ui hal
    ui_hal::on_delay([](uint32_t ms) { GetHAL().delay(ms); });
    ui_hal::on_get_tick([]() { return GetHAL().millis(); });

    const bool skip_mooncake =
#ifdef CONFIG_STACKCHAN_LOCAL_ITO
        GetHAL().getWarmRebootTarget() < 0;
#else
        GetHAL().getXiaozhiConfig().startAiAgentOnBoot && GetHAL().getWarmRebootTarget() < 0;
#endif

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

    // Start xiaozhi, never returns
    GetHAL().startXiaozhi();
}
