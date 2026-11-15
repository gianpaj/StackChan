/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_setup.h"
#include <hal/hal.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <assets/assets.h>
#include <stackchan/stackchan.h>
#include <apps/common/common.h>

using namespace mooncake;
using namespace view;
using namespace setup_workers;

#if CONFIG_STACKCHAN_LOCAL_ITO
static void show_local_mode_unavailable()
{
    view::pop_a_toast("Unavailable in local speech mode", view::ToastType::Warning);
}
#endif

AppSetup::AppSetup()
{
    // 配置 App 名
    setAppInfo().name = "SETUP";
    // 配置 App 图标
    static auto icon  = assets::get_image("icon_setup.bin");
    setAppInfo().icon = (void*)&icon;
    // 配置 App 主题颜色
    static uint32_t theme_color = 0xB3B3B3;
    setAppInfo().userData       = (void*)&theme_color;
}

void AppSetup::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
    // open();
}

void AppSetup::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    // Reset state
    _destroy_menu    = false;
    _need_warm_reset = false;
    _magic_count     = 0;

    _menu_sections = {
        {
            "Wi-Fi",
            {{"Change Wi-Fi",
              [&]() {
#if CONFIG_STACKCHAN_LOCAL_ITO
                  show_local_mode_unavailable();
#else
                  _destroy_menu    = true;
                  _need_warm_reset = true;
                  _worker          = std::make_unique<WifiSetupWorker>();
#endif
              }},
             {"Testing service",
              [&]() {
#if CONFIG_STACKCHAN_LOCAL_ITO
                  show_local_mode_unavailable();
#else
                  _destroy_menu = true;
                  _worker       = std::make_unique<NetworkTestWorker>();
#endif
              }}},
        },
        {
            "Device",
            {{"Brightness",
              [&]() {
                  _destroy_menu = true;
                  _worker       = std::make_unique<BrightnessSetupWorker>();
              }},
             {"Volume",
              [&]() {
                  _destroy_menu = true;
                  _worker       = std::make_unique<VolumeSetupWorker>();
              }},
             {"Timezone",
              [&]() {
                  _destroy_menu = true;
                  _worker       = std::make_unique<TimezoneWorker>();
              }}},
        },
        {
            "AI.Agent",
            {{"General",
              [&]() {
#if CONFIG_STACKCHAN_LOCAL_ITO
                  show_local_mode_unavailable();
#else
                  _destroy_menu    = true;
                  _need_warm_reset = true;
                  _worker          = std::make_unique<XiaozhiGeneralWorker>();
#endif
              }},
             {"Power Saving",
              [&]() {
#if CONFIG_STACKCHAN_LOCAL_ITO
                  show_local_mode_unavailable();
#else
                  _destroy_menu    = true;
                  _need_warm_reset = true;
                  _worker          = std::make_unique<XiaozhiPowerSavingWorker>();
#endif
              }}},
        },
        {
            "Hardware Test",
            {{"Servo",
              [&]() {
#if CONFIG_STACKCHAN_LOCAL_ITO
                  show_local_mode_unavailable();
#else
                  _destroy_menu = true;
                  _worker       = std::make_unique<ZeroCalibrationWorker>();
#endif
              }},
             {"Microphone",
              [&]() {
#if CONFIG_STACKCHAN_LOCAL_ITO
                  show_local_mode_unavailable();
#else
                  _destroy_menu = true;
                  _worker       = std::make_unique<MicTestWorker>();
#endif
              }},
             {"RGB Strip",
              [&]() {
                  _destroy_menu = true;
                  _worker       = std::make_unique<RgbTestWorker>();
              }}},
        },
        {
            "Account",
            {{"Unbind & Reset",
              [&]() {
#if CONFIG_STACKCHAN_LOCAL_ITO
                  show_local_mode_unavailable();
#else
                  _destroy_menu    = true;
                  _need_warm_reset = true;
                  _worker          = std::make_unique<AccountWorker>();
#endif
              }}},
        },
        {
            "Firmware",
            {
                {fmt::format("Version:  {}", common::FirmwareVersion),
                 [&]() {
                     _magic_count++;
                     if (_magic_count >= 10) {
                         _magic_count  = 0;
                         _destroy_menu = true;
                         _worker       = std::make_unique<FwVersionWorker>();
                     }
                 }},
                {"Check for Updates",
                 [&]() {
#if CONFIG_STACKCHAN_LOCAL_ITO
                     show_local_mode_unavailable();
#else
                     _destroy_menu    = true;
                     _need_warm_reset = true;
                     _worker          = std::make_unique<SystemUpdateWorker>();
#endif
                 }},
                //  {"Factory Reset",
                //   [&]() {
                //       _destroy_menu = true;
                //       _worker       = std::make_unique<FactoryResetWorker>();
                //   }}
            },
        },
    };

    LvglLockGuard lock;

    _menu_page = std::make_unique<view::SelectMenuPage>(_menu_sections);

    view::create_home_indicator([&]() { close(); });
    view::create_status_bar();
}

void AppSetup::onRunning()
{
    LvglLockGuard lock;

    if (_menu_page) {
        _menu_page->update();
    }

    if (_destroy_menu) {
        _menu_page.reset();
        _destroy_menu = false;
    }

    if (_worker) {
        _worker->update();
        if (_worker->isDone()) {
            _worker.reset();
            _menu_page = std::make_unique<view::SelectMenuPage>(_menu_sections);
        }
    }

    GetStackChan().update();

    view::update_home_indicator();
    view::update_status_bar();
}

void AppSetup::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    LvglLockGuard lock;

    _menu_sections.clear();
    _menu_page.reset();
    _worker.reset();

    view::destroy_home_indicator();
    view::destroy_status_bar();

    if (_need_warm_reset) {
        GetHAL().requestWarmReboot(6);
    }
}
