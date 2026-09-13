#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include "song/PianoRollData.h"

// ------------------------------------------------------------------
// Instrument
// ------------------------------------------------------------------

enum class WaveformType : uint8_t
{
    Square   = 0,
    Triangle = 1,
    Sawtooth = 2,
    Sine     = 3,
    Noise    = 4,
    Custom   = 5 // looks up Instrument::customWaveform by name in the runtime wavetable registry (see audio/Synth.h)
};

struct Envelope
{
    float attack  = 0.01f;  // seconds
    float decay   = 0.10f;  // seconds
    float sustain = 0.70f;  // level, 0-1
    float release = 0.20f;  // seconds
};

// Purely presentational -- colors the instrument's row in the track list so
// a busy song is easier to scan at a glance.
enum class InstrumentCategory : uint8_t
{
    Lead = 0,
    Bass,
    Percussion,
    Pad,
    FX
};

// Cheap, very authentic chiptune trick: rapidly retunes a held note through
// a small fixed interval pattern instead of holding one pitch, faking a
// chord or a richer texture from a single note.
enum class ArpPattern : uint8_t
{
    Off = 0,
    Octave,     // root, +1 octave
    MajorChord, // root, major third, fifth
    MinorChord  // root, minor third, fifth
};

enum class FilterType : uint8_t
{
    None = 0,
    LowPass,
    HighPass
};

// What the second envelope drives, instead of amplitude.
enum class Env2Target : uint8_t
{
    None = 0,
    Pitch,        // env2Amount in semitones, peak deviation
    FilterCutoff  // env2Amount as an additional 0-1 cutoff offset, peak
};

struct Instrument
{
    std::string  name = "Instrument";
    WaveformType waveform = WaveformType::Square;
    float        dutyCycle = 0.5f; // 0-1, only meaningful for Square
    std::string  customWaveform;   // name of a registered custom wavetable, only meaningful when waveform == Custom (see audio/Synth.h's registry, and the Wave Editor plugin)
    Envelope     envelope;         // amplitude envelope
    float        volume = 1.0f;    // 0-1
    float        pan    = 0.0f;    // -1 (left) .. 0 (center) .. 1 (right)
    bool         muted  = false;
    bool         solo   = false;   // if any instrument in the song is soloed, only soloed instruments are audible

    InstrumentCategory category = InstrumentCategory::Lead;

    ArpPattern arpPattern = ArpPattern::Off;
    float      arpRate = 12.0f; // notes per second

    bool  vibratoEnabled = false;
    float vibratoRate  = 5.0f; // Hz
    float vibratoDepth = 0.0f; // semitones, peak deviation

    FilterType filterType   = FilterType::None;
    float      filterCutoff = 1.0f; // 0 (closed) .. 1 (fully open / no effect), mapped to an exponential Hz range

    Env2Target env2Target = Env2Target::None; // None = second envelope isn't used at all
    Envelope   env2;                          // second envelope, drives whatever env2Target picks
    float      env2Amount = 0.0f;              // depth -- semitones for Pitch, additional 0-1 cutoff offset for FilterCutoff
};

// ------------------------------------------------------------------
// Note / Pattern
// ------------------------------------------------------------------

static constexpr int8_t  NOTE_EMPTY       = -1;
static constexpr uint8_t INSTRUMENT_NONE  = 0xFF;

struct Note
{
    int8_t  pitch      = NOTE_EMPTY;      // semitone offset from C0. -1 = empty cell
    uint8_t instrument = INSTRUMENT_NONE; // index into Song::instruments
    uint8_t effect     = 0;               // 0 = none. Reserved for step 8 (vibrato/slide/arp...)
    uint8_t effectParam = 0;
};

struct Pattern
{
    uint16_t numRows     = 32;
    uint8_t  numChannels = 4;
    std::vector<Note> cells; // row-major: cells[row * numChannels + channel]

    void Init(uint16_t rows, uint8_t channels)
    {
        numRows = rows;
        numChannels = channels;
        cells.assign((size_t)rows * channels, Note{});
    }

    Note& At(int row, int channel)
    {
        return cells[(size_t)row * numChannels + channel];
    }

    const Note& At(int row, int channel) const
    {
        return cells[(size_t)row * numChannels + channel];
    }
};

// ------------------------------------------------------------------
// Song
// ------------------------------------------------------------------

struct Song
{
    std::string name = "Untitled";
    uint16_t bpm = 120;
    uint8_t  rowsPerBeat = 4; // groove/resolution: rows per quarter note
    float    masterVolume = 1.0f; // 0-1, applied after per-instrument volume/pan, before the limiter

    std::vector<Instrument> instruments;
    std::vector<Pattern>    patterns;
    std::vector<uint16_t>   order; // sequence of pattern indices -- legacy tracker-grid arrangement, unused by this UI

    // Piano-roll style representation (step 5 onward), current as of the
    // arrangement rework (step 15): the song is a list of reusable Sections
    // (each holding one PianoRollTrack per instrument), chained together by
    // `arrangement`, a sequence of indices into `sections` -- classic-tracker
    // pattern/order, just built from piano-roll notes instead of grid cells.
    std::vector<Section> sections;
    std::vector<int32_t> arrangement;

    // Pre-arrangement (v1-v3) files stored one implicit section directly
    // here instead of `sections`/`arrangement`. LoadSong() only populates
    // this for such files; SaveSong() never writes it. Callers loading an
    // old file should synthesize a single Section from this and an
    // arrangement of just that one section.
    std::vector<PianoRollTrack> tracks;
};