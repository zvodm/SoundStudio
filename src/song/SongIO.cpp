#include "song/SongIO.h"
#include "song/InstrumentIO.h"
#include "song/BinaryIO.h"
#include <cstdio>
#include <cstring>
#include <vector>

static constexpr char     SONG_MAGIC[4] = { 'S', 'S', 'N', 'G' };
static constexpr uint32_t SONG_VERSION  = 6; // v1 = tracker Pattern only. v2 = adds piano-roll tracks.
                                              // v3 = adds song.masterVolume and per-instrument pan/muted/solo.
                                              // v4 = replaces the single flat piano-roll track list with
                                              //      Section (name + per-section tempo + tracks) and an
                                              //      arrangement (sequence of section indices).
                                              // v5 = adds PlacedNote::velocity, and per-instrument
                                              //      category/arpeggiator/vibrato/filter/second-envelope.
                                              // v6 = adds Instrument::customWaveform (name of a plugin-
                                              //      registered custom wavetable; only used when
                                              //      waveform == WaveformType::Custom).

// --------------------------------------------------------------
// Note / Pattern (legacy tracker grid) helpers
// --------------------------------------------------------------

static void WritePattern(FILE* f, const Pattern& pat)
{
    WriteRaw(f, pat.numRows);
    WriteRaw(f, pat.numChannels);
    // cells are POD, safe to write as a raw block
    fwrite(pat.cells.data(), sizeof(Note), pat.cells.size(), f);
}

static bool ReadPattern(FILE* f, Pattern& pat)
{
    if (!ReadRaw(f, pat.numRows)) return false;
    if (!ReadRaw(f, pat.numChannels)) return false;
    size_t count = (size_t)pat.numRows * pat.numChannels;
    pat.cells.resize(count);
    if (count > 0 && fread(pat.cells.data(), sizeof(Note), count, f) != count) return false;
    return true;
}

// --------------------------------------------------------------
// PlacedNote / PianoRollTrack helpers
// --------------------------------------------------------------

// v5+: each field written individually rather than as a raw struct blob, so
// the on-disk layout never depends on compiler struct padding.
static void WritePlacedNote(FILE* f, const PlacedNote& n)
{
    WriteRaw(f, n.pitch);
    WriteRaw(f, n.startStep);
    WriteRaw(f, n.length);
    WriteRaw(f, n.instrument);
    WriteRaw(f, n.velocity);
}

// Pre-v5 on-disk layout: PlacedNote minus `velocity`, written as one raw
// struct blob (the format PlacedNote itself used before this field existed).
// Its field types/order exactly match what those old files' writer used, so
// bulk-reading into this lets old files load correctly without depending on
// PlacedNote's current (larger) size.
namespace legacy
{
struct PlacedNoteV4
{
    int8_t  pitch;
    int     startStep;
    int     length;
    uint8_t instrument;
};
}

static bool ReadPlacedNotes(FILE* f, std::vector<PlacedNote>& notes, uint32_t noteCount, uint32_t version)
{
    notes.resize(noteCount);

    if (version >= 5)
    {
        for (auto& n : notes)
        {
            if (!ReadRaw(f, n.pitch)) return false;
            if (!ReadRaw(f, n.startStep)) return false;
            if (!ReadRaw(f, n.length)) return false;
            if (!ReadRaw(f, n.instrument)) return false;
            if (!ReadRaw(f, n.velocity)) return false;
        }
        return true;
    }

    std::vector<legacy::PlacedNoteV4> legacyNotes(noteCount);
    if (noteCount > 0 && fread(legacyNotes.data(), sizeof(legacy::PlacedNoteV4), noteCount, f) != noteCount)
        return false;

    for (uint32_t i = 0; i < noteCount; ++i)
    {
        notes[i].pitch = legacyNotes[i].pitch;
        notes[i].startStep = legacyNotes[i].startStep;
        notes[i].length = legacyNotes[i].length;
        notes[i].instrument = legacyNotes[i].instrument;
        notes[i].velocity = 127; // pre-v5 files have no velocity -- full velocity default
    }
    return true;
}

static void WritePianoRollTrack(FILE* f, const PianoRollTrack& track)
{
    WriteRaw(f, track.stepsPerBeat);
    WriteRaw(f, track.totalSteps);
    uint32_t noteCount = (uint32_t)track.notes.size();
    WriteRaw(f, noteCount);
    for (const auto& n : track.notes) WritePlacedNote(f, n);
}

static bool ReadPianoRollTrack(FILE* f, PianoRollTrack& track, uint32_t version)
{
    if (!ReadRaw(f, track.stepsPerBeat)) return false;
    if (!ReadRaw(f, track.totalSteps)) return false;
    uint32_t noteCount = 0;
    if (!ReadRaw(f, noteCount)) return false;
    return ReadPlacedNotes(f, track.notes, noteCount, version);
}

static void WriteSection(FILE* f, const Section& section)
{
    WriteString(f, section.name);
    WriteRaw(f, section.bpm);
    uint32_t trackCount = (uint32_t)section.tracks.size();
    WriteRaw(f, trackCount);
    for (const auto& track : section.tracks) WritePianoRollTrack(f, track);
}

static bool ReadSection(FILE* f, Section& section, uint32_t version)
{
    if (!ReadString(f, section.name)) return false;
    if (!ReadRaw(f, section.bpm)) return false;
    uint32_t trackCount = 0;
    if (!ReadRaw(f, trackCount)) return false;
    section.tracks.resize(trackCount);
    for (auto& track : section.tracks)
        if (!ReadPianoRollTrack(f, track, version)) return false;
    return true;
}

// --------------------------------------------------------------
// Public API
// --------------------------------------------------------------

bool SaveSong(const Song& song, const std::string& path)
{
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;

    fwrite(SONG_MAGIC, 1, 4, f);
    WriteRaw(f, SONG_VERSION);

    WriteString(f, song.name);
    WriteRaw(f, song.bpm);
    WriteRaw(f, song.rowsPerBeat);
    WriteRaw(f, song.masterVolume);

    uint32_t instCount = (uint32_t)song.instruments.size();
    WriteRaw(f, instCount);
    for (const auto& inst : song.instruments) WriteInstrument(f, inst);

    uint32_t patCount = (uint32_t)song.patterns.size();
    WriteRaw(f, patCount);
    for (const auto& pat : song.patterns) WritePattern(f, pat);

    uint32_t orderCount = (uint32_t)song.order.size();
    WriteRaw(f, orderCount);
    if (orderCount > 0) fwrite(song.order.data(), sizeof(uint16_t), orderCount, f);

    uint32_t sectionCount = (uint32_t)song.sections.size();
    WriteRaw(f, sectionCount);
    for (const auto& section : song.sections) WriteSection(f, section);

    uint32_t arrangementCount = (uint32_t)song.arrangement.size();
    WriteRaw(f, arrangementCount);
    if (arrangementCount > 0) fwrite(song.arrangement.data(), sizeof(int32_t), arrangementCount, f);

    fclose(f);
    return true;
}

bool LoadSong(Song& outSong, const std::string& path)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;

    char magic[4] = {};
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, SONG_MAGIC, 4) != 0)
    {
        fclose(f);
        return false;
    }

    uint32_t version = 0;
    if (!ReadRaw(f, version) || version < 1 || version > SONG_VERSION)
    {
        fclose(f); // unknown version -- bail rather than misread the rest of the file
        return false;
    }

    Song song;
    if (!ReadString(f, song.name)) { fclose(f); return false; }
    if (!ReadRaw(f, song.bpm)) { fclose(f); return false; }
    if (!ReadRaw(f, song.rowsPerBeat)) { fclose(f); return false; }
    if (version >= 3)
    {
        if (!ReadRaw(f, song.masterVolume)) { fclose(f); return false; }
    }
    // v1/v2 files have no masterVolume -- Song{}'s default (1.0) applies

    uint32_t instCount = 0;
    if (!ReadRaw(f, instCount)) { fclose(f); return false; }
    song.instruments.resize(instCount);
    for (auto& inst : song.instruments)
        if (!ReadInstrument(f, inst, version)) { fclose(f); return false; }

    uint32_t patCount = 0;
    if (!ReadRaw(f, patCount)) { fclose(f); return false; }
    song.patterns.resize(patCount);
    for (auto& pat : song.patterns)
        if (!ReadPattern(f, pat)) { fclose(f); return false; }

    uint32_t orderCount = 0;
    if (!ReadRaw(f, orderCount)) { fclose(f); return false; }
    song.order.resize(orderCount);
    if (orderCount > 0 && fread(song.order.data(), sizeof(uint16_t), orderCount, f) != orderCount)
    {
        fclose(f);
        return false;
    }

    if (version >= 4)
    {
        uint32_t sectionCount = 0;
        if (!ReadRaw(f, sectionCount)) { fclose(f); return false; }
        song.sections.resize(sectionCount);
        for (auto& section : song.sections)
            if (!ReadSection(f, section, version)) { fclose(f); return false; }

        uint32_t arrangementCount = 0;
        if (!ReadRaw(f, arrangementCount)) { fclose(f); return false; }
        song.arrangement.resize(arrangementCount);
        if (arrangementCount > 0 && fread(song.arrangement.data(), sizeof(int32_t), arrangementCount, f) != arrangementCount)
        {
            fclose(f);
            return false;
        }
    }
    else if (version >= 2)
    {
        // pre-arrangement (v2/v3) file: one implicit section, stored flat.
        // song.tracks stays populated here; the caller is responsible for
        // wrapping it into a single Section + arrangement.
        uint32_t trackCount = 0;
        if (!ReadRaw(f, trackCount)) { fclose(f); return false; }
        song.tracks.resize(trackCount);
        for (auto& track : song.tracks)
            if (!ReadPianoRollTrack(f, track, version)) { fclose(f); return false; }
    }
    // v1 files have neither -- song.tracks and song.sections both stay empty.

    fclose(f);
    outSong = std::move(song);
    return true;
}
