#pragma once

#include <cstddef>
#include <cstdint>

namespace local_ito_board {

bool prepare_avatar();
bool initialize_audio(int sample_rate);
bool write_mono(const int16_t* pcm, size_t samples);
bool drain();
bool flush();
bool mute(bool muted);
bool set_volume(int volume);
void set_speaking(bool speaking);

struct CaptureStats {
    size_t samples = 0;
    size_t clipped_samples = 0;
    uint16_t peak = 0;
    uint32_t duration_ms = 0;
    bool active = false;
};

// Caller owns the capture worker and checks cancellation between reads.
// Capture is 16 kHz PCM16 mono, at most 10 seconds and 320 samples per read.
bool start_capture();
bool read_capture_mono(int16_t* pcm, size_t samples);
bool end_capture();
CaptureStats capture_stats();

}  // namespace local_ito_board
