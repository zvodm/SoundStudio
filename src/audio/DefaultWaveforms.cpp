#include "audio/DefaultWaveforms.h"
#include "audio/Synth.h"
#include <cmath>
#include <initializer_list>
#include <string>
#include <vector>

namespace
{

constexpr int   TABLE_SIZE = 1024;
constexpr float TWO_PI = 6.28318530718f;

struct Partial
{
    int   n;             // harmonic number, 1 = fundamental
    float amp;           // relative amplitude
    float phase = 0.0f;  // 0..1 of a cycle -- only changes the shape's crest factor, not the timbre
};

// Peak-normalizes to +/-1 so every table hits the mixer at a comparable
// level regardless of how many partials went into it (a 20-partial string
// pad sums much higher than a 3-partial flute before this).
void Normalize(std::vector<float>& table)
{
    float peak = 0.0f;
    for (float s : table) peak = std::fmax(peak, std::fabs(s));
    if (peak <= 1e-6f) return;
    const float scale = 1.0f / peak;
    for (float& s : table) s *= scale;
}

std::vector<float> Additive(std::initializer_list<Partial> partials)
{
    std::vector<float> table((size_t)TABLE_SIZE, 0.0f);
    for (int i = 0; i < TABLE_SIZE; ++i)
    {
        const float t = (float)i / (float)TABLE_SIZE;
        float sum = 0.0f;
        for (const Partial& p : partials)
            sum += p.amp * std::sin(TWO_PI * ((float)p.n * t + p.phase));
        table[(size_t)i] = sum;
    }
    Normalize(table);
    return table;
}

// A plucked/struck string's spectrum: a string excited at a fraction
// `pluckPos` along its length has essentially no energy in the harmonics
// whose nodes sit at that point, giving |sin(n*pi*pluckPos)| -- the
// classic Karplus-strong-ish comb that makes a guitar sound like a guitar
// rather than a saw. `rolloff` sets how fast the top end dies off (higher
// = darker/mellower), `count` how far up we bother going.
std::vector<float> Plucked(float pluckPos, float rolloff, int count)
{
    std::vector<float> table((size_t)TABLE_SIZE, 0.0f);
    std::vector<float> amps((size_t)count + 1, 0.0f);
    for (int n = 1; n <= count; ++n)
        amps[(size_t)n] = std::fabs(std::sin((float)n * 3.14159265f * pluckPos)) / std::pow((float)n, rolloff);

    for (int i = 0; i < TABLE_SIZE; ++i)
    {
        const float t = (float)i / (float)TABLE_SIZE;
        float sum = 0.0f;
        for (int n = 1; n <= count; ++n)
            sum += amps[(size_t)n] * std::sin(TWO_PI * (float)n * t);
        table[(size_t)i] = sum;
    }
    Normalize(table);
    return table;
}

// A bowed/blown sustained tone: a full harmonic series rolled off at
// `rolloff`, optionally with only the odd harmonics kept (which is what
// makes a clarinet a clarinet and not an oboe).
std::vector<float> Series(float rolloff, int count, bool oddOnly = false, float tilt = 0.0f)
{
    std::vector<float> table((size_t)TABLE_SIZE, 0.0f);
    for (int i = 0; i < TABLE_SIZE; ++i)
    {
        const float t = (float)i / (float)TABLE_SIZE;
        float sum = 0.0f;
        for (int n = 1; n <= count; ++n)
        {
            if (oddOnly && (n % 2) == 0) continue;
            // `tilt` gently pushes energy off the fundamental and into the
            // low-mid harmonics -- brass and reeds are both much stronger
            // around the 2nd/3rd partial than at the root.
            float amp = 1.0f / std::pow((float)n, rolloff);
            if (tilt > 0.0f) amp *= 1.0f + tilt * std::exp(-std::pow((float)(n - 3), 2.0f) * 0.12f);
            sum += amp * std::sin(TWO_PI * (float)n * t);
        }
        table[(size_t)i] = sum;
    }
    Normalize(table);
    return table;
}

void Reg(const char* name, std::vector<float> table)
{
    RegisterCustomWaveform(name, std::move(table));
}

} // namespace

void RegisterBuiltinWaveforms()
{
    // ---------------- Pianos & struck keys ----------------
    // Piano partials fall off roughly as 1/n^1.5 with a dip around the 7th
    // (the hammer strikes near 1/7 of the string, killing that harmonic --
    // that dip is a big part of why a piano reads as a piano).
    Reg("Piano",        Additive({ {1,1.00f}, {2,0.44f}, {3,0.26f}, {4,0.17f}, {5,0.10f},
                                   {6,0.07f}, {7,0.02f}, {8,0.04f}, {9,0.025f}, {10,0.018f},
                                   {12,0.010f}, {14,0.006f} }));
    Reg("Piano Bright", Additive({ {1,1.00f}, {2,0.60f}, {3,0.42f}, {4,0.30f}, {5,0.22f},
                                   {6,0.16f}, {7,0.05f}, {8,0.10f}, {9,0.07f}, {10,0.05f},
                                   {12,0.035f}, {16,0.020f} }));
    // Rhodes/Wurlitzer: nearly a sine, plus a bell-like 4th and a faint
    // high tine ring. The tine is what you hear on the attack.
    Reg("EPiano",       Additive({ {1,1.00f}, {2,0.06f}, {3,0.04f}, {4,0.20f}, {6,0.09f},
                                   {9,0.04f}, {14,0.05f}, {18,0.02f} }));
    Reg("Clavinet",     Plucked(0.08f, 0.85f, 40));
    Reg("Harpsichord",  Plucked(0.12f, 0.70f, 48));
    Reg("Music Box",    Additive({ {1,1.00f}, {3,0.30f}, {5,0.16f}, {8,0.10f}, {11,0.06f}, {15,0.03f} }));
    Reg("Celesta",      Additive({ {1,1.00f}, {4,0.22f}, {8,0.10f}, {12,0.04f} }));

    // ---------------- Organs ----------------
    // Drawbar organ: a small stack of pure sines at octave/fifth
    // relationships, no envelope shaping at all -- the harmonics ARE the
    // instrument. 1-2-3-4-6-8 is a fair stand-in for a fat 888800000-ish
    // registration using only integer partials (so the pitch stays right).
    Reg("Organ",        Additive({ {1,1.00f}, {2,0.72f}, {3,0.50f}, {4,0.46f}, {6,0.30f}, {8,0.24f} }));
    // Rock/percussive drawbar: more upper-octave bite, less fifth.
    Reg("Organ Rock",   Additive({ {1,1.00f}, {2,0.85f}, {3,0.35f}, {4,0.55f}, {6,0.20f}, {8,0.45f}, {12,0.18f} }));
    // Pipe organ principal chorus + mixture ranks -- lots of quiet high
    // partials at octaves and fifths, which is where the "church" comes from.
    Reg("Organ Church", Additive({ {1,1.00f}, {2,0.55f}, {3,0.34f}, {4,0.30f}, {5,0.10f}, {6,0.20f},
                                   {8,0.18f}, {10,0.07f}, {12,0.11f}, {16,0.09f}, {20,0.04f}, {24,0.03f} }));
    // Full organ, every rank pulled: same shape, brighter and denser.
    Reg("Organ Full",   Additive({ {1,1.00f}, {2,0.80f}, {3,0.55f}, {4,0.50f}, {5,0.22f}, {6,0.38f},
                                   {8,0.34f}, {10,0.16f}, {12,0.22f}, {16,0.18f}, {20,0.10f},
                                   {24,0.08f}, {32,0.05f} }));
    // Reed organ / harmonium: buzzy free reed, odd-heavy, squarish.
    Reg("Organ Reed",   Series(0.95f, 24, /*oddOnly=*/true, /*tilt=*/0.5f));
    Reg("Organ Bass",   Additive({ {1,1.00f}, {2,0.45f}, {3,0.18f}, {4,0.20f}, {6,0.08f} }));

    // ---------------- Guitars ----------------
    Reg("Guitar Nylon", Plucked(0.36f, 1.25f, 28));
    Reg("Guitar Steel", Plucked(0.22f, 0.95f, 40));
    Reg("Guitar Clean", Plucked(0.14f, 0.90f, 40));
    Reg("Guitar Jazz",  Plucked(0.42f, 1.45f, 24));
    Reg("Banjo",        Plucked(0.09f, 0.62f, 48));
    Reg("Ukulele",      Plucked(0.30f, 1.05f, 32));
    Reg("Sitar",        Plucked(0.07f, 0.55f, 56));

    // ---------------- Basses ----------------
    Reg("Bass Finger",  Plucked(0.26f, 1.30f, 28));
    Reg("Bass Pick",    Plucked(0.16f, 1.00f, 36));
    Reg("Bass Slap",    Plucked(0.10f, 0.72f, 44));
    Reg("Bass Upright", Plucked(0.40f, 1.60f, 20));

    // ---------------- Brass ----------------
    // Brass spectra peak around the 2nd-4th partial rather than the
    // fundamental, which is the `tilt` argument's whole reason for existing.
    Reg("Trumpet",      Additive({ {1,0.62f}, {2,1.00f}, {3,0.88f}, {4,0.72f}, {5,0.56f}, {6,0.42f},
                                   {7,0.31f}, {8,0.23f}, {9,0.16f}, {10,0.11f}, {12,0.06f}, {14,0.03f} }));
    Reg("Trumpet Mute", Additive({ {1,0.30f}, {2,0.70f}, {3,1.00f}, {4,0.85f}, {5,0.70f}, {6,0.55f},
                                   {7,0.40f}, {8,0.28f}, {9,0.18f}, {10,0.12f} }));
    Reg("Trombone",     Additive({ {1,0.85f}, {2,1.00f}, {3,0.74f}, {4,0.52f}, {5,0.36f}, {6,0.24f},
                                   {7,0.16f}, {8,0.10f}, {9,0.06f} }));
    Reg("Horn",         Additive({ {1,1.00f}, {2,0.62f}, {3,0.36f}, {4,0.20f}, {5,0.12f}, {6,0.07f}, {7,0.04f} }));
    Reg("Tuba",         Additive({ {1,1.00f}, {2,0.48f}, {3,0.22f}, {4,0.11f}, {5,0.06f}, {6,0.03f} }));
    Reg("Brass Section",Additive({ {1,0.75f}, {2,1.00f}, {3,0.82f}, {4,0.64f}, {5,0.48f}, {6,0.36f},
                                   {7,0.26f}, {8,0.19f}, {9,0.13f}, {10,0.09f}, {11,0.06f}, {12,0.04f} }));

    // ---------------- Reeds & winds ----------------
    Reg("Sax Alto",     Additive({ {1,1.00f}, {2,0.52f}, {3,0.66f}, {4,0.34f}, {5,0.40f}, {6,0.20f},
                                   {7,0.22f}, {8,0.11f}, {9,0.10f}, {10,0.05f} }));
    Reg("Sax Tenor",    Additive({ {1,1.00f}, {2,0.40f}, {3,0.55f}, {4,0.24f}, {5,0.28f}, {6,0.13f},
                                   {7,0.13f}, {8,0.06f} }));
    // Cylindrical bore, closed at one end: odd harmonics only.
    Reg("Clarinet",     Additive({ {1,1.00f}, {3,0.60f}, {5,0.36f}, {7,0.21f}, {9,0.12f}, {11,0.06f}, {13,0.03f} }));
    Reg("Oboe",         Additive({ {1,0.48f}, {2,1.00f}, {3,0.82f}, {4,0.55f}, {5,0.44f}, {6,0.28f},
                                   {7,0.22f}, {8,0.13f}, {9,0.09f} }));
    Reg("Bassoon",      Additive({ {1,0.55f}, {2,1.00f}, {3,0.60f}, {4,0.30f}, {5,0.18f}, {6,0.10f}, {7,0.05f} }));
    // Nearly a pure sine with a touch of second -- a flute really is that simple.
    Reg("Flute",        Additive({ {1,1.00f}, {2,0.16f}, {3,0.06f}, {4,0.03f} }));
    Reg("Pan Flute",    Additive({ {1,1.00f}, {2,0.30f}, {3,0.12f}, {4,0.06f}, {5,0.03f} }));
    Reg("Recorder",     Additive({ {1,1.00f}, {2,0.22f}, {3,0.14f}, {4,0.05f} }));
    Reg("Harmonica",    Series(1.05f, 20, /*oddOnly=*/false, /*tilt=*/0.45f));
    Reg("Accordion",    Additive({ {1,1.00f}, {2,0.58f}, {3,0.48f}, {4,0.32f}, {5,0.26f}, {6,0.19f},
                                   {7,0.14f}, {8,0.10f}, {9,0.07f} }));

    // ---------------- Bowed strings ----------------
    // A bowed string is close to a sawtooth (Helmholtz motion), just with
    // the very top rolled off by the body and the bow's contact width.
    Reg("Violin",       Series(1.05f, 28));
    Reg("Viola",        Series(1.20f, 24));
    Reg("Cello",        Series(1.30f, 22));
    Reg("Strings",      Series(1.45f, 20));
    Reg("Harp",         Plucked(0.28f, 1.35f, 26));

    // ---------------- Mallets & bells ----------------
    // Bars and bells are genuinely inharmonic; with a single-cycle table the
    // honest approximation is to keep only the partials nearest the real
    // ratios (marimba ~4:1, vibraphone ~4:1 + 10:1, bell dense and high).
    Reg("Marimba",      Additive({ {1,1.00f}, {4,0.35f}, {10,0.08f} }));
    Reg("Xylophone",    Additive({ {1,1.00f}, {3,0.45f}, {6,0.20f}, {9,0.08f} }));
    Reg("Vibraphone",   Additive({ {1,1.00f}, {4,0.28f}, {8,0.10f}, {10,0.06f} }));
    Reg("Glockenspiel", Additive({ {1,1.00f}, {3,0.28f}, {6,0.22f}, {10,0.12f}, {14,0.06f} }));
    Reg("Tubular Bell", Additive({ {2,1.00f}, {3,0.72f}, {4,0.50f}, {6,0.34f}, {8,0.22f}, {11,0.12f} }));
    Reg("Bell",         Additive({ {1,0.70f}, {2,1.00f}, {3,0.45f}, {5,0.40f}, {7,0.25f}, {9,0.18f}, {13,0.10f} }));
    Reg("Steel Drum",   Additive({ {1,1.00f}, {2,0.55f}, {4,0.40f}, {6,0.18f}, {8,0.12f} }));
    Reg("Kalimba",      Additive({ {1,1.00f}, {2,0.18f}, {5,0.12f}, {9,0.05f} }));

    // ---------------- Voices & pads ----------------
    // Crude fixed formants: bumps of harmonics standing in for the vocal
    // tract's resonances. Not a vocoder, but it reads as "voice".
    Reg("Voice Ah",     Additive({ {1,1.00f}, {2,0.70f}, {3,0.52f}, {4,0.60f}, {5,0.42f}, {6,0.30f},
                                   {7,0.34f}, {8,0.22f}, {9,0.14f}, {10,0.16f}, {12,0.08f} }));
    Reg("Voice Ooh",    Additive({ {1,1.00f}, {2,0.62f}, {3,0.28f}, {4,0.12f}, {5,0.06f}, {6,0.03f} }));
    Reg("Choir",        Additive({ {1,1.00f}, {2,0.66f}, {3,0.44f}, {4,0.40f}, {5,0.28f}, {6,0.22f},
                                   {7,0.16f}, {8,0.14f}, {9,0.08f}, {10,0.06f} }));
    Reg("Warm Pad",     Series(1.70f, 18));
    Reg("Glass Pad",    Additive({ {1,1.00f}, {2,0.35f}, {3,0.20f}, {4,0.28f}, {6,0.14f}, {8,0.16f}, {12,0.07f} }));
}
