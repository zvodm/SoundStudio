#include "app/AppPaths.h"
#include <cstdlib>

#ifdef _WIN32
#include <direct.h>
#define SS_MKDIR(path) _mkdir(path)
#else
#include <sys/stat.h>
#define SS_MKDIR(path) mkdir(path, 0755)
#endif

namespace
{
    // %USERPROFILE% on Windows, $HOME elsewhere, with a couple of
    // fallbacks for the odd environment that has neither set. Deliberately
    // avoids std::filesystem here -- plain mkdir + getenv has no extra
    // linking requirements to worry about across toolchains.
    // A variable that is set but empty is treated as not set: taking it at
    // face value would resolve the SoundStudio folder to "/SoundStudio" and
    // quietly try to write the whole preset library to the filesystem root.
    const char* EnvOrNull(const char* name)
    {
        const char* value = std::getenv(name);
        return (value && value[0] != '\0') ? value : nullptr;
    }

    std::string GetHomeDir()
    {
        if (const char* userProfile = EnvOrNull("USERPROFILE")) return userProfile; // Windows
        if (const char* home = EnvOrNull("HOME")) return home;                      // POSIX
        const char* drive = EnvOrNull("HOMEDRIVE");
        const char* path  = EnvOrNull("HOMEPATH");
        if (drive && path) return std::string(drive) + path;                        // older Windows fallback
        return "."; // last resort -- current directory
    }

    std::string Join(const std::string& base, const char* sub)
    {
        return base + "/" + sub; // forward slash works fine with mkdir/fopen on Windows too
    }
}

namespace AppPaths
{
    const std::string& RootDir()
    {
        static std::string dir = Join(GetHomeDir(), "SoundStudio");
        return dir;
    }

    const std::string& WavesDir()       { static std::string dir = Join(RootDir(), "Waves");       return dir; }
    const std::string& PluginsDir()     { static std::string dir = Join(RootDir(), "Plugins");     return dir; }
    const std::string& SongsDir()       { static std::string dir = Join(RootDir(), "Songs");       return dir; }
    const std::string& InstrumentsDir() { static std::string dir = Join(RootDir(), "Instruments"); return dir; }

    void EnsureDirectories()
    {
        // Ignore errors -- EEXIST is the expected steady state after the
        // first launch, and there's nothing actionable to do about any
        // other failure here (a read-only filesystem, no home dir, ...);
        // callers just fall back to whatever paths end up unusable.
        SS_MKDIR(RootDir().c_str());
        SS_MKDIR(WavesDir().c_str());
        SS_MKDIR(PluginsDir().c_str());
        SS_MKDIR(SongsDir().c_str());
        SS_MKDIR(InstrumentsDir().c_str());
    }
}
