#include "plugin/PluginManager.h"
#include "plugin/DefaultPlugins.h"
#include "plugin/PluginHost.h"
#include "song/SongData.h"
#include "song/InstrumentIO.h"
#include "app/AppPaths.h"
#include "audio/Synth.h"
#include "lua.hpp"
#include "LuaBridge/LuaBridge.h"
#include "LuaBridge/Vector.h"
#include "imgui.h"
#include "raylib.h"
#include <dirent.h>
#include <cstring>
#include <algorithm>
#include <cmath>

// -----------------------------------------------------------------------
// Bound API -- everything under Lua's global `ss` table. Kept as plain,
// non-capturing free functions (LuaBridge's addFunction wants a function
// pointer, and a plugin script only ever runs on the main/UI thread
// between this plugin's own ImGui::Begin()/End(), so there's no need for
// any of this to carry per-plugin state).
// -----------------------------------------------------------------------

namespace pluginapi
{

// ---- basic widgets, operate on whichever ImGui window is currently open
// (PluginManager::DrawAll wraps each plugin's DrawUI() in its own Begin/End) ----

static void Text(const std::string& s) { ImGui::TextUnformatted(s.c_str()); }
static void SameLine() { ImGui::SameLine(); }
static void Separator() { ImGui::Separator(); }

static bool Button(const std::string& label) { return ImGui::Button(label.c_str()); }

static bool Checkbox(const std::string& label, bool current)
{
    bool v = current;
    ImGui::Checkbox(label.c_str(), &v);
    return v;
}

static float SliderFloat(const std::string& label, float current, float minV, float maxV)
{
    float v = current;
    ImGui::SliderFloat(label.c_str(), &v, minV, maxV);
    return v;
}

static std::string InputText(const std::string& label, const std::string& current)
{
    char buf[256];
    strncpy(buf, current.c_str(), sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    ImGui::InputText(label.c_str(), buf, sizeof(buf));
    return std::string(buf);
}

// ---- a small drawing canvas: BeginCanvas reserves a WxH region (also
// capturing mouse hover/click/drag for it), Draw* calls add shapes
// relative to the canvas's own top-left corner ----

static ImVec2 g_canvasOrigin;
static ImDrawList* g_canvasDrawList = nullptr;
static bool g_canvasHovered = false;
static bool g_canvasClicked = false;
static bool g_canvasDown = false;

static void BeginCanvas(float w, float h)
{
    g_canvasOrigin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##ss_plugin_canvas", ImVec2(w, h));
    g_canvasHovered = ImGui::IsItemHovered();
    g_canvasClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    g_canvasDown = g_canvasHovered && ImGui::IsMouseDown(ImGuiMouseButton_Left);
    g_canvasDrawList = ImGui::GetWindowDrawList();
    g_canvasDrawList->AddRectFilled(g_canvasOrigin, ImVec2(g_canvasOrigin.x + w, g_canvasOrigin.y + h), IM_COL32(18, 18, 20, 255));
    g_canvasDrawList->AddRect(g_canvasOrigin, ImVec2(g_canvasOrigin.x + w, g_canvasOrigin.y + h), IM_COL32(60, 60, 60, 255));
}

static void EndCanvas() { g_canvasDrawList = nullptr; }

static void DrawLine(float x1, float y1, float x2, float y2, int r, int g, int b, int a)
{
    if (!g_canvasDrawList) return;
    g_canvasDrawList->AddLine(ImVec2(g_canvasOrigin.x + x1, g_canvasOrigin.y + y1),
                               ImVec2(g_canvasOrigin.x + x2, g_canvasOrigin.y + y2),
                               IM_COL32(r, g, b, a));
}

static void DrawCircle(float x, float y, float radius, int r, int g, int b, int a)
{
    if (!g_canvasDrawList) return;
    g_canvasDrawList->AddCircleFilled(ImVec2(g_canvasOrigin.x + x, g_canvasOrigin.y + y), radius, IM_COL32(r, g, b, a));
}

static void DrawRect(float x, float y, float w, float h, int r, int g, int b, int a)
{
    if (!g_canvasDrawList) return;
    g_canvasDrawList->AddRectFilled(ImVec2(g_canvasOrigin.x + x, g_canvasOrigin.y + y),
                                     ImVec2(g_canvasOrigin.x + x + w, g_canvasOrigin.y + y + h),
                                     IM_COL32(r, g, b, a));
}

static void DrawText(float x, float y, const std::string& s, int r, int g, int b, int a)
{
    if (!g_canvasDrawList) return;
    g_canvasDrawList->AddText(ImVec2(g_canvasOrigin.x + x, g_canvasOrigin.y + y), IM_COL32(r, g, b, a), s.c_str());
}

static float MouseX() { return ImGui::GetMousePos().x - g_canvasOrigin.x; }
static float MouseY() { return ImGui::GetMousePos().y - g_canvasOrigin.y; }
static bool MouseClicked() { return g_canvasClicked; }
static bool MouseDown() { return g_canvasDown; }
static bool MouseHovered() { return g_canvasHovered; }

// ---- engine readouts / registration ----

static std::vector<float> GetMasterScopeLeft()
{
    std::vector<float> left, right;
    if (Synth* synth = GetActiveSynth()) synth->GetMasterScopeSamples(left, right);
    return left;
}

static std::vector<float> GetMasterScopeRight()
{
    std::vector<float> left, right;
    if (Synth* synth = GetActiveSynth()) synth->GetMasterScopeSamples(left, right);
    return right;
}

static void RegisterWaveform(const std::string& name, std::vector<float> samples)
{
    RegisterCustomWaveform(name, std::move(samples));
}

static std::string WavesDirLua() { return AppPaths::WavesDir(); }
static std::string PluginsDirLua() { return AppPaths::PluginsDir(); }
static std::string SongsDirLua() { return AppPaths::SongsDir(); }
static std::string InstrumentsDirLua() { return AppPaths::InstrumentsDir(); }

// ---- extra widgets: typed text-entry equivalents of the slider/combo
// widgets above. ImGui::InputFloat/InputInt with a zero step render as a
// plain text box (no +/- buttons, no drag), which is exactly what an
// "exact values, no sliders" editor wants -- and unlike a slider they
// accept values outside any range the UI happens to suggest. ----

static float InputFloat(const std::string& label, float current)
{
    float v = current;
    ImGui::InputFloat(label.c_str(), &v, 0.0f, 0.0f, "%.4f");
    return v;
}

static int InputInt(const std::string& label, int current)
{
    int v = current;
    ImGui::InputInt(label.c_str(), &v, 0, 0);
    return v;
}

static int Combo(const std::string& label, int current, std::vector<std::string> items)
{
    std::vector<const char*> ptrs;
    ptrs.reserve(items.size());
    for (const auto& s : items) ptrs.push_back(s.c_str());
    if (ptrs.empty()) return current;

    int v = current;
    if (v < 0) v = 0;
    if (v >= (int)ptrs.size()) v = (int)ptrs.size() - 1;
    ImGui::Combo(label.c_str(), &v, ptrs.data(), (int)ptrs.size());
    return v;
}

static bool Selectable(const std::string& label, bool selected)
{
    return ImGui::Selectable(label.c_str(), selected);
}

// True for the frame a widget took keyboard focus -- the moment to push an
// undo snapshot, before any edit has landed (same pattern main.cpp's own
// Instrument panel uses).
static bool IsItemActivated() { return ImGui::IsItemActivated(); }
static bool IsItemDeactivatedAfterEdit() { return ImGui::IsItemDeactivatedAfterEdit(); }

static void SeparatorText(const std::string& s) { ImGui::SeparatorText(s.c_str()); }
static void TextDisabled(const std::string& s) { ImGui::TextDisabled("%s", s.c_str()); }
static void TextColored(int r, int g, int b, int a, const std::string& s)
{
    ImGui::TextColored(ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f), "%s", s.c_str());
}
static void TextWrapped(const std::string& s) { ImGui::TextWrapped("%s", s.c_str()); }
static void Spacing() { ImGui::Spacing(); }
static void SetNextItemWidth(float w) { ImGui::SetNextItemWidth(w); }
static void PushId(int id) { ImGui::PushID(id); }
static void PopId() { ImGui::PopID(); }
static bool BeginChildRegion(const std::string& id, float w, float h)
{
    return ImGui::BeginChild(id.c_str(), ImVec2(w, h), ImGuiChildFlags_Borders);
}
static void EndChildRegion() { ImGui::EndChild(); }

// ---- live instrument access ----
//
// Everything below routes through PluginHost (see plugin/PluginHost.h),
// which main() fills in at startup. Fields are addressed by name rather
// than by handing Lua a userdata Instrument*, because the instrument
// vector reallocates constantly (add/remove track, undo, song load) --
// a name plus an index is re-resolved on every single call and can never
// go stale, where a cached pointer absolutely would.

static int InstrumentCount() { return PluginHost::InstrumentCount(); }

static int SelectedInstrument()
{
    const auto& ctx = PluginHost::Get();
    return ctx.selectedInstrument ? *ctx.selectedInstrument : 0;
}

static void SelectInstrument(int index)
{
    const auto& ctx = PluginHost::Get();
    if (!ctx.selectedInstrument) return;
    const int count = PluginHost::InstrumentCount();
    if (count <= 0) return;
    *ctx.selectedInstrument = std::clamp(index, 0, count - 1);
}

static float* NumberField(Instrument& inst, const std::string& field)
{
    if (field == "dutyCycle")    return &inst.dutyCycle;
    if (field == "volume")       return &inst.volume;
    if (field == "pan")          return &inst.pan;
    if (field == "attack")       return &inst.envelope.attack;
    if (field == "decay")        return &inst.envelope.decay;
    if (field == "sustain")      return &inst.envelope.sustain;
    if (field == "release")      return &inst.envelope.release;
    if (field == "arpRate")      return &inst.arpRate;
    if (field == "vibratoRate")  return &inst.vibratoRate;
    if (field == "vibratoDepth") return &inst.vibratoDepth;
    if (field == "filterCutoff") return &inst.filterCutoff;
    if (field == "env2Attack")   return &inst.env2.attack;
    if (field == "env2Decay")    return &inst.env2.decay;
    if (field == "env2Sustain")  return &inst.env2.sustain;
    if (field == "env2Release")  return &inst.env2.release;
    if (field == "env2Amount")   return &inst.env2Amount;
    return nullptr;
}

// Only the clamps the audio engine genuinely needs to stay sane -- a
// negative envelope time or a duty cycle of 0 produces silence or a
// divide-by-nothing, and levels above 1 just clip into the limiter. Ranges
// that are merely *conventional* (a 30 notes/s arp cap, +/-1 semitone of
// vibrato, a 1-second attack) are deliberately NOT enforced: being able to
// type past the sliders' suggestions is the entire point of this panel.
static float ClampField(const std::string& field, float v)
{
    if (field == "dutyCycle")  return std::clamp(v, 0.01f, 0.99f);
    if (field == "pan")        return std::clamp(v, -1.0f, 1.0f);
    if (field == "volume" || field == "sustain" || field == "env2Sustain" || field == "filterCutoff")
        return std::clamp(v, 0.0f, 1.0f);
    if (field == "attack" || field == "decay" || field == "release" ||
        field == "env2Attack" || field == "env2Decay" || field == "env2Release")
        return std::fmax(0.0f, v);
    if (field == "arpRate")     return std::fmax(0.1f, v);
    if (field == "vibratoRate") return std::fmax(0.0f, v);
    return v;
}

static float GetNumber(int index, const std::string& field)
{
    Instrument* inst = PluginHost::InstrumentAt(index);
    if (!inst) return 0.0f;
    const float* p = NumberField(*inst, field);
    return p ? *p : 0.0f;
}

static void SetNumber(int index, const std::string& field, float value)
{
    Instrument* inst = PluginHost::InstrumentAt(index);
    if (!inst) return;
    float* p = NumberField(*inst, field);
    if (!p) return;
    const float clamped = ClampField(field, value);
    if (*p == clamped) return;
    *p = clamped;
    PluginHost::MarkDirty();
}

// Enums and bools both come through as plain ints -- Lua has one number
// type and LuaBridge would need a registration per enum otherwise.
static int GetInt(int index, const std::string& field)
{
    Instrument* inst = PluginHost::InstrumentAt(index);
    if (!inst) return 0;
    if (field == "waveform")       return (int)inst->waveform;
    if (field == "category")       return (int)inst->category;
    if (field == "arpPattern")     return (int)inst->arpPattern;
    if (field == "filterType")     return (int)inst->filterType;
    if (field == "env2Target")     return (int)inst->env2Target;
    if (field == "muted")          return inst->muted ? 1 : 0;
    if (field == "solo")           return inst->solo ? 1 : 0;
    if (field == "vibratoEnabled") return inst->vibratoEnabled ? 1 : 0;
    return 0;
}

static void SetInt(int index, const std::string& field, int value)
{
    Instrument* inst = PluginHost::InstrumentAt(index);
    if (!inst) return;

    if (field == "waveform")            inst->waveform   = (WaveformType)std::clamp(value, 0, 5);
    else if (field == "category")       inst->category   = (InstrumentCategory)std::clamp(value, 0, 4);
    else if (field == "arpPattern")     inst->arpPattern = (ArpPattern)std::clamp(value, 0, 3);
    else if (field == "filterType")     inst->filterType = (FilterType)std::clamp(value, 0, 2);
    else if (field == "env2Target")     inst->env2Target = (Env2Target)std::clamp(value, 0, 2);
    else if (field == "muted")          inst->muted = (value != 0);
    else if (field == "solo")           inst->solo = (value != 0);
    else if (field == "vibratoEnabled") inst->vibratoEnabled = (value != 0);
    else return;

    PluginHost::MarkDirty();
}

static std::string GetText(int index, const std::string& field)
{
    Instrument* inst = PluginHost::InstrumentAt(index);
    if (!inst) return std::string();
    if (field == "name")           return inst->name;
    if (field == "customWaveform") return inst->customWaveform;
    return std::string();
}

static void SetText(int index, const std::string& field, const std::string& value)
{
    Instrument* inst = PluginHost::InstrumentAt(index);
    if (!inst) return;
    if (field == "name")                inst->name = value;
    else if (field == "customWaveform") inst->customWaveform = value;
    else return;
    PluginHost::MarkDirty();
}

// Push an undo snapshot of the whole song before a change lands. Call it
// on the frame a field is activated, not after it's edited.
static void BeginEdit() { PluginHost::PushUndo(); }

static bool HasCustomWaveform(const std::string& name)
{
    return FindCustomWaveform(name) != nullptr;
}

// ---- presets ----

static bool SavePreset(int index, const std::string& path)
{
    Instrument* inst = PluginHost::InstrumentAt(index);
    if (!inst) return false;
    return SaveInstrumentPreset(*inst, path);
}

// keepName leaves the track's own name alone and takes only the sound,
// which is almost always what you want when auditioning a library patch
// against an existing arrangement.
static bool LoadPreset(int index, const std::string& path, bool keepName)
{
    Instrument* inst = PluginHost::InstrumentAt(index);
    if (!inst) return false;

    Instrument loaded;
    if (!LoadInstrumentPreset(loaded, path)) return false;

    PluginHost::PushUndo();
    PluginHost::ResetSynth(); // no Voice may be left pointing into what we're about to overwrite

    // InstrumentAt again: ResetSynth/PushUndo don't resize the vector, but
    // re-resolving costs nothing and keeps this correct if they ever do.
    inst = PluginHost::InstrumentAt(index);
    if (!inst) return false;

    const std::string previousName = inst->name;
    *inst = loaded;
    if (keepName) inst->name = previousName;

    PluginHost::MarkDirty();
    return true;
}

// Every .ssip in the Instruments folder, without the extension, sorted --
// so a plugin can offer a click-to-load library list instead of making the
// user type a full path.
static std::vector<std::string> ListPresets()
{
    std::vector<std::string> names;
    DIR* dir = opendir(AppPaths::InstrumentsDir().c_str());
    if (!dir) return names;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr)
    {
        std::string name = entry->d_name;
        if (name.size() > 5 && name.compare(name.size() - 5, 5, ".ssip") == 0)
            names.push_back(name.substr(0, name.size() - 5));
    }
    closedir(dir);

    std::sort(names.begin(), names.end());
    return names;
}

static void BindApi(lua_State* L)
{
    luabridge::getGlobalNamespace(L)
        .beginNamespace("ss")
            .addFunction("Text", &Text)
            .addFunction("SameLine", &SameLine)
            .addFunction("Separator", &Separator)
            .addFunction("Button", &Button)
            .addFunction("Checkbox", &Checkbox)
            .addFunction("SliderFloat", &SliderFloat)
            .addFunction("InputText", &InputText)
            .addFunction("BeginCanvas", &BeginCanvas)
            .addFunction("EndCanvas", &EndCanvas)
            .addFunction("DrawLine", &DrawLine)
            .addFunction("DrawCircle", &DrawCircle)
            .addFunction("DrawRect", &DrawRect)
            .addFunction("DrawText", &DrawText)
            .addFunction("MouseX", &MouseX)
            .addFunction("MouseY", &MouseY)
            .addFunction("MouseClicked", &MouseClicked)
            .addFunction("MouseDown", &MouseDown)
            .addFunction("MouseHovered", &MouseHovered)
            .addFunction("GetMasterScopeLeft", &GetMasterScopeLeft)
            .addFunction("GetMasterScopeRight", &GetMasterScopeRight)
            .addFunction("RegisterWaveform", &RegisterWaveform)
            .addFunction("WavesDir", &WavesDirLua)
            .addFunction("PluginsDir", &PluginsDirLua)
            .addFunction("SongsDir", &SongsDirLua)
            .addFunction("InstrumentsDir", &InstrumentsDirLua)
            .addFunction("InputFloat", &InputFloat)
            .addFunction("InputInt", &InputInt)
            .addFunction("Combo", &Combo)
            .addFunction("Selectable", &Selectable)
            .addFunction("IsItemActivated", &IsItemActivated)
            .addFunction("IsItemDeactivatedAfterEdit", &IsItemDeactivatedAfterEdit)
            .addFunction("SeparatorText", &SeparatorText)
            .addFunction("TextDisabled", &TextDisabled)
            .addFunction("TextColored", &TextColored)
            .addFunction("TextWrapped", &TextWrapped)
            .addFunction("Spacing", &Spacing)
            .addFunction("SetNextItemWidth", &SetNextItemWidth)
            .addFunction("PushId", &PushId)
            .addFunction("PopId", &PopId)
            .addFunction("BeginChildRegion", &BeginChildRegion)
            .addFunction("EndChildRegion", &EndChildRegion)
            .addFunction("InstrumentCount", &InstrumentCount)
            .addFunction("SelectedInstrument", &SelectedInstrument)
            .addFunction("SelectInstrument", &SelectInstrument)
            .addFunction("GetNumber", &GetNumber)
            .addFunction("SetNumber", &SetNumber)
            .addFunction("GetInt", &GetInt)
            .addFunction("SetInt", &SetInt)
            .addFunction("GetText", &GetText)
            .addFunction("SetText", &SetText)
            .addFunction("BeginEdit", &BeginEdit)
            .addFunction("HasCustomWaveform", &HasCustomWaveform)
            .addFunction("SavePreset", &SavePreset)
            .addFunction("LoadPreset", &LoadPreset)
            .addFunction("ListPresets", &ListPresets)
        .endNamespace();
}

} // namespace pluginapi

// -----------------------------------------------------------------------
// PluginManager
// -----------------------------------------------------------------------

void PluginManager::LoadAll()
{
    Shutdown();

    WriteDefaultPluginsIfMissing();

    DIR* dir = opendir(AppPaths::PluginsDir().c_str());
    if (!dir)
    {
        TraceLog(LOG_WARNING, "PluginManager: couldn't open Plugins dir '%s'", AppPaths::PluginsDir().c_str());
        return;
    }

    std::vector<std::string> luaFiles;
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr)
    {
        std::string name = entry->d_name;
        if (name.size() > 4 && name.compare(name.size() - 4, 4, ".lua") == 0)
            luaFiles.push_back(name);
    }
    closedir(dir);

    std::sort(luaFiles.begin(), luaFiles.end()); // stable, predictable panel order

    for (const auto& fileName : luaFiles)
    {
        std::string defaultName = fileName.substr(0, fileName.size() - 4);
        LoadPlugin(AppPaths::PluginsDir() + "/" + fileName, defaultName);
    }
}

void PluginManager::LoadPlugin(const std::string& path, const std::string& defaultName)
{
    lua_State* L = luaL_newstate();
    if (!L) return;
    luaL_openlibs(L);
    pluginapi::BindApi(L);

    if (luaL_dofile(L, path.c_str()) != LUA_OK)
    {
        TraceLog(LOG_WARNING, "Plugin '%s' failed to load: %s", path.c_str(), lua_tostring(L, -1));
        lua_close(L);
        return;
    }

    LoadedPlugin p;
    p.filePath = path;
    p.L = L;

    luabridge::LuaRef nameRef = luabridge::getGlobal(L, "PLUGIN_NAME");
    p.name = nameRef.isString() ? nameRef.cast<std::string>() : defaultName;

    luabridge::LuaRef initFn = luabridge::getGlobal(L, "Init");
    if (initFn.isFunction())
    {
        try
        {
            initFn();
        }
        catch (const luabridge::LuaException& e)
        {
            TraceLog(LOG_WARNING, "Plugin '%s' Init() error: %s", p.name.c_str(), e.what());
        }
    }

    plugins_.push_back(std::move(p));
}

void PluginManager::Update(float dt)
{
    for (auto& p : plugins_)
    {
        luabridge::LuaRef updateFn = luabridge::getGlobal(p.L, "Update");
        if (!updateFn.isFunction()) continue;
        try
        {
            updateFn(dt);
        }
        catch (const luabridge::LuaException& e)
        {
            TraceLog(LOG_WARNING, "Plugin '%s' Update() error: %s", p.name.c_str(), e.what());
        }
    }
}

void PluginManager::DrawAll()
{
    for (auto& p : plugins_)
    {
        ImGui::Begin(p.name.c_str());

        luabridge::LuaRef drawFn = luabridge::getGlobal(p.L, "DrawUI");
        if (drawFn.isFunction())
        {
            try
            {
                drawFn();
            }
            catch (const luabridge::LuaException& e)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Lua error: %s", e.what());
            }
        }
        else
        {
            ImGui::TextDisabled("This plugin defines no DrawUI().");
        }

        ImGui::End();
    }
}

void PluginManager::Shutdown()
{
    for (auto& p : plugins_)
        if (p.L) lua_close(p.L);
    plugins_.clear();
}
