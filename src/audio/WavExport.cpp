#include "audio/WavExport.h"
#include "audio/Synth.h"
#include "audio/MusicUtils.h"
#include "audio/AudioConfig.h"
#include <cstdio>
#include <algorithm>
#include <cmath>

namespace
{

constexpr int WAV_SAMPLE_RATE = 44100;
constexpr int RENDER_BLOCK = 256;         // samples rendered per Synth::GenerateSamples call
constexpr float TAIL_SECONDS_MAX = 3.0f;  // hard cap on trailing silence, in case a voice never settles

struct ActiveNote { int endStep; int channel; };

// Standard 44-byte RIFF/WAVE header, 16-bit PCM.
bool WriteWavFile(const std::string& path, const std::vector<int16_t>& interleavedStereo, int sampleRate)
{
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;

    uint32_t numFrames = (uint32_t)(interleavedStereo.size() / 2);
    uint16_t numChannels = 2;
    uint16_t bitsPerSample = 16;
    uint32_t byteRate = (uint32_t)sampleRate * numChannels * (bitsPerSample / 8);
    uint16_t blockAlign = (uint16_t)(numChannels * (bitsPerSample / 8));
    uint32_t dataSize = numFrames * blockAlign;
    uint32_t riffSize = 36 + dataSize;
    uint32_t fmtChunkSize = 16;
    uint16_t audioFormat = 1; // PCM

    fwrite("RIFF", 1, 4, f);
    fwrite(&riffSize, 4, 1, f);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    fwrite(&fmtChunkSize, 4, 1, f);
    fwrite(&audioFormat, 2, 1, f);
    fwrite(&numChannels, 2, 1, f);
    fwrite(&sampleRate, 4, 1, f);
    fwrite(&byteRate, 4, 1, f);
    fwrite(&blockAlign, 2, 1, f);
    fwrite(&bitsPerSample, 2, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&dataSize, 4, 1, f);

    if (!interleavedStereo.empty())
        fwrite(interleavedStereo.data(), sizeof(int16_t), interleavedStereo.size(), f);

    fclose(f);
    return true;
}

// Renders `frameCount` frames from `synth` and appends them (as clamped
// 16-bit PCM) onto `out`.
void RenderBlockAppend(Synth& synth, int frameCount, std::vector<int16_t>& out)
{
    std::vector<float> block((size_t)frameCount * 2);
    synth.GenerateSamples(block.data(), (unsigned int)frameCount);

    size_t base = out.size();
    out.resize(base + block.size());
    for (size_t i = 0; i < block.size(); ++i)
    {
        float clamped = std::clamp(block[i], -1.0f, 1.0f);
        out[base + i] = (int16_t)std::lround(clamped * 32767.0f);
    }
}

} // namespace

bool ExportSongToWav(const std::vector<Section>& sections,
                      const std::vector<int32_t>& arrangement,
                      const std::vector<Instrument>& instruments,
                      int stepsPerBeat,
                      const std::string& path)
{
    if (arrangement.empty() || sections.empty() || instruments.empty() || stepsPerBeat <= 0)
        return false;

    // A separate Synth instance -- rendering must never touch the live
    // playback Synth that the real-time audio callback is reading from.
    Synth renderSynth;
    renderSynth.Init(WAV_SAMPLE_RATE, NUM_VOICES);

    std::vector<int16_t> pcm;
    std::vector<ActiveNote> activeNotes;

    auto stopAll = [&]()
    {
        for (auto& a : activeNotes) renderSynth.NoteOff(a.channel);
        activeNotes.clear();
    };

    bool renderedAnything = false;

    for (int32_t secIdx : arrangement)
    {
        if (secIdx < 0 || secIdx >= (int)sections.size()) continue;
        const Section& section = sections[secIdx];
        if (section.tracks.empty()) continue;

        int totalSteps = section.tracks[0].totalSteps;
        if (totalSteps <= 0 || section.bpm <= 0.0f) continue;

        float stepDuration = 60.0f / section.bpm / (float)stepsPerBeat;
        int samplesPerStep = std::max(1, (int)std::lround((double)stepDuration * WAV_SAMPLE_RATE));

        for (int step = 0; step < totalSteps; ++step)
        {
            // release notes ending on this step
            for (int i = (int)activeNotes.size() - 1; i >= 0; --i)
            {
                if (activeNotes[i].endStep == step)
                {
                    renderSynth.NoteOff(activeNotes[i].channel);
                    activeNotes.erase(activeNotes.begin() + i);
                }
            }

            // trigger notes starting on this step, across every track
            for (size_t t = 0; t < section.tracks.size() && t < instruments.size(); ++t)
            {
                for (const auto& n : section.tracks[t].notes)
                {
                    if (n.startStep != step) continue;

                    int channel = -1;
                    for (int c = 1; c < NUM_VOICES; ++c)
                    {
                        bool used = false;
                        for (const auto& a : activeNotes) if (a.channel == c) { used = true; break; }
                        if (!used) { channel = c; break; }
                    }
                    if (channel == -1) continue; // out of voices -- same voice-stealing tradeoff as live playback

                    renderSynth.NoteOn(channel, NoteToFrequency(n.pitch), &instruments[t], n.velocity / 127.0f);
                    activeNotes.push_back({ (n.startStep + n.length) % totalSteps, channel });
                }
            }

            int samplesLeft = samplesPerStep;
            while (samplesLeft > 0)
            {
                int n = std::min(samplesLeft, RENDER_BLOCK);
                RenderBlockAppend(renderSynth, n, pcm);
                samplesLeft -= n;
            }
            renderedAnything = true;
        }

        stopAll(); // don't carry ringing notes across a section boundary -- matches live playback
    }

    if (!renderedAnything) return false;

    // Tail: let release envelopes finish ringing out after the last note,
    // instead of cutting the file off mid-decay.
    int tailSamplesLeft = (int)(WAV_SAMPLE_RATE * TAIL_SECONDS_MAX);
    while (tailSamplesLeft > 0 && renderSynth.AnyVoiceActive())
    {
        int n = std::min(tailSamplesLeft, RENDER_BLOCK);
        RenderBlockAppend(renderSynth, n, pcm);
        tailSamplesLeft -= n;
    }

    return WriteWavFile(path, pcm, WAV_SAMPLE_RATE);
}
