#include "song/DefaultInstruments.h"
#include "song/InstrumentIO.h"
#include "song/SongData.h"
#include "app/AppPaths.h"
#include <cstdio>
#include <string>

namespace
{

// A whole preset as one row of a table. Every field has a sane default so
// each entry below only has to state what's actually characteristic of that
// instrument -- a flute is "a soft attack and a touch of vibrato", not
// twenty-four numbers.
//
// NOTE: `wave` defaults to Custom, and `custom` names one of the
// single-cycle tables from RegisterBuiltinWaveforms(). Rows that want a
// plain analytic oscillator set `.wave` explicitly and leave `.custom`
// empty. Designated initializers must stay in declaration order (C++20),
// so keep the rows below ordered the same way as this struct.
struct Patch
{
    const char*  file;                                  // filename, no extension
    const char*  name;                                  // instrument name inside the preset
    const char*  group  = "";                           // family, for the installer's pick-list
    WaveformType wave   = WaveformType::Custom;
    const char*  custom = "";
    float        duty   = 0.5f;                         // Square only
    float        a = 0.005f, d = 0.20f, s = 0.70f, r = 0.25f; // amplitude ADSR
    float        vol    = 0.85f;
    InstrumentCategory cat = InstrumentCategory::Lead;
    ArpPattern   arp     = ArpPattern::Off;
    float        arpRate = 12.0f;
    bool         vib     = false;
    float        vibRate = 5.0f, vibDepth = 0.0f;
    FilterType   filt    = FilterType::None;
    float        cutoff  = 1.0f;
    Env2Target   e2      = Env2Target::None;
    float        e2a = 0.001f, e2d = 0.25f, e2s = 0.0f, e2r = 0.20f, e2amt = 0.0f;
};

using W  = WaveformType;
using C  = InstrumentCategory;
using F  = FilterType;
using E  = Env2Target;
using Ar = ArpPattern;

const Patch kPatches[] =
{
    // ================= Pianos & struck keys =================
    // Sustain 0 with a long decay is what makes a struck string behave like
    // one: the note rings down on its own and holding the key longer
    // doesn't hold the level. The negative filter-cutoff second envelope is
    // the other half of it -- a piano's tone goes dark as it decays.
    { .file="GrandPiano",      .name="Grand Piano",         .group="Pianos & Keys", .custom="Piano",        .a=0.002f,.d=1.30f,.s=0.00f,.r=0.32f, .vol=0.90f, .filt=F::LowPass,.cutoff=0.92f, .e2=E::FilterCutoff,.e2d=0.55f,.e2amt=-0.20f },
    { .file="BrightPiano",     .name="Bright Piano",        .group="Pianos & Keys", .custom="Piano Bright", .a=0.001f,.d=1.10f,.s=0.00f,.r=0.28f, .vol=0.88f, .filt=F::LowPass,.cutoff=0.96f, .e2=E::FilterCutoff,.e2d=0.50f,.e2amt=-0.18f },
    { .file="UprightPiano",    .name="Upright Piano",       .group="Pianos & Keys", .custom="Piano",        .a=0.003f,.d=1.05f,.s=0.00f,.r=0.30f, .vol=0.86f, .filt=F::LowPass,.cutoff=0.80f },
    { .file="HonkyTonkPiano",  .name="Honky-Tonk Piano",    .group="Pianos & Keys", .custom="Piano Bright", .a=0.002f,.d=0.95f,.s=0.00f,.r=0.25f, .vol=0.85f, .vib=true,.vibRate=6.5f,.vibDepth=0.06f, .filt=F::LowPass,.cutoff=0.88f },
    { .file="ElectricPiano",   .name="Electric Piano",      .group="Pianos & Keys", .custom="EPiano",       .a=0.004f,.d=1.60f,.s=0.12f,.r=0.40f, .vol=0.90f, .filt=F::LowPass,.cutoff=0.85f },
    { .file="Wurlitzer",       .name="Wurlitzer",           .group="Pianos & Keys", .custom="EPiano",       .a=0.004f,.d=1.20f,.s=0.10f,.r=0.30f, .vol=0.88f, .vib=true,.vibRate=5.5f,.vibDepth=0.05f, .filt=F::LowPass,.cutoff=0.82f },
    { .file="Clavinet",        .name="Clavinet",            .group="Pianos & Keys", .custom="Clavinet",     .a=0.001f,.d=0.45f,.s=0.05f,.r=0.12f, .vol=0.82f, .filt=F::HighPass,.cutoff=0.28f },
    { .file="Harpsichord",     .name="Harpsichord",         .group="Pianos & Keys", .custom="Harpsichord",  .a=0.001f,.d=0.70f,.s=0.00f,.r=0.16f, .vol=0.80f },
    { .file="Celesta",         .name="Celesta",             .group="Pianos & Keys", .custom="Celesta",      .a=0.001f,.d=1.00f,.s=0.00f,.r=0.30f, .vol=0.78f },
    { .file="MusicBox",        .name="Music Box",           .group="Pianos & Keys", .custom="Music Box",    .a=0.001f,.d=1.30f,.s=0.00f,.r=0.35f, .vol=0.75f },

    // ================= Organs =================
    // An organ is the opposite of a piano: essentially no envelope at all.
    // Full sustain, near-instant attack, tiny release -- the pipe (or the
    // tonewheel) is either on or off, and all the character lives in the
    // harmonic stack of the wavetable.
    { .file="DrawbarOrgan",    .name="Drawbar Organ",       .group="Organs", .custom="Organ",        .a=0.012f,.d=0.02f,.s=1.00f,.r=0.06f, .vol=0.82f },
    { .file="JazzOrgan",       .name="Jazz Organ",          .group="Organs", .custom="Organ",        .a=0.020f,.d=0.05f,.s=0.95f,.r=0.08f, .vol=0.80f, .vib=true,.vibRate=6.0f,.vibDepth=0.04f },
    { .file="RockOrgan",       .name="Rock Organ",          .group="Organs", .custom="Organ Rock",   .a=0.008f,.d=0.04f,.s=0.95f,.r=0.06f, .vol=0.85f, .vib=true,.vibRate=6.5f,.vibDepth=0.07f },
    // The "percussion" stop on a drawbar organ: a short bright click on the
    // attack, which here is a fast positive cutoff sweep on the 2nd envelope.
    { .file="PercussiveOrgan", .name="Percussive Organ",    .group="Organs", .custom="Organ Rock",   .a=0.002f,.d=0.06f,.s=0.82f,.r=0.05f, .vol=0.85f, .filt=F::LowPass,.cutoff=0.62f, .e2=E::FilterCutoff,.e2d=0.12f,.e2r=0.05f,.e2amt=0.35f },
    { .file="ChurchOrgan",     .name="Church Organ",        .group="Organs", .custom="Organ Church", .a=0.070f,.d=0.05f,.s=1.00f,.r=0.30f, .vol=0.80f, .cat=C::Pad },
    { .file="FullOrgan",       .name="Full Organ (Mixtures)",.group="Organs", .custom="Organ Full",  .a=0.050f,.d=0.05f,.s=1.00f,.r=0.28f, .vol=0.78f, .cat=C::Pad },
    { .file="ReedOrgan",       .name="Reed Organ",          .group="Organs", .custom="Organ Reed",   .a=0.030f,.d=0.06f,.s=0.96f,.r=0.12f, .vol=0.72f, .filt=F::LowPass,.cutoff=0.72f },
    { .file="Harmonium",       .name="Harmonium",           .group="Organs", .custom="Organ Reed",   .a=0.060f,.d=0.08f,.s=0.95f,.r=0.18f, .vol=0.72f, .vib=true,.vibRate=4.5f,.vibDepth=0.03f, .filt=F::LowPass,.cutoff=0.62f },
    // Tremulant -- the theatre organ's signature wobble.
    { .file="TheatreOrgan",    .name="Theatre Organ",       .group="Organs", .custom="Organ Full",   .a=0.030f,.d=0.05f,.s=0.98f,.r=0.22f, .vol=0.76f, .cat=C::Pad, .vib=true,.vibRate=5.5f,.vibDepth=0.11f },
    { .file="OrganBassPedal",  .name="Organ Bass Pedal",    .group="Organs", .custom="Organ Bass",   .a=0.010f,.d=0.05f,.s=0.98f,.r=0.10f, .vol=0.90f, .cat=C::Bass, .filt=F::LowPass,.cutoff=0.55f },

    // ================= Guitars =================
    { .file="NylonGuitar",     .name="Nylon Guitar",        .group="Guitars", .custom="Guitar Nylon", .a=0.003f,.d=1.10f,.s=0.00f,.r=0.28f, .vol=0.85f },
    { .file="SteelGuitar",     .name="Steel Guitar",        .group="Guitars", .custom="Guitar Steel", .a=0.002f,.d=1.20f,.s=0.00f,.r=0.30f, .vol=0.85f },
    { .file="CleanElectric",   .name="Clean Electric Guitar",.group="Guitars", .custom="Guitar Clean",.a=0.002f,.d=1.00f,.s=0.05f,.r=0.25f, .vol=0.85f, .filt=F::LowPass,.cutoff=0.88f },
    { .file="JazzGuitar",      .name="Jazz Guitar",         .group="Guitars", .custom="Guitar Jazz",  .a=0.004f,.d=0.90f,.s=0.04f,.r=0.25f, .vol=0.85f, .filt=F::LowPass,.cutoff=0.70f },
    { .file="MutedGuitar",     .name="Muted Guitar",        .group="Guitars", .custom="Guitar Clean", .a=0.002f,.d=0.16f,.s=0.00f,.r=0.06f, .vol=0.80f, .filt=F::HighPass,.cutoff=0.30f },
    // There's no waveshaper in the engine, so "overdrive" and "distortion"
    // are approximated the honest way: a harmonically dense analytic wave
    // (saw/square) held at high sustain, rolled off with the low-pass so it
    // doesn't shriek. It reads as dirty guitar without pretending to be
    // real amp modelling.
    { .file="OverdriveGuitar", .name="Overdrive Guitar",    .group="Guitars", .wave=W::Sawtooth,      .a=0.005f,.d=0.30f,.s=0.70f,.r=0.18f, .vol=0.80f, .vib=true,.vibRate=5.5f,.vibDepth=0.04f, .filt=F::LowPass,.cutoff=0.80f },
    { .file="DistortionGuitar",.name="Distortion Guitar",   .group="Guitars", .wave=W::Square,        .duty=0.50f, .a=0.003f,.d=0.25f,.s=0.78f,.r=0.16f, .vol=0.78f, .filt=F::LowPass,.cutoff=0.78f },
    // Arpeggiated octave = a fake power chord from a single note, the
    // classic chiptune trick applied to a guitar patch.
    { .file="PowerChordGuitar",.name="Power Chord Guitar",  .group="Guitars", .wave=W::Square,        .duty=0.50f, .a=0.003f,.d=0.30f,.s=0.75f,.r=0.18f, .vol=0.75f, .arp=Ar::Octave,.arpRate=22.0f, .filt=F::LowPass,.cutoff=0.74f },
    { .file="LeadGuitarSolo",  .name="Lead Guitar (Solo)",  .group="Guitars", .wave=W::Sawtooth,      .a=0.010f,.d=0.20f,.s=0.82f,.r=0.22f, .vol=0.80f, .vib=true,.vibRate=6.0f,.vibDepth=0.18f, .filt=F::LowPass,.cutoff=0.86f },
    { .file="Banjo",           .name="Banjo",               .group="Guitars", .custom="Banjo",        .a=0.001f,.d=0.55f,.s=0.00f,.r=0.12f, .vol=0.80f },
    { .file="Ukulele",         .name="Ukulele",             .group="Guitars", .custom="Ukulele",      .a=0.002f,.d=0.60f,.s=0.00f,.r=0.16f, .vol=0.80f },
    { .file="Sitar",           .name="Sitar",               .group="Guitars", .custom="Sitar",        .a=0.002f,.d=1.40f,.s=0.05f,.r=0.35f, .vol=0.78f },

    // ================= Basses =================
    { .file="AcousticBass",    .name="Acoustic Bass",       .group="Basses", .custom="Bass Upright", .a=0.004f,.d=0.80f,.s=0.10f,.r=0.20f, .vol=0.90f, .cat=C::Bass, .filt=F::LowPass,.cutoff=0.55f },
    { .file="FingerBass",      .name="Finger Bass",         .group="Basses", .custom="Bass Finger",  .a=0.003f,.d=0.90f,.s=0.18f,.r=0.18f, .vol=0.90f, .cat=C::Bass, .filt=F::LowPass,.cutoff=0.62f },
    { .file="PickedBass",      .name="Picked Bass",         .group="Basses", .custom="Bass Pick",    .a=0.002f,.d=0.80f,.s=0.16f,.r=0.16f, .vol=0.90f, .cat=C::Bass, .filt=F::LowPass,.cutoff=0.68f },
    { .file="SlapBass",        .name="Slap Bass",           .group="Basses", .custom="Bass Slap",    .a=0.001f,.d=0.45f,.s=0.08f,.r=0.12f, .vol=0.90f, .cat=C::Bass, .filt=F::LowPass,.cutoff=0.82f },
    { .file="FretlessBass",    .name="Fretless Bass",       .group="Basses", .custom="Bass Finger",  .a=0.010f,.d=1.00f,.s=0.25f,.r=0.25f, .vol=0.90f, .cat=C::Bass, .vib=true,.vibRate=5.0f,.vibDepth=0.12f, .filt=F::LowPass,.cutoff=0.55f },
    { .file="SynthBass",       .name="Synth Bass",          .group="Basses", .wave=W::Square,        .duty=0.50f, .a=0.002f,.d=0.30f,.s=0.55f,.r=0.10f, .vol=0.90f, .cat=C::Bass, .filt=F::LowPass,.cutoff=0.48f, .e2=E::FilterCutoff,.e2d=0.18f,.e2amt=0.30f },
    { .file="SubBass",         .name="Sub Bass",            .group="Basses", .wave=W::Sine,          .a=0.006f,.d=0.20f,.s=0.90f,.r=0.18f, .vol=0.95f, .cat=C::Bass },
    { .file="MetalBass",       .name="Electric Bass (Metal)",.group="Basses", .wave=W::Sawtooth,     .a=0.002f,.d=0.25f,.s=0.60f,.r=0.10f, .vol=0.90f, .cat=C::Bass, .filt=F::LowPass,.cutoff=0.60f },

    // ================= Brass =================
    // Brass needs a slowish attack (the player has to get the air moving)
    // and a high sustain -- it's a blown, held tone, not a struck one.
    { .file="Trumpet",         .name="Trumpet",             .group="Brass", .custom="Trumpet",      .a=0.045f,.d=0.18f,.s=0.82f,.r=0.14f, .vol=0.80f, .vib=true,.vibRate=5.5f,.vibDepth=0.05f },
    { .file="MutedTrumpet",    .name="Muted Trumpet",       .group="Brass", .custom="Trumpet Mute", .a=0.040f,.d=0.16f,.s=0.80f,.r=0.12f, .vol=0.75f, .filt=F::HighPass,.cutoff=0.45f },
    { .file="Trombone",        .name="Trombone",            .group="Brass", .custom="Trombone",     .a=0.060f,.d=0.20f,.s=0.84f,.r=0.16f, .vol=0.82f },
    { .file="Tuba",            .name="Tuba",                .group="Brass", .custom="Tuba",         .a=0.070f,.d=0.20f,.s=0.86f,.r=0.18f, .vol=0.88f, .cat=C::Bass },
    { .file="FrenchHorn",      .name="French Horn",         .group="Brass", .custom="Horn",         .a=0.090f,.d=0.22f,.s=0.82f,.r=0.22f, .vol=0.82f, .filt=F::LowPass,.cutoff=0.78f },
    { .file="BrassSection",    .name="Brass Section",       .group="Brass", .custom="Brass Section",.a=0.070f,.d=0.22f,.s=0.80f,.r=0.20f, .vol=0.80f, .cat=C::Pad },
    { .file="SynthBrass",      .name="Synth Brass",         .group="Brass", .wave=W::Sawtooth,      .a=0.040f,.d=0.20f,.s=0.78f,.r=0.16f, .vol=0.80f, .filt=F::LowPass,.cutoff=0.72f, .e2=E::FilterCutoff,.e2a=0.040f,.e2d=0.30f,.e2s=0.50f,.e2amt=0.25f },

    // ================= Reeds & winds =================
    { .file="AltoSax",         .name="Alto Sax",            .group="Reeds & Winds", .custom="Sax Alto",     .a=0.030f,.d=0.18f,.s=0.84f,.r=0.16f, .vol=0.80f, .vib=true,.vibRate=5.0f,.vibDepth=0.07f },
    { .file="TenorSax",        .name="Tenor Sax",           .group="Reeds & Winds", .custom="Sax Tenor",    .a=0.035f,.d=0.20f,.s=0.84f,.r=0.18f, .vol=0.82f, .vib=true,.vibRate=4.8f,.vibDepth=0.06f },
    { .file="Clarinet",        .name="Clarinet",            .group="Reeds & Winds", .custom="Clarinet",     .a=0.040f,.d=0.12f,.s=0.88f,.r=0.14f, .vol=0.80f },
    { .file="Oboe",            .name="Oboe",                .group="Reeds & Winds", .custom="Oboe",         .a=0.040f,.d=0.14f,.s=0.86f,.r=0.14f, .vol=0.76f, .vib=true,.vibRate=5.2f,.vibDepth=0.05f },
    { .file="Bassoon",         .name="Bassoon",             .group="Reeds & Winds", .custom="Bassoon",      .a=0.050f,.d=0.16f,.s=0.86f,.r=0.16f, .vol=0.80f },
    { .file="Flute",           .name="Flute",               .group="Reeds & Winds", .custom="Flute",        .a=0.070f,.d=0.10f,.s=0.90f,.r=0.12f, .vol=0.78f, .vib=true,.vibRate=5.0f,.vibDepth=0.06f },
    { .file="Piccolo",         .name="Piccolo",             .group="Reeds & Winds", .custom="Flute",        .a=0.050f,.d=0.08f,.s=0.90f,.r=0.10f, .vol=0.70f, .filt=F::HighPass,.cutoff=0.55f },
    { .file="PanFlute",        .name="Pan Flute",           .group="Reeds & Winds", .custom="Pan Flute",    .a=0.060f,.d=0.12f,.s=0.86f,.r=0.16f, .vol=0.78f, .vib=true,.vibRate=4.5f,.vibDepth=0.05f },
    { .file="Recorder",        .name="Recorder",            .group="Reeds & Winds", .custom="Recorder",     .a=0.050f,.d=0.10f,.s=0.88f,.r=0.12f, .vol=0.76f },
    { .file="Harmonica",       .name="Harmonica",           .group="Reeds & Winds", .custom="Harmonica",    .a=0.020f,.d=0.12f,.s=0.88f,.r=0.12f, .vol=0.75f, .vib=true,.vibRate=6.0f,.vibDepth=0.06f },
    { .file="Accordion",       .name="Accordion",           .group="Reeds & Winds", .custom="Accordion",    .a=0.030f,.d=0.10f,.s=0.92f,.r=0.12f, .vol=0.78f, .vib=true,.vibRate=5.5f,.vibDepth=0.04f },

    // ================= Bowed strings =================
    { .file="Violin",          .name="Violin",              .group="Strings", .custom="Violin",       .a=0.100f,.d=0.20f,.s=0.82f,.r=0.25f, .vol=0.78f, .vib=true,.vibRate=5.5f,.vibDepth=0.10f },
    { .file="Viola",           .name="Viola",               .group="Strings", .custom="Viola",        .a=0.110f,.d=0.22f,.s=0.82f,.r=0.26f, .vol=0.80f, .vib=true,.vibRate=5.2f,.vibDepth=0.09f },
    { .file="Cello",           .name="Cello",               .group="Strings", .custom="Cello",        .a=0.120f,.d=0.24f,.s=0.84f,.r=0.30f, .vol=0.82f, .vib=true,.vibRate=4.8f,.vibDepth=0.08f },
    { .file="DoubleBassArco",  .name="Double Bass (Arco)",  .group="Strings", .custom="Cello",        .a=0.130f,.d=0.25f,.s=0.85f,.r=0.30f, .vol=0.86f, .cat=C::Bass, .vib=true,.vibRate=4.5f,.vibDepth=0.06f, .filt=F::LowPass,.cutoff=0.50f },
    { .file="StringEnsemble",  .name="String Ensemble",     .group="Strings", .custom="Strings",      .a=0.220f,.d=0.30f,.s=0.85f,.r=0.45f, .vol=0.78f, .cat=C::Pad, .vib=true,.vibRate=4.5f,.vibDepth=0.05f },
    { .file="PizzicatoStrings",.name="Pizzicato Strings",   .group="Strings", .custom="Harp",         .a=0.002f,.d=0.38f,.s=0.00f,.r=0.12f, .vol=0.80f },
    { .file="Harp",            .name="Harp",                .group="Strings", .custom="Harp",         .a=0.002f,.d=1.40f,.s=0.00f,.r=0.40f, .vol=0.82f },

    // ================= Mallets & bells =================
    { .file="Marimba",         .name="Marimba",             .group="Mallets & Bells", .custom="Marimba",      .a=0.001f,.d=0.55f,.s=0.00f,.r=0.14f, .vol=0.85f, .cat=C::Percussion },
    { .file="Xylophone",       .name="Xylophone",           .group="Mallets & Bells", .custom="Xylophone",    .a=0.001f,.d=0.30f,.s=0.00f,.r=0.10f, .vol=0.82f, .cat=C::Percussion },
    { .file="Vibraphone",      .name="Vibraphone",          .group="Mallets & Bells", .custom="Vibraphone",   .a=0.002f,.d=1.60f,.s=0.00f,.r=0.45f, .vol=0.82f, .cat=C::Percussion, .vib=true,.vibRate=5.0f,.vibDepth=0.04f },
    { .file="Glockenspiel",    .name="Glockenspiel",        .group="Mallets & Bells", .custom="Glockenspiel", .a=0.001f,.d=1.10f,.s=0.00f,.r=0.35f, .vol=0.75f, .cat=C::Percussion },
    { .file="TubularBells",    .name="Tubular Bells",       .group="Mallets & Bells", .custom="Tubular Bell", .a=0.003f,.d=2.40f,.s=0.00f,.r=0.80f, .vol=0.80f, .cat=C::Percussion },
    { .file="SteelDrum",       .name="Steel Drum",          .group="Mallets & Bells", .custom="Steel Drum",   .a=0.002f,.d=0.70f,.s=0.05f,.r=0.20f, .vol=0.85f, .cat=C::Percussion },
    { .file="Kalimba",         .name="Kalimba",             .group="Mallets & Bells", .custom="Kalimba",      .a=0.001f,.d=0.60f,.s=0.00f,.r=0.18f, .vol=0.80f, .cat=C::Percussion },
    // A drum with a definite pitch: a sine with a fast downward pitch
    // envelope, which is how every drum machine has faked a struck head
    // since the CR-78.
    { .file="Timpani",         .name="Timpani",             .group="Mallets & Bells", .wave=W::Sine,          .a=0.002f,.d=0.90f,.s=0.00f,.r=0.30f, .vol=0.95f, .cat=C::Percussion, .e2=E::Pitch,.e2d=0.070f,.e2r=0.05f,.e2amt=7.0f },

    // ================= Voices & pads =================
    { .file="ChoirAahs",       .name="Choir Aahs",          .group="Voices & Pads", .custom="Choir",        .a=0.250f,.d=0.30f,.s=0.88f,.r=0.50f, .vol=0.78f, .cat=C::Pad },
    { .file="VoiceOohs",       .name="Voice Oohs",          .group="Voices & Pads", .custom="Voice Ooh",    .a=0.180f,.d=0.25f,.s=0.88f,.r=0.40f, .vol=0.78f, .cat=C::Pad },
    { .file="SynthVoice",      .name="Synth Voice",         .group="Voices & Pads", .custom="Voice Ah",     .a=0.100f,.d=0.20f,.s=0.85f,.r=0.30f, .vol=0.78f, .cat=C::Pad, .vib=true,.vibRate=5.0f,.vibDepth=0.05f },
    { .file="WarmPad",         .name="Warm Pad",            .group="Voices & Pads", .custom="Warm Pad",     .a=0.450f,.d=0.40f,.s=0.85f,.r=0.80f, .vol=0.72f, .cat=C::Pad, .filt=F::LowPass,.cutoff=0.70f },
    { .file="GlassPad",        .name="Glass Pad",           .group="Voices & Pads", .custom="Glass Pad",    .a=0.350f,.d=0.40f,.s=0.82f,.r=0.70f, .vol=0.70f, .cat=C::Pad },
    { .file="BellPad",         .name="Bell Pad",            .group="Voices & Pads", .custom="Bell",         .a=0.010f,.d=2.00f,.s=0.20f,.r=0.90f, .vol=0.72f, .cat=C::Pad },

    // ================= Synth leads =================
    { .file="SquareLead",      .name="Square Lead",         .group="Synth Leads", .wave=W::Square,        .duty=0.500f, .a=0.005f,.d=0.10f,.s=0.85f,.r=0.10f, .vol=0.80f },
    { .file="PulseLead",       .name="Pulse Lead 12.5%",    .group="Synth Leads", .wave=W::Square,        .duty=0.125f, .a=0.004f,.d=0.10f,.s=0.82f,.r=0.10f, .vol=0.78f },
    { .file="SawLead",         .name="Saw Lead",            .group="Synth Leads", .wave=W::Sawtooth,      .a=0.005f,.d=0.12f,.s=0.84f,.r=0.12f, .vol=0.78f, .filt=F::LowPass,.cutoff=0.85f },
    { .file="SineLead",        .name="Sine Lead",           .group="Synth Leads", .wave=W::Sine,          .a=0.010f,.d=0.10f,.s=0.90f,.r=0.14f, .vol=0.85f },
    { .file="TriangleLead",    .name="Triangle Lead",       .group="Synth Leads", .wave=W::Triangle,      .a=0.006f,.d=0.10f,.s=0.88f,.r=0.12f, .vol=0.85f },
    { .file="ChipArpMajor",    .name="Chip Arp Lead",       .group="Synth Leads", .wave=W::Square,        .duty=0.250f, .a=0.002f,.d=0.08f,.s=0.85f,.r=0.06f, .vol=0.78f, .arp=Ar::MajorChord,.arpRate=18.0f },
    { .file="ChipArpMinor",    .name="Chip Arp (Minor)",    .group="Synth Leads", .wave=W::Square,        .duty=0.250f, .a=0.002f,.d=0.08f,.s=0.85f,.r=0.06f, .vol=0.78f, .arp=Ar::MinorChord,.arpRate=18.0f },
    // Resonance isn't available on a one-pole filter, but a nearly-closed
    // cutoff plus a strong fast envelope sweep still gets the "wow" of an
    // acid line.
    { .file="AcidLead",        .name="Acid Lead",           .group="Synth Leads", .wave=W::Sawtooth,      .a=0.002f,.d=0.18f,.s=0.40f,.r=0.10f, .vol=0.78f, .filt=F::LowPass,.cutoff=0.35f, .e2=E::FilterCutoff,.e2d=0.25f,.e2r=0.12f,.e2amt=0.55f },

    // ================= Drum kit =================
    // Everything here is a one-shot: sustain 0, short decay, and for the
    // pitched pieces a big positive pitch envelope over a few tens of
    // milliseconds -- that downward "thump" sweep is the entire trick.
    { .file="KickDrum",        .name="Kick Drum",           .group="Drum Kit", .wave=W::Sine,          .a=0.001f,.d=0.16f,.s=0.00f,.r=0.04f, .vol=0.95f, .cat=C::Percussion, .e2=E::Pitch,.e2d=0.045f,.e2r=0.03f,.e2amt=30.0f },
    { .file="KickDeep",        .name="Kick (Deep)",         .group="Drum Kit", .wave=W::Sine,          .a=0.001f,.d=0.28f,.s=0.00f,.r=0.06f, .vol=0.95f, .cat=C::Percussion, .filt=F::LowPass,.cutoff=0.45f, .e2=E::Pitch,.e2d=0.060f,.e2r=0.03f,.e2amt=24.0f },
    { .file="SnareDrum",       .name="Snare Drum",          .group="Drum Kit", .wave=W::Noise,         .a=0.001f,.d=0.16f,.s=0.00f,.r=0.05f, .vol=0.80f, .cat=C::Percussion, .filt=F::HighPass,.cutoff=0.40f },
    { .file="SnareTight",      .name="Snare (Tight)",       .group="Drum Kit", .wave=W::Noise,         .a=0.001f,.d=0.09f,.s=0.00f,.r=0.03f, .vol=0.80f, .cat=C::Percussion, .filt=F::HighPass,.cutoff=0.52f },
    { .file="Rimshot",         .name="Rimshot",             .group="Drum Kit", .wave=W::Noise,         .a=0.001f,.d=0.05f,.s=0.00f,.r=0.02f, .vol=0.75f, .cat=C::Percussion, .filt=F::HighPass,.cutoff=0.62f },
    { .file="HandClap",        .name="Hand Clap",           .group="Drum Kit", .wave=W::Noise,         .a=0.002f,.d=0.13f,.s=0.00f,.r=0.05f, .vol=0.78f, .cat=C::Percussion, .filt=F::HighPass,.cutoff=0.48f },
    { .file="HiHatClosed",     .name="Hi-Hat (Closed)",     .group="Drum Kit", .wave=W::Noise,         .a=0.001f,.d=0.04f,.s=0.00f,.r=0.02f, .vol=0.60f, .cat=C::Percussion, .filt=F::HighPass,.cutoff=0.80f },
    { .file="HiHatOpen",       .name="Hi-Hat (Open)",       .group="Drum Kit", .wave=W::Noise,         .a=0.001f,.d=0.30f,.s=0.00f,.r=0.12f, .vol=0.60f, .cat=C::Percussion, .filt=F::HighPass,.cutoff=0.78f },
    { .file="CrashCymbal",     .name="Crash Cymbal",        .group="Drum Kit", .wave=W::Noise,         .a=0.002f,.d=1.30f,.s=0.00f,.r=0.60f, .vol=0.65f, .cat=C::Percussion, .filt=F::HighPass,.cutoff=0.70f },
    { .file="RideCymbal",      .name="Ride Cymbal",         .group="Drum Kit", .wave=W::Noise,         .a=0.001f,.d=0.70f,.s=0.00f,.r=0.30f, .vol=0.60f, .cat=C::Percussion, .filt=F::HighPass,.cutoff=0.84f },
    { .file="TomHigh",         .name="Tom (High)",          .group="Drum Kit", .wave=W::Sine,          .a=0.001f,.d=0.24f,.s=0.00f,.r=0.08f, .vol=0.85f, .cat=C::Percussion, .e2=E::Pitch,.e2d=0.080f,.e2r=0.04f,.e2amt=12.0f },
    { .file="TomLow",          .name="Tom (Low)",           .group="Drum Kit", .wave=W::Sine,          .a=0.001f,.d=0.34f,.s=0.00f,.r=0.10f, .vol=0.88f, .cat=C::Percussion, .filt=F::LowPass,.cutoff=0.55f, .e2=E::Pitch,.e2d=0.100f,.e2r=0.04f,.e2amt=10.0f },
    { .file="Cowbell",         .name="Cowbell",             .group="Drum Kit", .wave=W::Square,        .duty=0.500f, .a=0.001f,.d=0.12f,.s=0.00f,.r=0.04f, .vol=0.70f, .cat=C::Percussion, .filt=F::HighPass,.cutoff=0.55f },
    { .file="Woodblock",       .name="Woodblock",           .group="Drum Kit", .wave=W::Square,        .duty=0.180f, .a=0.001f,.d=0.06f,.s=0.00f,.r=0.02f, .vol=0.70f, .cat=C::Percussion, .filt=F::HighPass,.cutoff=0.60f },
    { .file="Shaker",          .name="Shaker",              .group="Drum Kit", .wave=W::Noise,         .a=0.003f,.d=0.06f,.s=0.00f,.r=0.03f, .vol=0.55f, .cat=C::Percussion, .filt=F::HighPass,.cutoff=0.90f },
    { .file="Tambourine",      .name="Tambourine",          .group="Drum Kit", .wave=W::Noise,         .a=0.001f,.d=0.18f,.s=0.00f,.r=0.08f, .vol=0.60f, .cat=C::Percussion, .filt=F::HighPass,.cutoff=0.86f },

    // ================= FX =================
    { .file="FXNoiseSweep",    .name="Noise Sweep (FX)",    .group="FX", .wave=W::Noise,         .a=0.300f,.d=0.40f,.s=0.00f,.r=0.20f, .vol=0.60f, .cat=C::FX, .filt=F::HighPass,.cutoff=0.50f },
    { .file="FXLaserZap",      .name="Laser Zap (FX)",      .group="FX", .wave=W::Square,        .duty=0.250f, .a=0.001f,.d=0.22f,.s=0.00f,.r=0.05f, .vol=0.70f, .cat=C::FX, .e2=E::Pitch,.e2d=0.200f,.e2r=0.05f,.e2amt=-24.0f },
    { .file="FXPowerUp",       .name="Power Up (FX)",       .group="FX", .wave=W::Square,        .duty=0.500f, .a=0.001f,.d=0.30f,.s=0.00f,.r=0.05f, .vol=0.70f, .cat=C::FX, .e2=E::Pitch,.e2a=0.280f,.e2d=0.05f,.e2s=1.0f,.e2r=0.05f,.e2amt=24.0f },
    { .file="FXExplosion",     .name="Explosion (FX)",      .group="FX", .wave=W::Noise,         .a=0.001f,.d=0.80f,.s=0.00f,.r=0.30f, .vol=0.80f, .cat=C::FX, .filt=F::LowPass,.cutoff=0.40f },
    { .file="FXSiren",         .name="Siren (FX)",          .group="FX", .wave=W::Sawtooth,      .a=0.010f,.d=0.10f,.s=0.90f,.r=0.10f, .vol=0.70f, .cat=C::FX, .vib=true,.vibRate=1.2f,.vibDepth=6.0f },
};

Instrument ToInstrument(const Patch& p)
{
    Instrument inst;
    inst.name             = p.name;
    inst.waveform         = p.wave;
    inst.dutyCycle        = p.duty;
    inst.customWaveform   = p.custom;
    inst.envelope.attack  = p.a;
    inst.envelope.decay   = p.d;
    inst.envelope.sustain = p.s;
    inst.envelope.release = p.r;
    inst.volume           = p.vol;
    inst.pan              = 0.0f;
    inst.muted            = false;
    inst.solo             = false;
    inst.category         = p.cat;
    inst.arpPattern       = p.arp;
    inst.arpRate          = p.arpRate;
    inst.vibratoEnabled   = p.vib;
    inst.vibratoRate      = p.vibRate;
    inst.vibratoDepth     = p.vibDepth;
    inst.filterType       = p.filt;
    inst.filterCutoff     = p.cutoff;
    inst.env2Target       = p.e2;
    inst.env2.attack      = p.e2a;
    inst.env2.decay       = p.e2d;
    inst.env2.sustain     = p.e2s;
    inst.env2.release     = p.e2r;
    inst.env2Amount       = p.e2amt;
    return inst;
}

bool FileExists(const std::string& path)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

} // namespace

int DefaultInstrumentCount()
{
    return (int)(sizeof(kPatches) / sizeof(kPatches[0]));
}

DefaultInstrumentInfo DefaultInstrumentAt(int index)
{
    DefaultInstrumentInfo info{};
    if (index < 0 || index >= DefaultInstrumentCount()) return info;

    const Patch& p = kPatches[index];
    info.file  = p.file;
    info.name  = p.name;
    info.group = p.group;
    return info;
}

bool WriteDefaultInstrument(const std::string& fileStem, bool overwrite)
{
    for (const Patch& p : kPatches)
    {
        if (fileStem != p.file) continue;

        const std::string path = AppPaths::InstrumentsDir() + "/" + p.file + ".ssip";
        if (!overwrite && FileExists(path)) return true; // already there is a success, not a failure
        return SaveInstrumentPreset(ToInstrument(p), path);
    }
    return false; // no bundled preset by that name
}

void WriteDefaultInstrumentsIfMissing()
{
    for (const Patch& p : kPatches)
        WriteDefaultInstrument(p.file, /*overwrite=*/false);
}
