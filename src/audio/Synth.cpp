#include "audio/Synth.h"
#include "audio/MusicUtils.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>

static constexpr float PI2 = 6.28318530718f;

static Synth* g_activeSynth = nullptr;

void SetActiveSynth(Synth* synth)
{
    g_activeSynth = synth;
}

Synth* GetActiveSynth()
{
    return g_activeSynth;
}

// ------------------------------------------------------------------
// Custom waveform registry -- see Synth.h. Never mutated in place: a
// register call builds a brand new shared_ptr and swaps it into the map,
// so any Voice already holding an older one keeps reading valid data.
// ------------------------------------------------------------------

namespace
{
    std::mutex g_customWaveMutex;
    std::unordered_map<std::string, std::shared_ptr<const std::vector<float>>> g_customWaves;
}

void RegisterCustomWaveform(const std::string& name, std::vector<float> samples)
{
    auto table = std::make_shared<const std::vector<float>>(std::move(samples));
    std::lock_guard<std::mutex> lock(g_customWaveMutex);
    g_customWaves[name] = std::move(table);
}

std::shared_ptr<const std::vector<float>> FindCustomWaveform(const std::string& name)
{
    std::lock_guard<std::mutex> lock(g_customWaveMutex);
    auto it = g_customWaves.find(name);
    if (it == g_customWaves.end()) return nullptr;
    return it->second;
}

void SynthAudioCallback(void* bufferData, unsigned int frames)
{
    float* out = (float*)bufferData;
    if (g_activeSynth)
    {
        g_activeSynth->GenerateSamples(out, frames);
    }
    else
    {
        memset(out, 0, sizeof(float) * frames);
    }
}

void Synth::Init(int sampleRate, int voiceCount)
{
    sampleRate_ = sampleRate;
    voices_.assign(voiceCount, Voice{});

    // Limiter time constants -> per-sample smoothing coefficients. Fast
    // attack (~5ms) so transients get caught before they clip; slower
    // release (~100ms) so the gain reduction doesn't pump audibly.
    constexpr float attackSeconds = 0.005f;
    constexpr float releaseSeconds = 0.100f;
    limiterAttackCoeff_ = 1.0f - expf(-1.0f / ((float)sampleRate_ * attackSeconds));
    limiterReleaseCoeff_ = 1.0f - expf(-1.0f / ((float)sampleRate_ * releaseSeconds));

    scopeBuffer_.assign(SCOPE_BUFFER_SIZE, 0.0f);
    scopeWritePos_ = 0;

    masterScopeLeft_.assign(SCOPE_BUFFER_SIZE, 0.0f);
    masterScopeRight_.assign(SCOPE_BUFFER_SIZE, 0.0f);
    masterScopeWritePos_ = 0;
}

void Synth::GetMasterScopeSamples(std::vector<float>& outLeft, std::vector<float>& outRight)
{
    std::lock_guard<std::mutex> lock(mutex_);
    outLeft.resize(masterScopeLeft_.size());
    outRight.resize(masterScopeRight_.size());
    for (size_t k = 0; k < masterScopeLeft_.size(); ++k)
    {
        size_t idx = (masterScopeWritePos_ + k) % masterScopeLeft_.size();
        outLeft[k] = masterScopeLeft_[idx];
        outRight[k] = masterScopeRight_[idx];
    }
}

void Synth::SetScopeInstrument(const Instrument* instrument)
{
    std::lock_guard<std::mutex> lock(mutex_);
    scopeInstrument_ = instrument;
}

void Synth::GetScopeSamples(std::vector<float>& out)
{
    std::lock_guard<std::mutex> lock(mutex_);
    out.resize(scopeBuffer_.size());
    // copy out oldest-to-newest starting from the current write position,
    // so the caller can plot it left-to-right as a normal waveform
    for (size_t k = 0; k < scopeBuffer_.size(); ++k)
        out[k] = scopeBuffer_[(scopeWritePos_ + k) % scopeBuffer_.size()];
}

void Synth::SetMasterVolume(float volume)
{
    std::lock_guard<std::mutex> lock(mutex_);
    masterVolume_ = std::clamp(volume, 0.0f, 1.0f);
}

void Synth::SetAnySolo(bool anySolo)
{
    std::lock_guard<std::mutex> lock(mutex_);
    anySolo_ = anySolo;
}

void Synth::Reset()
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& v : voices_)
    {
        v.active = false;
        v.instrument = nullptr;
        v.ampEnv = EnvelopeState{};
        v.modEnv = EnvelopeState{};
    }
}

bool Synth::AnyVoiceActive() const
{
    // Not locked: only ever called on renderSynth instances used
    // single-threaded by the offline exporter, never on a Synth wired up to
    // the live audio callback.
    for (const auto& v : voices_) if (v.active) return true;
    return false;
}

void Synth::NoteOn(int channel, float frequency, const Instrument* instrument, float velocity01)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (channel < 0 || channel >= (int)voices_.size()) return;

    Voice& v = voices_[channel];
    v.active = true;
    v.instrument = instrument;
    v.baseFrequency = frequency;
    v.frequency = frequency; // AdvanceArp refines this every sample; start at the root in case arp is off
    v.velocity = std::clamp(velocity01, 0.0f, 1.0f);
    v.phase = 0.0f;
    v.noiseLfsr = 0x7FFF;
    v.arpStep = 0;
    v.arpTimer = 0.0f;
    v.lfoPhase = 0.0f;
    // filterState is intentionally NOT reset -- a retriggered voice keeps
    // its filter's running state, same "avoid a click" reasoning as envLevel below

    // Captured once here rather than looked up every sample: if a plugin
    // re-registers this name mid-note (a new shared_ptr swapped into the
    // registry), this voice keeps playing the wave it started with instead
    // of jumping to the replacement mid-cycle.
    v.customWave = (instrument && instrument->waveform == WaveformType::Custom)
                       ? FindCustomWaveform(instrument->customWaveform)
                       : nullptr;

    v.ampEnv.stage = EnvelopeStage::Attack;
    v.ampEnv.stageTime = 0.0f;
    // ampEnv.envLevel is intentionally NOT reset to 0 here -- if the voice is
    // retriggered while still releasing, this avoids a click by having
    // the attack ramp continue from wherever the level currently sits.

    v.modEnv.stage = EnvelopeStage::Attack;
    v.modEnv.stageTime = 0.0f;
}

void Synth::NoteOff(int channel)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (channel < 0 || channel >= (int)voices_.size()) return;

    Voice& v = voices_[channel];
    if (v.ampEnv.stage != EnvelopeStage::Idle && v.ampEnv.stage != EnvelopeStage::Release)
    {
        v.ampEnv.releaseStartLevel = v.ampEnv.envLevel;
        v.ampEnv.stage = EnvelopeStage::Release;
        v.ampEnv.stageTime = 0.0f;
    }
    if (v.modEnv.stage != EnvelopeStage::Idle && v.modEnv.stage != EnvelopeStage::Release)
    {
        v.modEnv.releaseStartLevel = v.modEnv.envLevel;
        v.modEnv.stage = EnvelopeStage::Release;
        v.modEnv.stageTime = 0.0f;
    }
}

float Synth::GenerateOscillatorSample(Voice& voice, float frequency)
{
    if (!voice.instrument) return 0.0f;

    float sample = 0.0f;
    switch (voice.instrument->waveform)
    {
        case WaveformType::Square:
        {
            sample = (voice.phase < voice.instrument->dutyCycle) ? 1.0f : -1.0f;
            break;
        }
        case WaveformType::Triangle:
        {
            sample = 4.0f * fabsf(voice.phase - 0.5f) - 1.0f;
            break;
        }
        case WaveformType::Sawtooth:
        {
            sample = 2.0f * voice.phase - 1.0f;
            break;
        }
        case WaveformType::Sine:
        {
            sample = sinf(voice.phase * PI2);
            break;
        }
        case WaveformType::Noise:
        {
            // 15-bit LFSR, Game Boy channel 4 style
            uint16_t lfsr = voice.noiseLfsr;
            uint16_t bit = (uint16_t)((lfsr ^ (lfsr >> 1)) & 1u);
            lfsr = (uint16_t)((lfsr >> 1) | (bit << 14));
            voice.noiseLfsr = lfsr;
            sample = (lfsr & 1u) ? 1.0f : -1.0f;
            break;
        }
        case WaveformType::Custom:
        {
            // Linear-interpolated lookup into a plugin-registered wavetable
            // (one full cycle, any length) -- the same technique classic
            // wavetable synths use. Falls through to silence if the name
            // isn't registered (e.g. the Wave Editor plugin hasn't run yet
            // this session).
            if (voice.customWave && !voice.customWave->empty())
            {
                const auto& table = *voice.customWave;
                float pos = voice.phase * (float)table.size();
                int i0 = (int)pos % (int)table.size();
                int i1 = (i0 + 1) % (int)table.size();
                float frac = pos - floorf(pos);
                sample = table[i0] + (table[i1] - table[i0]) * frac;
            }
            break;
        }
    }

    voice.phase += frequency / (float)sampleRate_;
    if (voice.phase >= 1.0f) voice.phase -= 1.0f;

    return sample;
}

void Synth::AdvanceEnvelopeState(EnvelopeState& es, const Envelope& env, float dt)
{
    if (es.stage == EnvelopeStage::Idle) return;
    es.stageTime += dt;

    switch (es.stage)
    {
        case EnvelopeStage::Attack:
        {
            float t = (env.attack > 0.0f) ? (es.stageTime / env.attack) : 1.0f;
            if (t >= 1.0f)
            {
                es.envLevel = 1.0f;
                es.stage = EnvelopeStage::Decay;
                es.stageTime = 0.0f;
            }
            else
            {
                es.envLevel = t;
            }
            break;
        }
        case EnvelopeStage::Decay:
        {
            float t = (env.decay > 0.0f) ? (es.stageTime / env.decay) : 1.0f;
            if (t >= 1.0f)
            {
                es.envLevel = env.sustain;
                es.stage = EnvelopeStage::Sustain;
                es.stageTime = 0.0f;
            }
            else
            {
                es.envLevel = 1.0f + (env.sustain - 1.0f) * t;
            }
            break;
        }
        case EnvelopeStage::Sustain:
        {
            es.envLevel = env.sustain;
            break;
        }
        case EnvelopeStage::Release:
        {
            float t = (env.release > 0.0f) ? (es.stageTime / env.release) : 1.0f;
            if (t >= 1.0f)
            {
                es.envLevel = 0.0f;
                es.stage = EnvelopeStage::Idle;
            }
            else
            {
                es.envLevel = es.releaseStartLevel * (1.0f - t);
            }
            break;
        }
        default: break;
    }
}

void Synth::AdvanceArp(Voice& voice, float dt)
{
    if (!voice.instrument || voice.instrument->arpPattern == ArpPattern::Off)
    {
        voice.frequency = voice.baseFrequency;
        return;
    }

    static const int kOctaveIntervals[] = { 0, 12 };
    static const int kMajorIntervals[]  = { 0, 4, 7 };
    static const int kMinorIntervals[]  = { 0, 3, 7 };

    const int* intervals = kOctaveIntervals;
    int count = 2;
    switch (voice.instrument->arpPattern)
    {
        case ArpPattern::Octave:     intervals = kOctaveIntervals; count = 2; break;
        case ArpPattern::MajorChord: intervals = kMajorIntervals;  count = 3; break;
        case ArpPattern::MinorChord: intervals = kMinorIntervals;  count = 3; break;
        default: break;
    }

    float rate = std::max(1.0f, voice.instrument->arpRate);
    float stepDuration = 1.0f / rate;
    voice.arpTimer += dt;
    while (voice.arpTimer >= stepDuration)
    {
        voice.arpTimer -= stepDuration;
        voice.arpStep = (voice.arpStep + 1) % count;
    }

    voice.frequency = voice.baseFrequency * powf(2.0f, (float)intervals[voice.arpStep % count] / 12.0f);
}

float Synth::ApplyFilter(Voice& voice, float sample, float cutoff01)
{
    if (!voice.instrument || voice.instrument->filterType == FilterType::None) return sample;

    // one-pole (6dB/octave) filter: y += alpha*(x-y) is the discretized RC
    // lowpass; alpha derived from an exponential 20Hz..~20kHz cutoff range
    // so the slider feels even across the whole sweep. Highpass is just the
    // signal minus its own lowpassed version.
    float cutoffHz = 20.0f * powf(1000.0f, std::clamp(cutoff01, 0.0f, 1.0f));
    float rc = 1.0f / (PI2 * cutoffHz);
    float dt = 1.0f / (float)sampleRate_;
    float alpha = dt / (rc + dt);

    voice.filterState += alpha * (sample - voice.filterState);

    if (voice.instrument->filterType == FilterType::LowPass) return voice.filterState;
    return sample - voice.filterState; // HighPass
}

void Synth::GenerateSamples(float* buffer, unsigned int frameCount)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const float dt = 1.0f / (float)sampleRate_;
    constexpr float kLimiterThreshold = 0.95f; // just under full scale, leaves a little ceiling

    for (unsigned int i = 0; i < frameCount; ++i)
    {
        float left = 0.0f, right = 0.0f;
        float scopeSample = 0.0f; // sum of every voice currently playing scopeInstrument_, this frame

        for (auto& voice : voices_)
        {
            if (voice.ampEnv.stage == EnvelopeStage::Idle) continue;
            if (!voice.instrument) continue;
            const Instrument& inst = *voice.instrument;

            // ---- modulation sources (advanced regardless of mute/solo, so a
            // voice picks up mid-note cleanly if it becomes audible again) ----
            AdvanceArp(voice, dt); // updates voice.frequency from voice.baseFrequency

            AdvanceEnvelopeState(voice.modEnv, inst.env2, dt); // harmless no-op unless env2Target is actually used
            float modLevel = voice.modEnv.envLevel;

            float pitchSemitones = 0.0f;
            if (inst.env2Target == Env2Target::Pitch) pitchSemitones += modLevel * inst.env2Amount;

            if (inst.vibratoEnabled && inst.vibratoDepth > 0.0f)
            {
                pitchSemitones += sinf(voice.lfoPhase * PI2) * inst.vibratoDepth;
                voice.lfoPhase += inst.vibratoRate * dt;
                if (voice.lfoPhase >= 1.0f) voice.lfoPhase -= 1.0f;
            }

            float effFrequency = voice.frequency * powf(2.0f, pitchSemitones / 12.0f);

            // ---- oscillator -> filter -> amplitude envelope, classic subtractive-synth order ----
            float osc = GenerateOscillatorSample(voice, effFrequency);

            float cutoff01 = inst.filterCutoff;
            if (inst.env2Target == Env2Target::FilterCutoff) cutoff01 += modLevel * inst.env2Amount;
            float filtered = ApplyFilter(voice, osc, std::clamp(cutoff01, 0.0f, 1.0f));

            AdvanceEnvelopeState(voice.ampEnv, inst.envelope, dt);
            if (voice.ampEnv.stage == EnvelopeStage::Idle) voice.active = false;

            bool audible = !inst.muted && (!anySolo_ || inst.solo);
            if (!audible) continue;

            float sample = filtered * voice.ampEnv.envLevel * inst.volume * voice.velocity;

            if (scopeInstrument_ && voice.instrument == scopeInstrument_) scopeSample += sample;

            // constant-power pan: -1 = full left, 0 = center, +1 = full right
            float pan = std::clamp(inst.pan, -1.0f, 1.0f);
            float t = (pan + 1.0f) * 0.5f;
            float leftGain = sqrtf(1.0f - t);
            float rightGain = sqrtf(t);

            left += sample * leftGain;
            right += sample * rightGain;
        }

        left *= masterVolume_;
        right *= masterVolume_;

        // simple peak limiter on the final mix: only pulls gain down when the
        // signal is actually about to clip, instead of a fixed headroom
        // division permanently quietening everything regardless of how many
        // voices are actually playing
        float peak = std::max(fabsf(left), fabsf(right));
        float desiredGain = (peak > kLimiterThreshold) ? (kLimiterThreshold / peak) : 1.0f;
        float coeff = (desiredGain < limiterGain_) ? limiterAttackCoeff_ : limiterReleaseCoeff_;
        limiterGain_ += (desiredGain - limiterGain_) * coeff;

        float finalLeft = left * limiterGain_;
        float finalRight = right * limiterGain_;
        buffer[i * 2 + 0] = finalLeft;
        buffer[i * 2 + 1] = finalRight;

        if (!scopeBuffer_.empty())
        {
            scopeBuffer_[scopeWritePos_] = scopeSample;
            scopeWritePos_ = (scopeWritePos_ + 1) % (int)scopeBuffer_.size();
        }

        if (!masterScopeLeft_.empty())
        {
            masterScopeLeft_[masterScopeWritePos_] = finalLeft;
            masterScopeRight_[masterScopeWritePos_] = finalRight;
            masterScopeWritePos_ = (masterScopeWritePos_ + 1) % (int)masterScopeLeft_.size();
        }
    }
}
