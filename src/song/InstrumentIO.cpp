#include "song/InstrumentIO.h"
#include "song/BinaryIO.h"
#include <cstring>

void WriteInstrument(FILE* f, const Instrument& inst)
{
    WriteString(f, inst.name);
    WriteRaw(f, inst.waveform);
    WriteRaw(f, inst.dutyCycle);
    WriteRaw(f, inst.envelope.attack);
    WriteRaw(f, inst.envelope.decay);
    WriteRaw(f, inst.envelope.sustain);
    WriteRaw(f, inst.envelope.release);
    WriteRaw(f, inst.volume);
    WriteRaw(f, inst.pan);
    WriteRaw(f, inst.muted);
    WriteRaw(f, inst.solo);
    WriteRaw(f, inst.category);
    WriteRaw(f, inst.arpPattern);
    WriteRaw(f, inst.arpRate);
    WriteRaw(f, inst.vibratoEnabled);
    WriteRaw(f, inst.vibratoRate);
    WriteRaw(f, inst.vibratoDepth);
    WriteRaw(f, inst.filterType);
    WriteRaw(f, inst.filterCutoff);
    WriteRaw(f, inst.env2Target);
    WriteRaw(f, inst.env2.attack);
    WriteRaw(f, inst.env2.decay);
    WriteRaw(f, inst.env2.sustain);
    WriteRaw(f, inst.env2.release);
    WriteRaw(f, inst.env2Amount);
    WriteString(f, inst.customWaveform);
}

bool ReadInstrument(FILE* f, Instrument& inst, uint32_t version)
{
    if (!ReadString(f, inst.name)) return false;
    if (!ReadRaw(f, inst.waveform)) return false;
    if (!ReadRaw(f, inst.dutyCycle)) return false;
    if (!ReadRaw(f, inst.envelope.attack)) return false;
    if (!ReadRaw(f, inst.envelope.decay)) return false;
    if (!ReadRaw(f, inst.envelope.sustain)) return false;
    if (!ReadRaw(f, inst.envelope.release)) return false;
    if (!ReadRaw(f, inst.volume)) return false;

    if (version >= 3)
    {
        if (!ReadRaw(f, inst.pan)) return false;
        if (!ReadRaw(f, inst.muted)) return false;
        if (!ReadRaw(f, inst.solo)) return false;
    }
    // v1/v2 files simply have no pan/muted/solo -- Instrument{}'s defaults apply

    if (version >= 5)
    {
        if (!ReadRaw(f, inst.category)) return false;
        if (!ReadRaw(f, inst.arpPattern)) return false;
        if (!ReadRaw(f, inst.arpRate)) return false;
        if (!ReadRaw(f, inst.vibratoEnabled)) return false;
        if (!ReadRaw(f, inst.vibratoRate)) return false;
        if (!ReadRaw(f, inst.vibratoDepth)) return false;
        if (!ReadRaw(f, inst.filterType)) return false;
        if (!ReadRaw(f, inst.filterCutoff)) return false;
        if (!ReadRaw(f, inst.env2Target)) return false;
        if (!ReadRaw(f, inst.env2.attack)) return false;
        if (!ReadRaw(f, inst.env2.decay)) return false;
        if (!ReadRaw(f, inst.env2.sustain)) return false;
        if (!ReadRaw(f, inst.env2.release)) return false;
        if (!ReadRaw(f, inst.env2Amount)) return false;
    }
    // v1-v4 files have no category/arp/vibrato/filter/second-envelope --
    // Instrument{}'s defaults apply (category Lead, everything else off)

    if (version >= 6)
    {
        if (!ReadString(f, inst.customWaveform)) return false;
    }
    // v1-v5 files have no customWaveform -- Instrument{}'s default (empty) applies

    return true;
}

static constexpr char     INSTRUMENT_MAGIC[4] = { 'S', 'S', 'I', 'P' };
static constexpr uint32_t INSTRUMENT_PRESET_VERSION = 1; // bump if the preset *container* format ever changes independent of the instrument field set

bool SaveInstrumentPreset(const Instrument& inst, const std::string& path)
{
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;

    fwrite(INSTRUMENT_MAGIC, 1, 4, f);
    WriteRaw(f, INSTRUMENT_PRESET_VERSION);
    WriteInstrument(f, inst);

    fclose(f);
    return true;
}

bool LoadInstrumentPreset(Instrument& outInst, const std::string& path)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;

    char magic[4] = {};
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, INSTRUMENT_MAGIC, 4) != 0)
    {
        fclose(f);
        return false;
    }

    uint32_t version = 0;
    if (!ReadRaw(f, version) || version < 1 || version > INSTRUMENT_PRESET_VERSION)
    {
        fclose(f);
        return false;
    }

    Instrument inst;
    // presets are only ever written by this same build, so always read the
    // full current instrument field set rather than gating on any older
    // song-format version
    if (!ReadInstrument(f, inst, CURRENT_INSTRUMENT_FIELD_VERSION))
    {
        fclose(f);
        return false;
    }

    fclose(f);
    outInst = std::move(inst);
    return true;
}
