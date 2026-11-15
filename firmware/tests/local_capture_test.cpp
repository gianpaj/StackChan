#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

constexpr int ESP_OK = 0, ESP_CODEC_DEV_OK = 0, ESP_FAIL = -1;
[[maybe_unused]] constexpr int I2S_NUM_0 = 0, I2S_ROLE_MASTER = 0, I2S_GPIO_UNUSED = -1;
[[maybe_unused]] constexpr int I2S_DATA_BIT_WIDTH_16BIT = 16, I2S_SLOT_MODE_MONO = 1, I2S_STD_SLOT_LEFT = 1;
constexpr int ES7210_SEL_MIC1 = 1, AUDIO_CODEC_DMA_FRAME_NUM = 240;
#define ESP_CODEC_DEV_MAKE_CHANNEL_MASK(i) (1 << (i))
#define ESP_LOGE(...) ((void)0)
#define pdMS_TO_TICKS(ms) (ms)
using esp_err_t = int;
struct i2s_std_gpio_config_t { int mclk = 0, bclk = 34, ws = 33, dout = 13, din = -1; };
struct i2s_std_clk_config_t { uint32_t sample_rate_hz; };
struct Slot { int slot_mask = 0; };
struct i2s_std_config_t { i2s_std_clk_config_t clk_cfg; Slot slot_cfg; i2s_std_gpio_config_t gpio_cfg; };
struct i2s_chan_config_t { int dma_desc_num = 0, dma_frame_num = 0; };
struct i2s_chan_info_t { uint32_t total_dma_buf_size = 2880; };
#define I2S_CHANNEL_DEFAULT_CONFIG(...) i2s_chan_config_t{}
#define I2S_STD_CLK_DEFAULT_CONFIG(rate) i2s_std_clk_config_t{rate}
#define I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(...) Slot{}
using i2s_chan_handle_t = int*;
struct audio_codec_ctrl_if_t {};
struct audio_codec_i2c_cfg_t { int port, addr; void* bus_handle; };
struct audio_codec_if_t;
struct es7210_codec_cfg_t { const audio_codec_ctrl_if_t* ctrl_if; int mic_selected; };
struct esp_codec_dev_sample_info_t { int bits_per_sample, channel, channel_mask, sample_rate, mclk_multiple; };
struct audio_codec_if_t {
    int (*set_fs)(const audio_codec_if_t*, esp_codec_dev_sample_info_t*) = nullptr;
    int (*set_mic_gain)(const audio_codec_if_t*, float) = nullptr;
    int (*enable)(const audio_codec_if_t*, bool) = nullptr;
    int (*mute_mic)(const audio_codec_if_t*, bool) = nullptr;
    int (*mute)(const audio_codec_if_t*, bool) = nullptr;
};
struct State {
    std::string failure;
    bool failed = false, tx_running = true, rx_allocated = false, rx_running = false;
    bool mic_power = false, ctrl = false, adc = false, speaker_muted = false;
    int power_off_attempts = 0, write_calls = 0;
    std::vector<std::string> events;
} s;
int tx_cookie, rx_cookie;
bool pass(const char* event) {
    s.events.emplace_back(event);
    if (!s.failed && s.failure == event) { s.failed = true; return false; }
    return true;
}
int i2s_channel_disable(i2s_chan_handle_t h) {
    if (!pass(h == &tx_cookie ? "tx_disable" : "rx_disable")) return ESP_FAIL;
    (h == &tx_cookie ? s.tx_running : s.rx_running) = false; return ESP_OK;
}
int i2s_channel_enable(i2s_chan_handle_t h) {
    if (!pass(h == &tx_cookie ? "tx_enable" : "rx_enable")) return ESP_FAIL;
    if (h == &tx_cookie) assert(!s.mic_power && !s.rx_allocated);
    (h == &tx_cookie ? s.tx_running : s.rx_running) = true; return ESP_OK;
}
int i2s_channel_reconfig_std_gpio(i2s_chan_handle_t, const i2s_std_gpio_config_t* gpio) {
    return pass(gpio->mclk == -1 ? "tx_gpio_detach" : "tx_gpio_restore") ? ESP_OK : ESP_FAIL;
}
int i2s_channel_reconfig_std_clock(i2s_chan_handle_t, const i2s_std_clk_config_t* clock) {
    assert(clock->sample_rate_hz == 24000); return pass("tx_clock") ? ESP_OK : ESP_FAIL;
}
int i2s_new_channel(const i2s_chan_config_t* cfg, void*, i2s_chan_handle_t* rx) {
    assert(cfg->dma_desc_num == 3 && cfg->dma_frame_num == 320 && !s.tx_running);
    if (!pass("rx_new")) return ESP_FAIL;
    s.rx_allocated = true; *rx = &rx_cookie; return ESP_OK;
}
int i2s_channel_init_std_mode(i2s_chan_handle_t, const i2s_std_config_t* cfg) {
    assert(cfg->clk_cfg.sample_rate_hz == 16000 && cfg->slot_cfg.slot_mask == I2S_STD_SLOT_LEFT);
    return pass("rx_init") ? ESP_OK : ESP_FAIL;
}
int i2s_del_channel(i2s_chan_handle_t) {
    if (!pass("rx_delete")) return ESP_FAIL;
    if (s.rx_running) return ESP_FAIL;
    s.rx_allocated = false; return ESP_OK;
}
int i2s_channel_read(i2s_chan_handle_t, void* data, size_t bytes, size_t* received, unsigned timeout) {
    assert(timeout == 40 && bytes <= 640 && s.rx_running && !s.tx_running);
    if (!pass("rx_read")) { *received = 0; return ESP_FAIL; }
    *received = pass("rx_short") ? bytes : bytes - 2;
    std::fill_n(static_cast<int16_t*>(data), bytes / 2, 123); return ESP_OK;
}
int i2s_channel_write(i2s_chan_handle_t, const void*, size_t bytes, size_t* written, unsigned) {
    assert(s.tx_running && !s.rx_allocated && !s.mic_power);
    ++s.write_calls; *written = bytes; return ESP_OK;
}
int i2s_channel_get_info(i2s_chan_handle_t, i2s_chan_info_t*) { return ESP_OK; }
const char* esp_err_to_name(int) { return "mock"; }
int64_t esp_timer_get_time() { return 1000; }
void vTaskDelay(int) {}
namespace hal_bridge {
bool board_set_microphone_power(bool enable) {
    if (!enable) ++s.power_off_attempts;
    if (!pass(enable ? "mic_on" : "mic_off")) return false;
    s.mic_power = enable; return true;
}
}
int adc_fs(const audio_codec_if_t*, esp_codec_dev_sample_info_t* fs) {
    assert(fs->sample_rate == 16000 && fs->bits_per_sample == 16);
    return pass("adc_fs") ? ESP_OK : ESP_FAIL;
}
int adc_gain(const audio_codec_if_t*, float gain) { assert(gain == 30); return pass("adc_gain") ? ESP_OK : ESP_FAIL; }
int adc_enable(const audio_codec_if_t*, bool enable) { return pass(enable ? "adc_enable" : "adc_disable") ? ESP_OK : ESP_FAIL; }
int adc_mute(const audio_codec_if_t*, bool) { return pass("adc_unmute") ? ESP_OK : ESP_FAIL; }
int output_mute(const audio_codec_if_t*, bool mute) { s.speaker_muted = mute; return ESP_OK; }
audio_codec_if_t adc_if{adc_fs, adc_gain, adc_enable, adc_mute}, output_if{nullptr, nullptr, nullptr, nullptr, output_mute};
audio_codec_ctrl_if_t ctrl_if;
const audio_codec_ctrl_if_t* audio_codec_new_i2c_ctrl(audio_codec_i2c_cfg_t*) {
    if (!pass("i2c_new")) return nullptr;
    s.ctrl = true; return &ctrl_if;
}
const audio_codec_if_t* es7210_codec_new(es7210_codec_cfg_t* cfg) {
    assert(cfg->mic_selected == ES7210_SEL_MIC1);
    if (!pass("adc_new")) return nullptr;
    s.adc = true; return &adc_if;
}
void audio_codec_delete_codec_if(const audio_codec_if_t*) { s.adc = false; }
void audio_codec_delete_ctrl_if(const audio_codec_ctrl_if_t*) { s.ctrl = false; }
class CoreS3AudioCodec {
public:
    bool output_enabled_ = true, local_audio_fault_ = false, local_capture_active_ = false;
    bool local_tx_paused_ = false, local_rx_running_ = false, local_muted_ = false;
    int output_sample_rate_ = 24000, output_volume_ = 35, local_adc_addr_ = 0x80, local_din_ = 14;
    int64_t local_pending_until_us_ = 0;
    i2s_chan_handle_t tx_handle_ = &tx_cookie, rx_handle_ = nullptr;
    void* local_i2c_bus_ = nullptr;
    i2s_std_gpio_config_t local_tx_gpio_;
    const audio_codec_if_t* in_codec_if_ = nullptr;
    const audio_codec_if_t* out_codec_if_ = &output_if;
    const audio_codec_ctrl_if_t* in_ctrl_if_ = nullptr;
    bool LocalFlush() { return pass("flush"); }
    bool LocalMute(bool);
    bool LocalWrite(const int16_t*, size_t);
    bool LocalStartCapture();
    bool LocalReadCapture(int16_t*, size_t);
    bool LocalEndCapture();
};
#include "capture_methods.inc"
int main() {
    int16_t pcm[321];
    {
        CoreS3AudioCodec c;
        assert(c.LocalStartCapture());
        assert(s.mic_power && s.rx_running && !s.tx_running && s.speaker_muted);
        assert(!c.LocalWrite(pcm, 160) && !c.LocalMute(false));
        assert(!c.LocalReadCapture(pcm, 321));
        assert(c.LocalReadCapture(pcm, 320) && pcm[0] == 123);
        assert(c.LocalEndCapture());
        assert(!s.mic_power && !s.rx_allocated && !s.adc && !s.ctrl && s.tx_running);
        assert(s.speaker_muted && c.LocalWrite(pcm, 160));
        c.output_volume_ = 0;
        assert(c.LocalMute(false) && s.speaker_muted);
    }
    for (const char* failure : {"tx_disable", "tx_gpio_detach", "rx_new", "rx_init", "rx_enable", "mic_on",
                                "i2c_new", "adc_new", "adc_fs", "adc_gain", "adc_enable", "adc_unmute", "rx_read", "rx_short"}) {
        s = {}; s.failure = failure;
        CoreS3AudioCodec c;
        assert(!c.LocalStartCapture());
        assert(s.failed && s.power_off_attempts > 0 && !s.mic_power && !s.rx_allocated && !s.adc && !s.ctrl);
        assert(s.speaker_muted && (s.tx_running || c.local_audio_fault_));
    }
    for (const char* failure : {"adc_disable", "rx_disable", "rx_delete", "mic_off", "tx_clock", "tx_gpio_restore", "tx_enable"}) {
        s = {};
        CoreS3AudioCodec c;
        assert(c.LocalStartCapture());
        s.failure = failure;
        assert(!c.LocalEndCapture());
        assert(s.failed && c.local_audio_fault_ && s.power_off_attempts > 0 && s.speaker_muted);
        assert(!c.LocalWrite(pcm, 160) && !c.LocalMute(false));
    }
    std::puts("Capture ownership, bounds and 21 injected failure cases pass");
}
