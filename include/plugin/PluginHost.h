#pragma once
#include <functional>
#include <vector>

struct Instrument;

// ------------------------------------------------------------------
// The bridge between main()'s editor state and the Lua plugin API.
//
// PluginManager's bound `ss.*` functions are plain free functions (that's
// what LuaBridge::addFunction wants), but the things a plugin needs to
// reach -- the song's instrument list, which track is selected, the undo
// stack, the dirty flag, the live synth -- are all locals inside main().
// Rather than hoisting all of that to file scope, main() fills in one
// Context here at startup and the plugin API reads it back through
// PluginHost::Get().
//
// Everything is optional: a null instruments pointer or an empty
// std::function simply means "the host doesn't offer this", and the bound
// functions degrade to no-ops / zero rather than crashing. That keeps
// PluginManager usable from a test harness or a headless tool that never
// sets a context at all.
// ------------------------------------------------------------------
namespace PluginHost
{
    struct Context
    {
        // The live instrument list. The vector object itself is stable for
        // the lifetime of main(), so holding a pointer to it is safe even
        // though its contents get reallocated by add/remove/undo.
        std::vector<Instrument>* instruments = nullptr;

        // Which track the editor currently has selected (index into
        // *instruments). Plugins can read it to follow the selection and
        // write it to change it.
        int* selectedInstrument = nullptr;

        // Push an undo snapshot of the whole song *before* a change is
        // applied -- same contract as main()'s own
        // undo.PushUndo(instruments, sections, arrangement) calls.
        std::function<void()> pushUndo;

        // Flag the song as having unsaved changes.
        std::function<void()> markDirty;

        // Silence every voice with no release tail. Must be called before
        // an Instrument a sounding Voice might point at gets overwritten
        // or the vector reallocated -- otherwise a Voice is left holding a
        // dangling Instrument*.
        std::function<void()> resetSynth;
    };

    void Set(const Context& ctx);
    const Context& Get();

    // Bounds-checked accessor: nullptr if no context is set or `index` is
    // outside the current instrument list.
    Instrument* InstrumentAt(int index);

    int InstrumentCount();

    // Convenience wrappers that are safe to call with an unset context.
    void PushUndo();
    void MarkDirty();
    void ResetSynth();
}
