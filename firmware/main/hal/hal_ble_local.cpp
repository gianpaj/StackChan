/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "sdkconfig.h"

#ifdef CONFIG_STACKCHAN_LOCAL_ITO
#include "hal.h"
#include <mooncake_log.h>
#include <settings.h>

void Hal::ble_init(bool)
{
    mclog::tagWarn("HAL-BLE", "Bluetooth is unavailable in local Ito mode");
}

void Hal::startBleServer()
{
    ble_init(false);
}

bool Hal::isBleConnected()
{
    return false;
}

void Hal::startAppConfigServer()
{
    ble_init(true);
}

bool Hal::isAppConfiged()
{
    Settings settings("app_config", false);
    return settings.GetBool("is_configed", false);
}

void Hal::resetAppConfiged()
{
    Settings settings("app_config", true);
    settings.SetBool("is_configed", false);
}
#endif
