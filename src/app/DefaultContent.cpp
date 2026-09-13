#include "app/DefaultContent.h"
#include "app/AppPaths.h"
#include "song/DefaultInstruments.h"
#include "plugin/DefaultPlugins.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{

constexpr int MANIFEST_VERSION = 1;

std::string ManifestPath()
{
    return AppPaths::RootDir() + "/defaults.manifest";
}

bool FileExists(const std::string& path)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

// Our catalogue strings are plain ASCII names, but escaping is cheap and a
// hand-rolled JSON writer that doesn't escape is a bug waiting for the
// first preset called 6" Snare.
std::string JsonEscape(const char* text)
{
    std::string out;
    for (const char* p = text ? text : ""; *p; ++p)
    {
        switch (*p)
        {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if ((unsigned char)*p < 0x20)
                {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)*p);
                    out += buf;
                }
                else out.push_back(*p);
        }
    }
    return out;
}

int WriteCatalogue(const std::string& path)
{
    FILE* f = fopen(path.c_str(), "w");
    if (!f)
    {
        return 1;
    }

    fprintf(f, "{\n");
    fprintf(f, "  \"catalogueVersion\": 1,\n");

    fprintf(f, "  \"instruments\": [\n");
    const int instrumentCount = DefaultInstrumentCount();
    for (int i = 0; i < instrumentCount; ++i)
    {
        const DefaultInstrumentInfo info = DefaultInstrumentAt(i);
        fprintf(f, "    { \"id\": \"%s\", \"name\": \"%s\", \"group\": \"%s\" }%s\n",
                JsonEscape(info.file).c_str(),
                JsonEscape(info.name).c_str(),
                JsonEscape(info.group).c_str(),
                (i + 1 < instrumentCount) ? "," : "");
    }
    fprintf(f, "  ],\n");

    fprintf(f, "  \"plugins\": [\n");
    const int pluginCount = DefaultPluginCount();
    for (int i = 0; i < pluginCount; ++i)
    {
        const DefaultPluginInfo info = DefaultPluginAt(i);
        fprintf(f, "    { \"id\": \"%s\", \"name\": \"%s\", \"description\": \"%s\" }%s\n",
                JsonEscape(info.file).c_str(),
                JsonEscape(info.name).c_str(),
                JsonEscape(info.description).c_str(),
                (i + 1 < pluginCount) ? "," : "");
    }
    fprintf(f, "  ]\n");
    fprintf(f, "}\n");

    return (fclose(f) == 0) ? 0 : 1;
}

std::string Trim(const std::string& s)
{
    size_t begin = 0;
    size_t end = s.size();
    while (begin < end && (s[begin] == ' ' || s[begin] == '\t' || s[begin] == '\r' || s[begin] == '\n')) ++begin;
    while (end > begin && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r' || s[end - 1] == '\n')) --end;
    return s.substr(begin, end - begin);
}

bool ReadLines(const std::string& path, std::vector<std::string>& outLines)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;

    std::string current;
    int c;
    while ((c = fgetc(f)) != EOF)
    {
        if (c == '\n') { outLines.push_back(current); current.clear(); }
        else current.push_back((char)c);
    }
    if (!current.empty()) outLines.push_back(current);

    fclose(f);
    return true;
}

bool WriteManifest(const std::vector<std::string>& installedIds)
{
    FILE* f = fopen(ManifestPath().c_str(), "w");
    if (!f) return false;

    fprintf(f, "# SoundStudio bundled-content manifest\n");
    fprintf(f, "#\n");
    fprintf(f, "# Written once, when the library was chosen (by the installer, or by a\n");
    fprintf(f, "# first launch that installed everything). Its existence is what stops\n");
    fprintf(f, "# SoundStudio re-creating presets and plugins you have since deleted.\n");
    fprintf(f, "#\n");
    fprintf(f, "# Delete this file to be given the whole bundled library again on the\n");
    fprintf(f, "# next launch. Editing it has no other effect.\n");
    fprintf(f, "version %d\n", MANIFEST_VERSION);

    for (const std::string& id : installedIds)
        fprintf(f, "%s\n", id.c_str());

    return fclose(f) == 0;
}

// Applies a selection file. Wildcards ("instrument:*", "plugin:*") expand to
// the whole catalogue of that kind. Unknown ids are counted and reported via
// the exit code rather than silently ignored -- a typo in a selection file
// should not look like a successful install.
int InstallSelection(const std::string& selectionPath)
{
    std::vector<std::string> lines;
    if (!ReadLines(selectionPath, lines)) return 1;

    std::vector<std::string> installed;
    int unknown = 0;
    int failed = 0;

    auto installAllInstruments = [&]()
    {
        for (int i = 0; i < DefaultInstrumentCount(); ++i)
        {
            const DefaultInstrumentInfo info = DefaultInstrumentAt(i);
            if (WriteDefaultInstrument(info.file)) installed.push_back(std::string("instrument:") + info.file);
            else ++failed;
        }
    };

    auto installAllPlugins = [&]()
    {
        for (int i = 0; i < DefaultPluginCount(); ++i)
        {
            const DefaultPluginInfo info = DefaultPluginAt(i);
            if (WriteDefaultPlugin(info.file)) installed.push_back(std::string("plugin:") + info.file);
            else ++failed;
        }
    };

    for (const std::string& rawLine : lines)
    {
        const std::string line = Trim(rawLine);
        if (line.empty() || line[0] == '#') continue;

        const size_t colon = line.find(':');
        if (colon == std::string::npos) { ++unknown; continue; }

        const std::string kind = line.substr(0, colon);
        const std::string id   = Trim(line.substr(colon + 1));

        if (kind == "instrument")
        {
            if (id == "*") { installAllInstruments(); continue; }
            if (WriteDefaultInstrument(id)) installed.push_back(line);
            else ++unknown;
        }
        else if (kind == "plugin")
        {
            if (id == "*") { installAllPlugins(); continue; }
            if (WriteDefaultPlugin(id)) installed.push_back(line);
            else ++unknown;
        }
        else
        {
            ++unknown;
        }
    }

    // The manifest is written even for an empty selection -- "the user chose
    // nothing" is a real choice, and recording it is what stops the next
    // launch from helpfully installing all 108 presets anyway.
    if (!WriteManifest(installed)) return 1;

    if (failed > 0)  return 2; // something couldn't be written to disk
    if (unknown > 0) return 3; // the selection file named things we don't have
    return 0;
}

} // namespace

namespace DefaultContent
{

bool HandleCommandLine(int argc, char** argv, int& outExitCode)
{
    for (int i = 1; i < argc; ++i)
    {
        const bool isList    = strcmp(argv[i], "--list-defaults") == 0;
        const bool isInstall = strcmp(argv[i], "--install-defaults") == 0;
        if (!isList && !isInstall) continue;

        if (i + 1 >= argc)
        {
            outExitCode = 1; // the mode takes a file path, and didn't get one
            return true;
        }

        // Both modes touch the SoundStudio folder, and the installer may be
        // running this before the app has ever been launched normally.
        AppPaths::EnsureDirectories();

        outExitCode = isList ? WriteCatalogue(argv[i + 1])
                             : InstallSelection(argv[i + 1]);
        return true;
    }

    return false;
}

void EnsureInstalledOnFirstRun()
{
    // Not just main()'s job: these writes go straight into Instruments/ and
    // Plugins/, and silently write nothing at all if those don't exist yet.
    // Idempotent, so calling it again here costs nothing.
    AppPaths::EnsureDirectories();

    // A manifest means somebody has already decided what this install
    // should contain. Respect that -- including the parts they decided to
    // leave out, and anything they have deleted since.
    if (FileExists(ManifestPath())) return;

    WriteDefaultInstrumentsIfMissing();
    WriteDefaultPluginsIfMissing();

    std::vector<std::string> installed;
    installed.reserve((size_t)DefaultInstrumentCount() + (size_t)DefaultPluginCount());
    for (int i = 0; i < DefaultInstrumentCount(); ++i)
        installed.push_back(std::string("instrument:") + DefaultInstrumentAt(i).file);
    for (int i = 0; i < DefaultPluginCount(); ++i)
        installed.push_back(std::string("plugin:") + DefaultPluginAt(i).file);

    WriteManifest(installed);
}

} // namespace DefaultContent
