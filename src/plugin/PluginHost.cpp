#include "plugin/PluginHost.h"
#include "song/SongData.h"

namespace
{
    PluginHost::Context g_ctx;
}

namespace PluginHost
{
    void Set(const Context& ctx) { g_ctx = ctx; }
    const Context& Get() { return g_ctx; }

    int InstrumentCount()
    {
        return g_ctx.instruments ? (int)g_ctx.instruments->size() : 0;
    }

    Instrument* InstrumentAt(int index)
    {
        if (!g_ctx.instruments) return nullptr;
        if (index < 0 || index >= (int)g_ctx.instruments->size()) return nullptr;
        return &(*g_ctx.instruments)[index];
    }

    void PushUndo()   { if (g_ctx.pushUndo)   g_ctx.pushUndo(); }
    void MarkDirty()  { if (g_ctx.markDirty)  g_ctx.markDirty(); }
    void ResetSynth() { if (g_ctx.resetSynth) g_ctx.resetSynth(); }
}
