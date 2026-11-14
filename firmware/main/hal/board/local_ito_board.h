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

}  // namespace local_ito_board
