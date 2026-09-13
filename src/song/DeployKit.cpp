#include "song/DeployKit.h"
#include "song/SongIO.h"
#include "song/BinaryIO.h"
#include "audio/Synth.h"   // FindCustomWaveform -- the live wavetable registry
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>

#ifdef _WIN32
#include <direct.h>
#define SS_MKDIR(path) _mkdir(path)
#else
#include <sys/stat.h>
#define SS_MKDIR(path) mkdir(path, 0755)
#endif

namespace
{

constexpr char     BUNDLE_MAGIC[4] = { 'S', 'S', 'B', 'N' };
constexpr uint32_t BUNDLE_VERSION  = 1;

// Creates every missing directory along the path. Ignores failures the same
// way AppPaths::EnsureDirectories does -- EEXIST is the normal case, and the
// file writes below report anything genuinely wrong.
void EnsureDir(const std::string& path)
{
    std::string partial;
    partial.reserve(path.size());

    for (size_t i = 0; i < path.size(); ++i)
    {
        const char c = path[i];
        partial.push_back(c);
        if ((c == '/' || c == '\\') && i > 0) SS_MKDIR(partial.c_str());
    }
    if (!partial.empty()) SS_MKDIR(partial.c_str());
}

// Filenames get whatever the song is called, minus anything Windows or a
// shell would object to.
std::string SanitizeFileName(const std::string& name)
{
    std::string out;
    for (char c : name)
    {
        const bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                          (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
        out.push_back(safe ? c : '_');
    }
    while (!out.empty() && (out.front() == '.' || out.front() == '_')) out.erase(out.begin());
    if (out.empty()) out = "song";
    return out;
}

// ...and C identifiers get a stricter version of the same treatment.
std::string SanitizeIdentifier(const std::string& name)
{
    std::string out;
    for (char c : name)
    {
        const bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                          (c >= '0' && c <= '9') || c == '_';
        out.push_back(safe ? c : '_');
    }
    if (out.empty() || (out[0] >= '0' && out[0] <= '9')) out.insert(out.begin(), '_');
    return out;
}

bool ReadWholeFile(const std::string& path, std::vector<uint8_t>& out)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return false; }
    const long length = ftell(f);
    if (length <= 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return false; }

    out.resize((size_t)length);
    const size_t read = fread(out.data(), 1, out.size(), f);
    fclose(f);
    return read == out.size();
}

bool WriteWholeFile(const std::string& path, const std::vector<uint8_t>& bytes)
{
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = bytes.empty() || fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    fclose(f);
    return ok;
}

bool CopyFileTo(const std::string& from, const std::string& to)
{
    std::vector<uint8_t> bytes;
    if (!ReadWholeFile(from, bytes)) return false;
    return WriteWholeFile(to, bytes);
}

bool FileExists(const std::string& path)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

std::string JoinPath(const std::string& dir, const std::string& leaf)
{
    if (dir.empty()) return leaf;
    const char last = dir[dir.size() - 1];
    if (last == '/' || last == '\\') return dir + leaf;
    return dir + "/" + leaf;
}

// The wavetables this particular song actually needs, resolved against the
// live registry. Deduplicated, and in the order instruments first mention
// them, so a bundle is reproducible rather than hash-order.
struct CollectedWave
{
    std::string name;
    std::shared_ptr<const std::vector<float>> samples;
};

void CollectWaves(const Song& song,
                  std::vector<CollectedWave>& outWaves,
                  std::vector<std::string>& outMissing,
                  int& outCustomInstruments)
{
    outCustomInstruments = 0;

    for (const Instrument& inst : song.instruments)
    {
        if (inst.waveform != WaveformType::Custom) continue;
        ++outCustomInstruments;

        if (inst.customWaveform.empty())
        {
            outMissing.push_back(inst.name + " (no wavetable name set)");
            continue;
        }

        const bool already = std::any_of(outWaves.begin(), outWaves.end(),
                                         [&](const CollectedWave& w) { return w.name == inst.customWaveform; });
        if (already) continue;

        auto table = FindCustomWaveform(inst.customWaveform);
        if (!table || table->empty())
        {
            const bool reported = std::any_of(outMissing.begin(), outMissing.end(),
                                              [&](const std::string& m) { return m.rfind(inst.customWaveform + " ", 0) == 0; });
            if (!reported) outMissing.push_back(inst.customWaveform + " (used by " + inst.name + ")");
            continue;
        }

        outWaves.push_back({ inst.customWaveform, std::move(table) });
    }
}

bool WriteBundle(const std::string& path,
                 const std::vector<CollectedWave>& waves,
                 const std::vector<uint8_t>& songBytes,
                 size_t& outBytesWritten)
{
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;

    fwrite(BUNDLE_MAGIC, 1, 4, f);
    WriteRaw(f, BUNDLE_VERSION);

    WriteRaw(f, (uint32_t)waves.size());
    for (const CollectedWave& wave : waves)
    {
        WriteString(f, wave.name);
        WriteRaw(f, (uint32_t)wave.samples->size());
        if (!wave.samples->empty())
            fwrite(wave.samples->data(), sizeof(float), wave.samples->size(), f);
    }

    WriteRaw(f, (uint32_t)songBytes.size());
    if (!songBytes.empty()) fwrite(songBytes.data(), 1, songBytes.size(), f);

    const long written = ftell(f);
    const bool ok = (fclose(f) == 0) && written > 0;
    outBytesWritten = ok ? (size_t)written : 0;
    return ok;
}

// The bundle as a C array. `inline` (C++17) so the header can be included
// from more than one translation unit without a duplicate-symbol link error
// and without every one of them getting its own copy of the bytes.
bool WriteEmbedHeader(const std::string& path,
                      const std::string& baseName,
                      const std::string& identifier,
                      const std::string& songName,
                      const std::vector<uint8_t>& bundleBytes)
{
    FILE* f = fopen(path.c_str(), "w");
    if (!f) return false;

    fprintf(f, "// Generated by SoundStudio's Deploy button -- do not edit.\n");
    fprintf(f, "//\n");
    fprintf(f, "// \"%s\", as a .ssbundle compiled straight into your executable:\n", songName.c_str());
    fprintf(f, "// song data plus every custom wavetable it uses, %zu bytes.\n", bundleBytes.size());
    fprintf(f, "//\n");
    fprintf(f, "//     #include \"%s_embed.h\"\n", baseName.c_str());
    fprintf(f, "//\n");
    fprintf(f, "//     SoundStudio::Player player;\n");
    fprintf(f, "//     player.Init();\n");
    fprintf(f, "//     player.LoadFromMemory(SoundStudioEmbedded::k%s,\n", identifier.c_str());
    fprintf(f, "//                           SoundStudioEmbedded::k%sSize);\n", identifier.c_str());
    fprintf(f, "//     player.Play();\n");
    fprintf(f, "//\n");
    fprintf(f, "// Requires C++17 (for the inline variables below).\n\n");
    fprintf(f, "#pragma once\n#include <cstddef>\n\nnamespace SoundStudioEmbedded\n{\n\n");
    fprintf(f, "inline const unsigned char k%s[] = {\n", identifier.c_str());

    for (size_t i = 0; i < bundleBytes.size(); ++i)
    {
        if (i % 16 == 0) fputs("    ", f);
        fprintf(f, "0x%02x,", bundleBytes[i]);
        fputs((i % 16 == 15) ? "\n" : " ", f);
    }
    if (bundleBytes.size() % 16 != 0) fputc('\n', f);

    fprintf(f, "};\n\n");
    fprintf(f, "inline constexpr std::size_t k%sSize = sizeof(k%s);\n\n", identifier.c_str(), identifier.c_str());
    fprintf(f, "} // namespace SoundStudioEmbedded\n");

    return fclose(f) == 0;
}

bool WriteReadme(const std::string& path,
                 const std::string& baseName,
                 const std::string& identifier,
                 const std::string& songName,
                 const std::vector<CollectedWave>& waves,
                 const std::vector<std::string>& missing,
                 bool playerSourcesCopied)
{
    FILE* f = fopen(path.c_str(), "w");
    if (!f) return false;

    fprintf(f, "%s -- SoundStudio game kit\n", songName.c_str());
    fprintf(f, "=====================================================================\n\n");
    fprintf(f, "Everything needed to play this song from a game. No editor, no ImGui,\n");
    fprintf(f, "no Lua, no plugins -- just raylib and a C++17 compiler.\n\n");

    fprintf(f, "FILES\n");
    fprintf(f, "  %s.ssbundle    the song plus %d baked wavetable(s). Ship this.\n", baseName.c_str(), (int)waves.size());
    fprintf(f, "  %s.ssng        the plain song, to reopen in the editor.\n", baseName.c_str());
    fprintf(f, "  %s_embed.h     the bundle as a C array, if you would rather\n", baseName.c_str());
    fprintf(f, "  %*s                have no music file to ship at all.\n", (int)baseName.size(), "");
    if (playerSourcesCopied)
        fprintf(f, "  soundstudio_player.h / .cpp   the runtime. Add the .cpp to your build.\n");
    else
        fprintf(f, "  (soundstudio_player.h / .cpp were not found next to the editor --\n"
                   "   copy them from the SoundStudio repository root by hand.)\n");

    fprintf(f, "\nOPTION A -- load the file at runtime\n\n");
    fprintf(f, "    #include \"soundstudio_player.h\"\n\n");
    fprintf(f, "    SoundStudio::Player player;\n");
    fprintf(f, "    player.Init();                             // pass false if you already called InitAudioDevice()\n");
    fprintf(f, "    player.LoadFromFile(\"%s.ssbundle\");\n", baseName.c_str());
    fprintf(f, "    player.Play();\n\n");
    fprintf(f, "    while (!WindowShouldClose()) { player.Update(GetFrameTime()); /* ... */ }\n\n");
    fprintf(f, "    player.Shutdown();\n");

    fprintf(f, "\nOPTION B -- compile the music into the executable\n\n");
    fprintf(f, "    #include \"soundstudio_player.h\"\n");
    fprintf(f, "    #include \"%s_embed.h\"\n\n", baseName.c_str());
    fprintf(f, "    player.Init();\n");
    fprintf(f, "    player.LoadFromMemory(SoundStudioEmbedded::k%s,\n", identifier.c_str());
    fprintf(f, "                          SoundStudioEmbedded::k%sSize);\n", identifier.c_str());
    fprintf(f, "    player.Play();\n\n");
    fprintf(f, "Nothing is decoded or synthesized at startup either way: the wavetables\n");
    fprintf(f, "are stored as raw floats and read straight into memory. Play() is a\n");
    fprintf(f, "state flip; audio is generated live, sample by sample, from there.\n");

    if (!waves.empty())
    {
        fprintf(f, "\nWAVETABLES BAKED IN\n");
        for (const CollectedWave& w : waves)
            fprintf(f, "  %-24s %zu samples\n", w.name.c_str(), w.samples->size());
    }

    if (!missing.empty())
    {
        fprintf(f, "\nWARNING -- WAVETABLES THAT COULD NOT BE BAKED\n");
        for (const std::string& m : missing)
            fprintf(f, "  %s\n", m.c_str());
        fprintf(f, "\nThese instruments will play as sine waves, in the game and in the\n");
        fprintf(f, "editor alike. Usually it means the plugin that registers the wave\n");
        fprintf(f, "wasn't loaded when you pressed Deploy -- load it and deploy again.\n");
    }

    return fclose(f) == 0;
}

} // namespace

DeployResult DeployGameKit(const Song& song,
                           const std::string& outDir,
                           const std::vector<std::string>& playerSourceSearchDirs)
{
    DeployResult result;

    if (song.sections.empty() || song.arrangement.empty())
    {
        result.message = "Nothing to deploy: the song has no sections in its arrangement.";
        return result;
    }
    if (outDir.empty())
    {
        result.message = "Deploy failed: no output folder given.";
        return result;
    }

    EnsureDir(outDir);

    const std::string baseName   = SanitizeFileName(song.name);
    const std::string identifier = SanitizeIdentifier(baseName);
    const std::string ssngPath   = JoinPath(outDir, baseName + ".ssng");
    const std::string bundlePath = JoinPath(outDir, baseName + ".ssbundle");

    // Write the song out through the normal saver and read it straight back,
    // so the bytes inside the bundle are byte-for-byte a real .ssng written
    // by the one and only writer -- no second serializer to drift out of
    // sync with SongIO.cpp.
    if (!SaveSong(song, ssngPath))
    {
        result.message = "Deploy failed: couldn't write " + ssngPath;
        return result;
    }

    std::vector<uint8_t> songBytes;
    if (!ReadWholeFile(ssngPath, songBytes))
    {
        result.message = "Deploy failed: couldn't read back " + ssngPath;
        return result;
    }

    std::vector<CollectedWave> waves;
    CollectWaves(song, waves, result.missingWaves, result.customInstruments);
    result.wavesEmbedded = (int)waves.size();

    if (!WriteBundle(bundlePath, waves, songBytes, result.bundleBytes))
    {
        result.message = "Deploy failed: couldn't write " + bundlePath;
        return result;
    }
    result.bundlePath = bundlePath;

    std::vector<uint8_t> bundleBytes;
    if (!ReadWholeFile(bundlePath, bundleBytes))
    {
        result.message = "Deploy failed: couldn't read back " + bundlePath;
        return result;
    }

    if (!WriteEmbedHeader(JoinPath(outDir, baseName + "_embed.h"), baseName, identifier, song.name, bundleBytes))
    {
        result.message = "Deploy failed: couldn't write " + baseName + "_embed.h";
        return result;
    }

    // Copy the runtime in, if we can find it. Not fatal -- the bundle is the
    // part that can only be made here; the runtime is just a file in the repo.
    for (const std::string& dir : playerSourceSearchDirs)
    {
        const std::string h   = JoinPath(dir, "soundstudio_player.h");
        const std::string cpp = JoinPath(dir, "soundstudio_player.cpp");
        if (!FileExists(h) || !FileExists(cpp)) continue;

        if (CopyFileTo(h,   JoinPath(outDir, "soundstudio_player.h")) &&
            CopyFileTo(cpp, JoinPath(outDir, "soundstudio_player.cpp")))
        {
            result.playerSourcesCopied = true;
        }
        break;
    }

    WriteReadme(JoinPath(outDir, "README.txt"), baseName, identifier, song.name,
                waves, result.missingWaves, result.playerSourcesCopied);

    result.ok = true;

    char summary[512];
    snprintf(summary, sizeof(summary),
             "Deployed to %s -- %s.ssbundle (%.1f KB, %d wavetable%s baked in)%s%s",
             outDir.c_str(), baseName.c_str(), result.bundleBytes / 1024.0f,
             result.wavesEmbedded, result.wavesEmbedded == 1 ? "" : "s",
             result.playerSourcesCopied ? " + runtime sources" : "",
             result.missingWaves.empty() ? "." : ". See README.txt: some wavetables are missing!");
    result.message = summary;

    return result;
}
