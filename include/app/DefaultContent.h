#pragma once

// ------------------------------------------------------------------
// Bundled content: which of the 100-odd instrument presets and the
// bundled plugins actually get written into the user's SoundStudio folder.
//
// Until now this was not a decision anyone got to make -- every launch
// called WriteDefaultInstrumentsIfMissing()/WriteDefaultPluginsIfMissing()
// and the whole library appeared, including the resurrection of anything
// the user had deliberately deleted. Now the choice is made once, by the
// installer, and recorded in a manifest.
//
// Two headless command-line modes make that possible, so install.ps1 can
// show a pick-list without keeping its own copy of the catalogue (which
// would drift out of date the moment a preset is added):
//
//     SoundStudio.exe --list-defaults <out.json>
//         Writes the catalogue -- every bundled preset with its display
//         name and family, and every bundled plugin -- as JSON, then exits.
//
//     SoundStudio.exe --install-defaults <selection.txt>
//         Installs exactly the listed ids, writes the manifest, then exits.
//         One id per line, "instrument:<stem>" or "plugin:<stem>"; the
//         wildcards "instrument:*" and "plugin:*" mean all of that kind,
//         and blank lines and #-comments are ignored. Anything already on
//         disk is left untouched.
//
// Both write to a FILE rather than stdout on purpose: the editor is linked
// as a GUI subsystem binary (-mwindows), so it has no console to print to
// when launched from one.
//
// The manifest lives at <SoundStudio>/defaults.manifest. Its presence is
// what marks the choice as made -- delete it and the next normal launch
// installs the full library again.
// ------------------------------------------------------------------
namespace DefaultContent
{
    // Returns true if argv held one of the headless commands above, in
    // which case main() should return outExitCode immediately without
    // opening a window. Call it as the very first thing in main().
    bool HandleCommandLine(int argc, char** argv, int& outExitCode);

    // The normal startup path. Installs the full bundled library only if
    // no manifest exists yet -- i.e. only when nobody has been offered the
    // choice, which covers running a build straight out of build.bat.
    // Once a manifest is there, this does nothing, so presets the user
    // deleted stay deleted.
    void EnsureInstalledOnFirstRun();
}
