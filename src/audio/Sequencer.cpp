#include "audio/Sequencer.h"
#include "audio/AudioConfig.h"
#include "audio/MusicUtils.h"
#include <algorithm>

void Sequencer::TogglePlay(int countInBeats)
{
    playing_ = !playing_;
    if (!playing_)
    {
        StopAllNotes();
        countInBeatsRemaining_ = 0;
        countInTimer_ = 0.0f;
    }
    else
    {
        currentStep_ = 0;
        stepTimer_ = 0.0f;
        arrangementIndex_ = 0; // always restart the arrangement from the top
        countInBeatsRemaining_ = std::max(0, countInBeats);
        countInTimer_ = 0.0f;
    }
}

void Sequencer::Stop()
{
    playing_ = false;
    currentSectionIndex_ = -1;
    countInBeatsRemaining_ = 0;
    countInTimer_ = 0.0f;
    StopAllNotes();
}

void Sequencer::StopAllNotes()
{
    if (synthRef_)
    {
        for (auto& a : activeNotes_) synthRef_->NoteOff(a.channel);
        synthRef_->NoteOff(METRONOME_CHANNEL); // cut off a still-ringing click too
    }
    activeNotes_.clear();
}

void Sequencer::ConfigureMetronomeInstrument()
{
    if (metronomeConfigured_) return;
    metronomeConfigured_ = true;

    // A short, plain sine blip -- loud enough to cut through the mix
    // without being mistaken for part of the song.
    metronomeInstrument_.waveform = WaveformType::Sine;
    metronomeInstrument_.volume = 0.5f;
    metronomeInstrument_.pan = 0.0f;
    metronomeInstrument_.envelope.attack = 0.001f;
    metronomeInstrument_.envelope.decay = 0.04f;
    metronomeInstrument_.envelope.sustain = 0.0f;
    metronomeInstrument_.envelope.release = 0.02f;
}

void Sequencer::PlayClick(Synth& synth, bool accent)
{
    ConfigureMetronomeInstrument();
    synth.NoteOff(METRONOME_CHANNEL); // in case the previous click is still ringing
    float freq = accent ? 1600.0f : 1100.0f; // higher pitch marks the start of a section/count-in
    synth.NoteOn(METRONOME_CHANNEL, freq, &metronomeInstrument_);
}

Section* Sequencer::ResolveCurrentSection(std::vector<Section>& sections, const std::vector<int32_t>& arrangement,
                                           int editingSection, bool loopEditingSectionOnly)
{
    if (loopEditingSectionOnly)
    {
        if (editingSection < 0 || editingSection >= (int)sections.size()) { currentSectionIndex_ = -1; return nullptr; }
        Section& s = sections[editingSection];
        if (s.tracks.empty()) { currentSectionIndex_ = -1; return nullptr; }
        currentSectionIndex_ = editingSection;
        return &s;
    }

    if (arrangement.empty()) { currentSectionIndex_ = -1; return nullptr; }
    if (arrangementIndex_ < 0 || arrangementIndex_ >= (int)arrangement.size()) arrangementIndex_ = 0;

    int idx = arrangement[arrangementIndex_];
    if (idx < 0 || idx >= (int)sections.size()) { currentSectionIndex_ = -1; return nullptr; }

    Section& s = sections[idx];
    if (s.tracks.empty()) { currentSectionIndex_ = -1; return nullptr; }
    currentSectionIndex_ = idx;
    return &s;
}

void Sequencer::Update(std::vector<Section>& sections,
                        const std::vector<int32_t>& arrangement,
                        int editingSection,
                        bool loopEditingSectionOnly,
                        std::vector<Instrument>& instruments,
                        Synth& synth,
                        float dt,
                        int stepsPerBeat,
                        bool metronomeEnabled)
{
    synthRef_ = &synth;
    if (!playing_ || stepsPerBeat <= 0) return;

    Section* section = ResolveCurrentSection(sections, arrangement, editingSection, loopEditingSectionOnly);
    if (!section) { Stop(); return; }

    // Count-in: the arrangement hasn't actually started yet -- just click
    // once per beat (always, regardless of metronomeEnabled -- a count-in
    // you can't hear isn't a count-in) until the countdown reaches zero.
    if (countInBeatsRemaining_ > 0)
    {
        float beatDuration = 60.0f / section->bpm;
        countInTimer_ += dt;
        while (countInTimer_ >= beatDuration && countInBeatsRemaining_ > 0)
        {
            countInTimer_ -= beatDuration;
            PlayClick(synth, /*accent=*/true);
            countInBeatsRemaining_--;
        }
        return;
    }

    float stepDuration = 60.0f / section->bpm / (float)stepsPerBeat;
    stepTimer_ += dt;

    while (stepTimer_ >= stepDuration)
    {
        int totalSteps = section->tracks[0].totalSteps;
        if (totalSteps <= 0) { Stop(); return; }

        stepTimer_ -= stepDuration;

        if (metronomeEnabled && currentStep_ % stepsPerBeat == 0)
            PlayClick(synth, /*accent=*/currentStep_ == 0);

        // release notes ending on this step
        for (int i = (int)activeNotes_.size() - 1; i >= 0; --i)
        {
            if (activeNotes_[i].endStep == currentStep_)
            {
                synth.NoteOff(activeNotes_[i].channel);
                activeNotes_.erase(activeNotes_.begin() + i);
            }
        }

        // trigger notes starting on this step, across every track in the current section
        for (size_t t = 0; t < section->tracks.size() && t < instruments.size(); ++t)
        {
            for (const auto& n : section->tracks[t].notes)
            {
                if (n.startStep != currentStep_) continue;

                int channel = -1;
                for (int c = 1; c < NUM_VOICES; ++c)
                {
                    bool used = false;
                    for (const auto& a : activeNotes_) if (a.channel == c) { used = true; break; }
                    if (!used) { channel = c; break; }
                }
                if (channel == -1) continue; // out of voices -- note dropped (voice stealing can come later)

                synth.NoteOn(channel, NoteToFrequency(n.pitch), &instruments[t], n.velocity / 127.0f);
                activeNotes_.push_back({ (n.startStep + n.length) % totalSteps, channel });
            }
        }

        currentStep_++;
        if (currentStep_ >= totalSteps)
        {
            currentStep_ = 0;
            StopAllNotes(); // new section (or looping back) -- don't carry ringing notes across the boundary

            if (!loopEditingSectionOnly)
                arrangementIndex_ = (arrangementIndex_ + 1) % (int)arrangement.size();

            section = ResolveCurrentSection(sections, arrangement, editingSection, loopEditingSectionOnly);
            if (!section) { Stop(); return; }

            // tempo may have changed crossing into the new section -- recompute
            stepDuration = 60.0f / section->bpm / (float)stepsPerBeat;
        }
    }
}
