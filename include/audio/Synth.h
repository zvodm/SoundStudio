#pragma once
#include "song/SongData.h"
#include <cstdint>
#include <vector>
#include <mutex>
#include <memory>
#include <string>

enum class EnvelopeStage : uint8_t
{
    Idle,
    Attack,
    Decay,
    Sustain,
    Release
};

// One ADSR envelope's running state. A Voice carries two of these: the
// amplitude envelope (always active) and the modulation envelope (only
// meaningful when the instrument's env2Target isn't None) -- both driven by
// the same AdvanceEnvelopeState() logic, just against different Envelope
// parameters and different destinations.
struct EnvelopeState
{
    EnvelopeStage stage = EnvelopeStage::Idle;
    float stageTime = 0.0f;         // seconds elapsed in current stage
    float envLevel = 0.0f;          // current envelope output, 0..1
    float releaseStartLevel = 0.0f; // envLevel captured at the moment release began
};

struct Voice
{
    bool active = false;
    const Instrument* instrument = nullptr;
    float baseFrequency = 440.0f; // the root note's frequency, before arp/vibrato/pitch-env offsets
    float frequency = 440.0f;     // current oscillator frequency, after those offsets are applied each sample
    float phase = 0.0f;           // 0..1
    uint16_t noiseLfsr = 0x7FFF;  // must never be 0
    float velocity = 1.0f;        // 0..1, scales this note's amplitude

    EnvelopeState ampEnv; // drives amplitude, as before
    EnvelopeState modEnv; // drives whatever instrument->env2Target picks (pitch or filter cutoff), if anything

    // Arpeggiator: cycles the sounding pitch through a small interval
    // pattern while the voice is held.
    int arpStep = 0;
    float arpTimer = 0.0f;

    // Vibrato LFO phase, 0..1, free-running while the voice is held.
    float lfoPhase = 0.0f;

    // One-pole filter state (see Synth::ApplyFilter). For LowPass this is
    // the filter's own running output; HighPass derives its output from the
    // same lowpass state.
    float filterState = 0.0f;

    // Only populated when instrument->waveform == WaveformType::Custom,
    // captured once in NoteOn (see FindCustomWaveform below) so a voice
    // keeps playing its own wave even if a plugin re-registers/replaces
    // that name mid-note.
    std::shared_ptr<const std::vector<float>> customWave;
};

// ------------------------------------------------------------------
// Custom waveform registry -- populated at runtime by plugins (see
// plugin/PluginManager and the bundled Wave Editor plugin), looked up by
// name. One full cycle of samples, any length; the oscillator resamples it
// with linear interpolation the same way a classic wavetable synth would.
// Thread-safe: RegisterCustomWaveform never mutates a table in place, only
// swaps in a brand new one, so a Voice holding an older shared_ptr from
// before the swap keeps reading valid, unchanging data.
// ------------------------------------------------------------------

void RegisterCustomWaveform(const std::string& name, std::vector<float> samples);
std::shared_ptr<const std::vector<float>> FindCustomWaveform(const std::string& name);

class Synth
{
public:
    void Init(int sampleRate, int voiceCount = 4);

    // channel = which voice slot to use (0..voiceCount-1). velocity01 is
    // 0..1 (from PlacedNote::velocity / 127) and scales this note's
    // amplitude; defaults to full velocity for callers that don't have a
    // real one (e.g. the piano roll's live key-preview audition).
    void NoteOn(int channel, float frequency, const Instrument* instrument, float velocity01 = 1.0f);
    void NoteOff(int channel);

    // Called from the audio thread via the raylib AudioStream callback.
    // Writes frameCount stereo frames (frameCount * 2 floats, interleaved L/R).
    void GenerateSamples(float* buffer, unsigned int frameCount);

    // Immediately silences every voice with no release tail. Call this
    // right before the Instrument list a Voice might be pointing into
    // gets reallocated or replaced (undo/redo, add/remove track, load) --
    // otherwise a Voice can end up holding a dangling Instrument*.
    void Reset();

    // True if any voice still has sound to produce (attack through release
    // tail). Used by the offline WAV exporter to know when it can stop
    // rendering trailing silence after the last note in the song.
    bool AnyVoiceActive() const;

    // Master mix controls -- safe to call from the UI thread every frame,
    // both are read under the same mutex GenerateSamples locks.
    void SetMasterVolume(float volume);
    void SetAnySolo(bool anySolo); // true if any instrument in the song currently has solo enabled

    // Live oscilloscope tap (item 30): while set, GenerateSamples sums the
    // post-filter/post-envelope (pre-pan) sample of every currently
    // sounding voice whose Voice::instrument pointer equals this one, into
    // a small ring buffer -- so the UI can show what a specific instrument
    // is actually outputting right now, whether that's from held preview
    // notes or real playback. Pass nullptr to stop tapping (saves nothing
    // extra, just avoids the pointer comparison each sample). Both methods
    // lock the same mutex GenerateSamples does, so they're safe to call
    // from the UI thread every frame.
    void SetScopeInstrument(const Instrument* instrument);
    void GetScopeSamples(std::vector<float>& out);

    // Master oscilloscope tap -- unlike SetScopeInstrument above, this is
    // always capturing: every sample of the final post-limiter stereo mix,
    // regardless of which instrument(s) produced it. Used by the
    // Oscilloscope plugin (and available to any other UI) to visualize the
    // whole song's actual output. Locks the same mutex GenerateSamples
    // does, so it's safe to call from the UI thread every frame.
    void GetMasterScopeSamples(std::vector<float>& outLeft, std::vector<float>& outRight);

private:
    float GenerateOscillatorSample(Voice& voice, float frequency);
    void AdvanceEnvelopeState(EnvelopeState& es, const Envelope& env, float dt);
    void AdvanceArp(Voice& voice, float dt);          // updates voice.frequency's arp offset
    float ApplyFilter(Voice& voice, float sample, float cutoff01);

    std::vector<Voice> voices_;
    int sampleRate_ = 44100;
    std::mutex mutex_;

    float masterVolume_ = 1.0f;
    bool  anySolo_ = false;

    // Simple peak limiter on the final stereo mix -- replaces a fixed
    // 1/sqrt(voiceCount) headroom division with something that only pulls
    // gain down when the signal is actually about to clip.
    float limiterGain_ = 1.0f;          // current smoothed gain reduction, 0..1
    float limiterAttackCoeff_ = 1.0f;   // how fast gain drops when the mix gets loud (set in Init, depends on sampleRate)
    float limiterReleaseCoeff_ = 1.0f;  // how fast gain recovers afterwards

    // Oscilloscope tap -- see SetScopeInstrument/GetScopeSamples above.
    static constexpr int SCOPE_BUFFER_SIZE = 1024;
    const Instrument* scopeInstrument_ = nullptr;
    std::vector<float> scopeBuffer_;
    int scopeWritePos_ = 0;

    // Master oscilloscope tap -- see GetMasterScopeSamples above.
    std::vector<float> masterScopeLeft_;
    std::vector<float> masterScopeRight_;
    int masterScopeWritePos_ = 0;
};

// Only one Synth can drive audio output at a time -- this is the instance
// the raylib AudioStream callback below pulls samples from.
void SetActiveSynth(Synth* synth);
Synth* GetActiveSynth(); // nullptr if none is active yet -- used by the plugin API to reach the live synth's master scope tap
void SynthAudioCallback(void* bufferData, unsigned int frames);
