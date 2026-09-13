#pragma once
#include <cmath>
#include <cstdint>

constexpr float C0_FREQUENCY = 16.3516f; // Hz, standard C0

// pitch is a semitone offset from C0, matching Note::pitch in SongData.h
inline float NoteToFrequency(int8_t pitch)
{
    return C0_FREQUENCY * powf(2.0f, (float)pitch / 12.0f);
}