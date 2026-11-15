#include "cores3_audio_codec.h"

#include <esp_log.h>
#include <driver/i2c_master.h>
#include <driver/i2s_tdm.h>

#define TAG "CoreS3AudioCodec"

CoreS3AudioCodec::CoreS3AudioCodec(void* i2c_master_handle, int input_sample_rate, int output_sample_rate,
    gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws, gpio_num_t dout, gpio_num_t din,
    uint8_t aw88298_addr, uint8_t es7210_addr, bool input_reference) {
#if CONFIG_STACKCHAN_LOCAL_ITO
    duplex_ = false;
    local_i2c_bus_ = i2c_master_handle;
    local_adc_addr_ = es7210_addr;
    local_din_ = din;
#else
    duplex_ = true; // 是否双工
#endif
    input_reference_ = input_reference; // 是否使用参考输入，实现回声消除
    input_channels_ = input_reference_ ? 2 : 1; // 输入通道数
    input_sample_rate_ = input_sample_rate;
    output_sample_rate_ = output_sample_rate;
    input_gain_ = 60;

    CreateDuplexChannels(mclk, bclk, ws, dout, din);

    // Do initialize of related interface: data_if, ctrl_if and gpio_if
    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = I2S_NUM_0,
        .rx_handle = rx_handle_,
        .tx_handle = tx_handle_,
    };
    data_if_ = audio_codec_new_i2s_data(&i2s_cfg);
    assert(data_if_ != NULL);

    // Audio Output(Speaker)
    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = (i2c_port_t)1,
        .addr = aw88298_addr,
        .bus_handle = i2c_master_handle,
    };
    out_ctrl_if_ = audio_codec_new_i2c_ctrl(&i2c_cfg);
    assert(out_ctrl_if_ != NULL);

    gpio_if_ = audio_codec_new_gpio();
    assert(gpio_if_ != NULL);

    aw88298_codec_cfg_t aw88298_cfg = {};
    aw88298_cfg.ctrl_if = out_ctrl_if_;
    aw88298_cfg.gpio_if = gpio_if_;
    aw88298_cfg.reset_pin = GPIO_NUM_NC;
    aw88298_cfg.hw_gain.pa_voltage = 5.0;
    aw88298_cfg.hw_gain.codec_dac_voltage = 3.3;
    aw88298_cfg.hw_gain.pa_gain = 1;
    out_codec_if_ = aw88298_codec_new(&aw88298_cfg);
    assert(out_codec_if_ != NULL);

    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = out_codec_if_,
        .data_if = data_if_,
    };
    output_dev_ = esp_codec_dev_new(&dev_cfg);
    assert(output_dev_ != NULL);

#if !CONFIG_STACKCHAN_LOCAL_ITO
    // Audio Input(Microphone)
    i2c_cfg.addr = es7210_addr;
    in_ctrl_if_ = audio_codec_new_i2c_ctrl(&i2c_cfg);
    assert(in_ctrl_if_ != NULL);

    es7210_codec_cfg_t es7210_cfg = {};
    es7210_cfg.ctrl_if = in_ctrl_if_;
    es7210_cfg.mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2 | ES7210_SEL_MIC3;
    in_codec_if_ = es7210_codec_new(&es7210_cfg);
    assert(in_codec_if_ != NULL);

    dev_cfg.dev_type = ESP_CODEC_DEV_TYPE_IN;
    dev_cfg.codec_if = in_codec_if_;
    input_dev_ = esp_codec_dev_new(&dev_cfg);
    assert(input_dev_ != NULL);

#endif
    ESP_LOGI(TAG, "CoreS3AudioCodec initialized");
}

CoreS3AudioCodec::~CoreS3AudioCodec() {
    ESP_ERROR_CHECK(esp_codec_dev_close(output_dev_));
    esp_codec_dev_delete(output_dev_);
    if (input_dev_) {
        ESP_ERROR_CHECK(esp_codec_dev_close(input_dev_));
        esp_codec_dev_delete(input_dev_);
    }

    if (in_codec_if_) audio_codec_delete_codec_if(in_codec_if_);
    if (in_ctrl_if_) audio_codec_delete_ctrl_if(in_ctrl_if_);
    audio_codec_delete_codec_if(out_codec_if_);
    audio_codec_delete_ctrl_if(out_ctrl_if_);
    audio_codec_delete_gpio_if(gpio_if_);
    audio_codec_delete_data_if(data_if_);
}

void CoreS3AudioCodec::CreateDuplexChannels(gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws, gpio_num_t dout, gpio_num_t din) {
    assert(input_sample_rate_ == output_sample_rate_);

    ESP_LOGI(TAG, "Audio IOs: mclk: %d, bclk: %d, ws: %d, dout: %d, din: %d", mclk, bclk, ws, dout, din);

    i2s_chan_config_t chan_cfg = {
        .id = I2S_NUM_0,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = AUDIO_CODEC_DMA_DESC_NUM,
        .dma_frame_num = AUDIO_CODEC_DMA_FRAME_NUM,
        .auto_clear_after_cb = true,
        .auto_clear_before_cb = false,
        .intr_priority = 0,
    };
#if CONFIG_STACKCHAN_LOCAL_ITO
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle_, nullptr));
#else
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle_, &rx_handle_));
#endif

    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = (uint32_t)output_sample_rate_,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .ext_clk_freq_hz = 0,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_STEREO,
            .slot_mask = I2S_STD_SLOT_BOTH,
            .ws_width = I2S_DATA_BIT_WIDTH_16BIT,
            .ws_pol = false,
            .bit_shift = true,
            .left_align = true,
            .big_endian = false,
            .bit_order_lsb = false
        },
        .gpio_cfg = {
            .mclk = mclk,
            .bclk = bclk,
            .ws = ws,
            .dout = dout,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false
            }
        }
    };

#if !CONFIG_STACKCHAN_LOCAL_ITO
    i2s_tdm_config_t tdm_cfg = {
        .clk_cfg = {
            .sample_rate_hz = (uint32_t)input_sample_rate_,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .ext_clk_freq_hz = 0,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
            .bclk_div = 8,
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_STEREO,
            .slot_mask = i2s_tdm_slot_mask_t(I2S_TDM_SLOT0 | I2S_TDM_SLOT1 | I2S_TDM_SLOT2 | I2S_TDM_SLOT3),
            .ws_width = I2S_TDM_AUTO_WS_WIDTH,
            .ws_pol = false,
            .bit_shift = true,
            .left_align = false,
            .big_endian = false,
            .bit_order_lsb = false,
            .skip_mask = false,
            .total_slot = I2S_TDM_AUTO_SLOT_NUM
        },
        .gpio_cfg = {
            .mclk = mclk,
            .bclk = bclk,
            .ws = ws,
            .dout = I2S_GPIO_UNUSED,
            .din = din,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false
            }
        }
    };

#endif
#if CONFIG_STACKCHAN_LOCAL_ITO
    local_tx_gpio_ = std_cfg.gpio_cfg;
#endif
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle_, &std_cfg));
#if !CONFIG_STACKCHAN_LOCAL_ITO
    ESP_ERROR_CHECK(i2s_channel_init_tdm_mode(rx_handle_, &tdm_cfg));
#endif
    ESP_ERROR_CHECK(i2s_channel_enable(tx_handle_));
#if !CONFIG_STACKCHAN_LOCAL_ITO
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle_));
#endif
    ESP_LOGI(TAG, "Duplex channels created");
}

void CoreS3AudioCodec::SetOutputVolume(int volume) {
    ESP_ERROR_CHECK(esp_codec_dev_set_out_vol(output_dev_, volume));
    AudioCodec::SetOutputVolume(volume);
}

void CoreS3AudioCodec::EnableInput(bool enable) {
#if CONFIG_STACKCHAN_LOCAL_ITO
    return;
#endif
    if (enable == input_enabled_) {
        return;
    }
    if (enable) {
        esp_codec_dev_sample_info_t fs = {
            .bits_per_sample = 16,
            .channel = 2,
            .channel_mask = ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0),
            .sample_rate = (uint32_t)output_sample_rate_,
            .mclk_multiple = 0,
        };
        if (input_reference_) {
            fs.channel_mask |= ESP_CODEC_DEV_MAKE_CHANNEL_MASK(1);
        }
        ESP_ERROR_CHECK(esp_codec_dev_open(input_dev_, &fs));
        ESP_ERROR_CHECK(esp_codec_dev_set_in_channel_gain(input_dev_, ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0), input_gain_));
    } else {
        ESP_ERROR_CHECK(esp_codec_dev_close(input_dev_));
    }
    AudioCodec::EnableInput(enable);
}

void CoreS3AudioCodec::EnableOutput(bool enable) {
#if CONFIG_STACKCHAN_LOCAL_ITO
    return;
#endif
    if (enable == output_enabled_) {
        return;
    }
    if (enable) {
        // Play 16bit 1 channel
        esp_codec_dev_sample_info_t fs = {
            .bits_per_sample = 16,
            .channel = 1,
            .channel_mask = 0,
            .sample_rate = (uint32_t)output_sample_rate_,
            .mclk_multiple = 0,
        };
        ESP_ERROR_CHECK(esp_codec_dev_open(output_dev_, &fs));
        ESP_ERROR_CHECK(esp_codec_dev_set_out_vol(output_dev_, output_volume_));
    } else {
        ESP_ERROR_CHECK(esp_codec_dev_close(output_dev_));
    }
    AudioCodec::EnableOutput(enable);
}

int CoreS3AudioCodec::Read(int16_t* dest, int samples) {
#if CONFIG_STACKCHAN_LOCAL_ITO
    return 0;
#endif
    if (input_enabled_) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_codec_dev_read(input_dev_, (void*)dest, samples * sizeof(int16_t)));
    }
    return samples;
}

int CoreS3AudioCodec::Write(const int16_t* data, int samples) {
#if CONFIG_STACKCHAN_LOCAL_ITO
    return 0;
#endif
    if (output_enabled_) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_codec_dev_write(output_dev_, (void*)data, samples * sizeof(int16_t)));
    }
    return samples;
}

#if CONFIG_STACKCHAN_LOCAL_ITO
#include <esp_timer.h>
#include <freertos/task.h>
#include <algorithm>
#include "hal_bridge.h"

bool CoreS3AudioCodec::LocalOpen(int sample_rate)
{
    if (sample_rate != output_sample_rate_ || local_audio_fault_ || local_capture_active_) {
        return false;
    }
    if (output_enabled_) {
        return true;
    }
    Start();
    esp_codec_dev_sample_info_t fs = {};
    fs.bits_per_sample = 16;
    fs.channel = 1;
    fs.sample_rate = sample_rate;
    if (esp_codec_dev_open(output_dev_, &fs) != ESP_CODEC_DEV_OK) {
        return false;
    }
    AudioCodec::EnableOutput(true);
    if (!LocalSetVolume(output_volume_)) {
        return false;
    }
    return LocalFlush() && LocalLogOutputState("open");
}

bool CoreS3AudioCodec::LocalWrite(const int16_t* data, size_t samples)
{
    if (!output_enabled_ || local_audio_fault_ || local_capture_active_ ||
        !data || samples == 0 || samples > AUDIO_CODEC_DMA_FRAME_NUM) {
        return false;
    }
    size_t bytes_written = 0;
    const size_t bytes = samples * sizeof(int16_t);
    const esp_err_t result = i2s_channel_write(tx_handle_, data, bytes, &bytes_written, 40);
    if (result != ESP_OK || bytes_written != bytes) {
        ESP_LOGE(TAG, "Local PCM write failed: %s, %u/%u bytes", esp_err_to_name(result),
                 static_cast<unsigned>(bytes_written), static_cast<unsigned>(bytes));
        return false;
    }
    i2s_chan_info_t info = {};
    if (i2s_channel_get_info(tx_handle_, &info) != ESP_OK) {
        return false;
    }
    // One full ring plus one frame covers every sample accepted by the driver.
    local_pending_until_us_ = esp_timer_get_time() +
        (info.total_dma_buf_size * 1000000LL / (output_sample_rate_ * sizeof(int16_t))) +
        (AUDIO_CODEC_DMA_FRAME_NUM * 1000000LL / output_sample_rate_);
    return true;
}

bool CoreS3AudioCodec::LocalMute(bool muted)
{
    if (!output_enabled_ || (!muted && (local_audio_fault_ || local_capture_active_)) || !out_codec_if_->mute ||
        out_codec_if_->mute(out_codec_if_, muted || output_volume_ == 0) != ESP_CODEC_DEV_OK) {
        return false;
    }
    local_muted_ = muted;
    if (!muted && !LocalLogOutputState("unmute")) {
        out_codec_if_->mute(out_codec_if_, true);
        local_muted_ = true;
        local_audio_fault_ = true;
        return false;
    }
    return true;
}

bool CoreS3AudioCodec::LocalFlush()
{
    if (!output_enabled_ || local_audio_fault_ || local_capture_active_) {
        return false;
    }
    const bool restore_mute = local_muted_;
    if (!LocalMute(true)) {
        return false;
    }
    if (i2s_channel_disable(tx_handle_) != ESP_OK) {
        return false;
    }
    i2s_chan_info_t info = {};
    if (i2s_channel_get_info(tx_handle_, &info) != ESP_OK || info.total_dma_buf_size == 0) {
        return false;
    }
    static const int16_t silence[AUDIO_CODEC_DMA_FRAME_NUM] = {};
    size_t remaining = info.total_dma_buf_size;
    while (remaining > 0) {
        const size_t chunk = std::min(remaining, sizeof(silence));
        size_t loaded = 0;
        if (i2s_channel_preload_data(tx_handle_, silence, chunk, &loaded) != ESP_OK || loaded != chunk) {
            return false;
        }
        remaining -= loaded;
    }
    if (i2s_channel_enable(tx_handle_) != ESP_OK) {
        return false;
    }
    local_pending_until_us_ = 0;
    return LocalMute(restore_mute);
}

bool CoreS3AudioCodec::LocalPlaying() const
{
    return output_enabled_ && !local_capture_active_ && esp_timer_get_time() < local_pending_until_us_;
}

bool CoreS3AudioCodec::LocalSetVolume(int volume)
{
    if (volume < 0 || volume > 100 || !out_codec_if_->set_vol || !out_codec_if_->mute) {
        return false;
    }
    if (volume == 0 && out_codec_if_->mute(out_codec_if_, true) != ESP_CODEC_DEV_OK) {
        return false;
    }
    // Match esp_codec_dev's default percent-to-decibel curve.
    const float db = volume == 0 ? -96.0f : -50.0f + volume * 0.5f;
    if (out_codec_if_->set_vol(out_codec_if_, db) != ESP_CODEC_DEV_OK) {
        return false;
    }
    output_volume_ = volume;
    return LocalMute(local_muted_);
}

bool CoreS3AudioCodec::LocalStartCapture()
{
    if (!output_enabled_ || local_audio_fault_ || local_capture_active_) {
        return false;
    }
    if (!LocalMute(true) || !LocalFlush()) {
        return false;
    }
    local_capture_active_ = true;
    auto fail = [this]() {
        LocalEndCapture();
        return false;
    };
    if (!LocalLogOutputState("capture-start")) {
        local_audio_fault_ = true;
        return fail();
    }
    local_amp_paused_ = true;
    if (out_codec_if_->enable(out_codec_if_, false) != ESP_CODEC_DEV_OK) {
        local_audio_fault_ = true;
        return fail();
    }
    if (i2s_channel_disable(tx_handle_) != ESP_OK) {
        local_audio_fault_ = true;
        return fail();
    }
    local_tx_paused_ = true;
    auto unused_gpio = local_tx_gpio_;
    unused_gpio.mclk = unused_gpio.bclk = unused_gpio.ws = I2S_GPIO_UNUSED;
    unused_gpio.dout = unused_gpio.din = I2S_GPIO_UNUSED;
    if (i2s_channel_reconfig_std_gpio(tx_handle_, &unused_gpio) != ESP_OK) {
        return fail();
    }

    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channel.dma_desc_num = 3;
    channel.dma_frame_num = 320;
    if (i2s_new_channel(&channel, nullptr, &rx_handle_) != ESP_OK) {
        return fail();
    }
    i2s_std_config_t config = {};
    config.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000);
    config.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
    config.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
    config.gpio_cfg = local_tx_gpio_;
    config.gpio_cfg.dout = I2S_GPIO_UNUSED;
    config.gpio_cfg.din = local_din_;
    if (i2s_channel_init_std_mode(rx_handle_, &config) != ESP_OK ||
        i2s_channel_enable(rx_handle_) != ESP_OK) {
        return fail();
    }
    local_rx_running_ = true;
    if (!hal_bridge::board_set_microphone_power(true)) {
        return fail();
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    audio_codec_i2c_cfg_t i2c_config = {};
    i2c_config.port = 1;
    i2c_config.addr = local_adc_addr_;
    i2c_config.bus_handle = local_i2c_bus_;
    in_ctrl_if_ = audio_codec_new_i2c_ctrl(&i2c_config);
    if (!in_ctrl_if_) {
        return fail();
    }
    es7210_codec_cfg_t adc_config = {};
    adc_config.ctrl_if = in_ctrl_if_;
    adc_config.mic_selected = ES7210_SEL_MIC1;
    in_codec_if_ = es7210_codec_new(&adc_config);
    if (!in_codec_if_) {
        return fail();
    }
    esp_codec_dev_sample_info_t format = {};
    format.bits_per_sample = 16;
    format.channel = 2;
    format.channel_mask = ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0);
    format.sample_rate = 16000;
    format.mclk_multiple = 256;
    if (in_codec_if_->set_fs(in_codec_if_, &format) != ESP_CODEC_DEV_OK ||
        in_codec_if_->enable(in_codec_if_, true) != ESP_CODEC_DEV_OK ||
        in_codec_if_->set_mic_gain(in_codec_if_, 30) != ESP_CODEC_DEV_OK ||
        in_codec_if_->mute_mic(in_codec_if_, false) != ESP_CODEC_DEV_OK) {
        return fail();
    }
    // Discard the RX ring filled during ADC startup before delivering audio.
    int16_t warmup[320];
    for (int i = 0; i < 3; ++i) {
        if (!LocalReadCapture(warmup, 320)) {
            return fail();
        }
    }
    return true;
}

bool CoreS3AudioCodec::LocalReadCapture(int16_t* data, size_t samples)
{
    if (!local_capture_active_ || !local_rx_running_ || local_audio_fault_ ||
        !data || samples == 0 || samples > 320) {
        return false;
    }
    const size_t bytes = samples * sizeof(int16_t);
    size_t received = 0;
    const auto error = i2s_channel_read(rx_handle_, data, bytes, &received, 40);
    if (error != ESP_OK || received != bytes) {
        ESP_LOGE(TAG, "Microphone read failed: %s, %u/%u bytes", esp_err_to_name(error),
                 static_cast<unsigned>(received), static_cast<unsigned>(bytes));
        return false;
    }
    return true;
}

bool CoreS3AudioCodec::LocalEndCapture()
{
    bool ok = true;
    if (in_codec_if_) {
        if (in_codec_if_->enable(in_codec_if_, false) != ESP_CODEC_DEV_OK) {
            ok = false;
        }
        audio_codec_delete_codec_if(in_codec_if_);
        in_codec_if_ = nullptr;
    }
    if (in_ctrl_if_) {
        audio_codec_delete_ctrl_if(in_ctrl_if_);
        in_ctrl_if_ = nullptr;
    }
    if (rx_handle_) {
        if (local_rx_running_ && i2s_channel_disable(rx_handle_) != ESP_OK) {
            ok = false;
        }
        local_rx_running_ = false;
        if (i2s_del_channel(rx_handle_) != ESP_OK) {
            ok = false;
        } else {
            rx_handle_ = nullptr;
        }
    }
    // Cut microphone power even when ADC or DMA cleanup fails.
    if (!hal_bridge::board_set_microphone_power(false)) {
        ok = false;
    }
    if (ok && !local_audio_fault_ && local_tx_paused_) {
        i2s_std_clk_config_t clock = I2S_STD_CLK_DEFAULT_CONFIG(static_cast<uint32_t>(output_sample_rate_));
        if (i2s_channel_reconfig_std_clock(tx_handle_, &clock) != ESP_OK ||
            i2s_channel_reconfig_std_gpio(tx_handle_, &local_tx_gpio_) != ESP_OK ||
            i2s_channel_enable(tx_handle_) != ESP_OK) {
            ok = false;
        } else {
            local_tx_paused_ = false;
        }
    }
    if (ok && !local_audio_fault_ && local_amp_paused_) {
        esp_codec_dev_sample_info_t format = {};
        format.bits_per_sample = 16;
        format.channel = 1;
        format.sample_rate = output_sample_rate_;
        if (out_codec_if_->set_fs(out_codec_if_, &format) != ESP_CODEC_DEV_OK ||
            out_codec_if_->enable(out_codec_if_, true) != ESP_CODEC_DEV_OK ||
            !LocalSetVolume(output_volume_) || !LocalOutputReady() ||
            !LocalLogOutputState("capture-end")) {
            ok = false;
        } else {
            local_amp_paused_ = false;
        }
    }
    local_capture_active_ = false;
    local_pending_until_us_ = 0;
    if (!ok) {
        local_audio_fault_ = true;
        ESP_LOGE(TAG, "Microphone cleanup failed; speaker remains blocked");
    }
    if (local_audio_fault_ && local_amp_paused_) {
        out_codec_if_->mute(out_codec_if_, true);
        out_codec_if_->enable(out_codec_if_, false);
    }
    return ok && !local_audio_fault_;
}

bool CoreS3AudioCodec::LocalLogOutputState(const char* stage)
{
    const int registers[] = {0x01, 0x04, 0x05, 0x06, 0x0c};
    int values[5] = {};
    for (size_t i = 0; i < 5; ++i) {
        if (!out_codec_if_->get_reg ||
            out_codec_if_->get_reg(out_codec_if_, registers[i], &values[i]) != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "AW88298 %s readback failed at %02x", stage, registers[i]);
            return false;
        }
    }
    ESP_LOGI(TAG, "AW88298 %s status=%04x control=%04x mute=%04x format=%04x volume=%04x",
             stage, values[0], values[1], values[2], values[3], values[4]);
    return true;
}

bool CoreS3AudioCodec::LocalOutputReady()
{
    // SYSST requires PLL lock, stable clocks and amplifier switching before unmute.
    for (int attempt = 0; attempt < 20; ++attempt) {
        int status = 0;
        if (!out_codec_if_->get_reg ||
            out_codec_if_->get_reg(out_codec_if_, 0x01, &status) != ESP_CODEC_DEV_OK) {
            return false;
        }
        if ((status & 0x0131) == 0x0111) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    LocalLogOutputState("restart-timeout");
    return false;
}
#endif
