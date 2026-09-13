#include "plugin/DefaultPlugins.h"
#include "app/AppPaths.h"
#include <cstdio>

namespace
{

const char* kWaveEditorLua =
R"LUAEOF(PLUGIN_NAME = "Wave Editor"

local mode = "formula"       -- "formula" or "points"
local waveName = "MyWave"
local formulaText = "math.sin(x * 2 * math.pi)"
local points = { {x=0.0,y=0.0}, {x=0.25,y=1.0}, {x=0.5,y=0.0}, {x=0.75,y=-1.0}, {x=1.0,y=0.0} }
local dragIndex = nil
local status = ""

local SAMPLES = 512

local function lerp(a, b, t) return a + (b - a) * t end

local function samplePoints(x)
    if #points == 0 then return 0.0 end
    if x <= points[1].x then return points[1].y end
    if x >= points[#points].x then return points[#points].y end
    for i = 1, #points - 1 do
        local a, b = points[i], points[i + 1]
        if x >= a.x and x <= b.x then
            local t = (b.x - a.x > 0) and (x - a.x) / (b.x - a.x) or 0
            return lerp(a.y, b.y, t)
        end
    end
    return 0.0
end

local function buildWavetable()
    local samples = {}
    for i = 0, SAMPLES - 1 do
        local x = i / SAMPLES
        local y
        if mode == "formula" then
            local f = load("local x = ...\nreturn " .. formulaText)
            local ok, result = false, nil
            if f then ok, result = pcall(f, x) end
            y = (ok and type(result) == "number") and result or 0.0
        else
            y = samplePoints(x)
        end
        if y > 1.0 then y = 1.0 end
        if y < -1.0 then y = -1.0 end
        samples[i + 1] = y
    end
    return samples
end

local function saveToFile()
    local path = ss.WavesDir() .. "/" .. waveName .. ".sswave"
    local f = io.open(path, "w")
    if not f then return false end
    if mode == "formula" then
        f:write("formula\n")
        f:write(formulaText .. "\n")
    else
        f:write("points\n")
        for _, p in ipairs(points) do
            f:write(string.format("%.6f %.6f\n", p.x, p.y))
        end
    end
    f:close()
    return true
end

local function loadFromFile(name)
    local path = ss.WavesDir() .. "/" .. name .. ".sswave"
    local f = io.open(path, "r")
    if not f then return false end
    local fileMode = f:read("l")
    if fileMode == "formula" then
        formulaText = f:read("l") or formulaText
        mode = "formula"
    elseif fileMode == "points" then
        local newPoints = {}
        for line in f:lines() do
            local x, y = line:match("([%-%d%.]+)%s+([%-%d%.]+)")
            if x and y then table.insert(newPoints, { x = tonumber(x), y = tonumber(y) }) end
        end
        if #newPoints > 0 then points = newPoints end
        mode = "points"
    end
    f:close()
    return true
end

local function registerAndSave()
    local wavetable = buildWavetable()
    ss.RegisterWaveform(waveName, wavetable)
    if saveToFile() then
        status = "Saved & registered '" .. waveName .. "'"
    else
        status = "Registered, but failed to save to disk"
    end
end

function Init()
    -- Re-register the saved wave (if any) so an instrument that references
    -- it still sounds correct after a restart -- registration only lives
    -- in memory for the running session, so this has to happen every
    -- launch. Use Load below to pull in a different saved wave by name.
    loadFromFile(waveName)
end

function DrawUI()
    waveName = ss.InputText("Wave Name", waveName)

    ss.Text("Mode:")
    ss.SameLine()
    if ss.Button(mode == "formula" and "[Formula] Points" or "Formula [Points]") then
        mode = (mode == "formula") and "points" or "formula"
    end

    if mode == "formula" then
        ss.Text("y = f(x), x in 0..1 (one cycle):")
        formulaText = ss.InputText("##formula", formulaText)
    else
        ss.Text("Click to add a point; click near an existing point to drag it.")
    end

    ss.Separator()

    local w, h = 480, 220
    ss.BeginCanvas(w, h)
    ss.DrawLine(0, h / 2, w, h / 2, 90, 90, 90, 255)

    local wave = buildWavetable()
    for i = 1, #wave - 1 do
        local x1 = (i - 1) / #wave * w
        local x2 = i / #wave * w
        local y1 = h / 2 - wave[i] * (h / 2 - 4)
        local y2 = h / 2 - wave[i + 1] * (h / 2 - 4)
        ss.DrawLine(x1, y1, x2, y2, 100, 200, 255, 255)
    end

    if mode == "points" then
        local mx, my = ss.MouseX(), ss.MouseY()
        for _, p in ipairs(points) do
            local px = p.x * w
            local py = h / 2 - p.y * (h / 2 - 4)
            ss.DrawCircle(px, py, 5, 255, 210, 90, 255)
        end

        if ss.MouseClicked() then
            local nearest, nearestDist = nil, 12
            for i, p in ipairs(points) do
                local px, py = p.x * w, h / 2 - p.y * (h / 2 - 4)
                local d = ((px - mx) ^ 2 + (py - my) ^ 2) ^ 0.5
                if d < nearestDist then nearest, nearestDist = i, d end
            end
            if nearest then
                dragIndex = nearest
            else
                local nx = math.max(0, math.min(1, mx / w))
                local ny = math.max(-1, math.min(1, (h / 2 - my) / (h / 2 - 4)))
                table.insert(points, { x = nx, y = ny })
                table.sort(points, function(a, b) return a.x < b.x end)
                for i, p in ipairs(points) do
                    if p.x == nx and p.y == ny then
                        dragIndex = i
                        break
                    end
                end
            end
        end

        if ss.MouseDown() and dragIndex then
            local p = points[dragIndex]
            if p then
                p.x = math.max(0, math.min(1, mx / w))
                p.y = math.max(-1, math.min(1, (h / 2 - my) / (h / 2 - 4)))
                table.sort(points, function(a, b) return a.x < b.x end)
            end
        end

        if not ss.MouseDown() then dragIndex = nil end
    end

    ss.EndCanvas()

    ss.Separator()
    if ss.Button("Save & Register") then
        registerAndSave()
    end
    ss.SameLine()
    if ss.Button("Load") then
        if loadFromFile(waveName) then
            status = "Loaded '" .. waveName .. "'"
        else
            status = "No saved wave named '" .. waveName .. "'"
        end
    end

    if status ~= "" then ss.Text(status) end
    ss.Text("Set an instrument's Waveform to Custom, Custom Name '" .. waveName .. "', to use it.")
end
)LUAEOF";

const char* kOscilloscopeLua =
R"LUAEOF(PLUGIN_NAME = "Oscilloscope"

local hue = 0.0

local function hsvToRgb(h, s, v)
    local i = math.floor(h * 6)
    local f = h * 6 - i
    local p = v * (1 - s)
    local q = v * (1 - f * s)
    local t = v * (1 - (1 - f) * s)
    i = i % 6
    local r, g, b
    if i == 0 then r, g, b = v, t, p
    elseif i == 1 then r, g, b = q, v, p
    elseif i == 2 then r, g, b = p, v, t
    elseif i == 3 then r, g, b = p, q, v
    elseif i == 4 then r, g, b = t, p, v
    else r, g, b = v, p, q end
    return math.floor(r * 255), math.floor(g * 255), math.floor(b * 255)
end

function DrawUI()
    ss.Text("Master output -- live, colored, a little bit extra.")

    local w, h = 480, 220
    ss.BeginCanvas(w, h)
    ss.DrawLine(0, h / 2, w, h / 2, 60, 60, 60, 255)

    local left = ss.GetMasterScopeLeft()
    local right = ss.GetMasterScopeRight()

    local n = #left
    if n > 1 then
        for i = 1, n - 1 do
            local hueL = (hue + i / n) % 1.0
            local r, g, b = hsvToRgb(hueL, 0.85, 1.0)
            local x1 = (i - 1) / n * w
            local x2 = i / n * w
            local y1 = h / 2 - left[i] * (h / 2 - 4)
            local y2 = h / 2 - left[i + 1] * (h / 2 - 4)
            ss.DrawLine(x1, y1, x2, y2, r, g, b, 255)
        end
    end

    local m = #right
    if m > 1 then
        for i = 1, m - 1 do
            local hueR = (hue + 0.5 + i / m) % 1.0
            local r, g, b = hsvToRgb(hueR, 0.85, 1.0)
            local x1 = (i - 1) / m * w
            local x2 = i / m * w
            local y1 = h / 2 - right[i] * (h / 2 - 4) * 0.6
            local y2 = h / 2 - right[i + 1] * (h / 2 - 4) * 0.6
            ss.DrawLine(x1, y1, x2, y2, r, g, b, 160)
        end
    end

    ss.EndCanvas()
end

function Update(dt)
    hue = (hue + dt * 0.15) % 1.0
end
)LUAEOF";

const char* kInstrumentEditorLua =
R"LUAEOF(PLUGIN_NAME = "Instrument Editor"

-- Every value of the selected instrument as a typed text box instead of a
-- slider. Sliders are quick but they can't hit an exact number and they
-- can't leave the range whoever wrote the panel picked -- this can do
-- both. Type 2.5 into Attack for a slow pad swell, 45 into Arp Rate, 30
-- into Env2 Amount for a drum pitch-drop, and so on. Only the handful of
-- values the audio engine genuinely can't accept get clamped (levels to
-- 0..1, pan to -1..1, times to >= 0, duty to 0.01..0.99).
--
-- Everything here edits the live instrument -- the same one the built-in
-- Instrument panel shows -- and pushes a proper undo snapshot the moment
-- you click into a field, so Ctrl+Z behaves exactly like it does anywhere
-- else in the editor.

local WAVEFORMS  = { "Square", "Triangle", "Sawtooth", "Sine", "Noise", "Custom" }
local CATEGORIES = { "Lead", "Bass", "Percussion", "Pad", "FX" }
local ARPS       = { "Off", "Octave", "Major Chord", "Minor Chord" }
local FILTERS    = { "None", "Low Pass", "High Pass" }
local ENV2       = { "None", "Pitch", "Filter Cutoff" }

local followSelection = true
local trackIndex      = 0
local keepTrackName   = true
local presetName      = "MyPatch"
local presetFilter    = ""
local status          = ""
local presets         = {}

local function refreshPresets()
    presets = ss.ListPresets()
end

-- ---------------------------------------------------------------
-- Field helpers. Each reads the live value, draws the widget, pushes an
-- undo snapshot on the frame the widget takes focus (before any edit has
-- landed -- otherwise undo would restore the already-changed state), and
-- writes back only on an actual change.
-- ---------------------------------------------------------------

local function numberField(label, idx, field)
    local current = ss.GetNumber(idx, field)
    local v = ss.InputFloat(label, current)
    if ss.IsItemActivated() then ss.BeginEdit() end
    if v ~= current then ss.SetNumber(idx, field, v) end
end

local function enumField(label, idx, field, items)
    local current = ss.GetInt(idx, field)
    local v = ss.Combo(label, current, items)
    if ss.IsItemActivated() then ss.BeginEdit() end
    if v ~= current then ss.SetInt(idx, field, v) end
end

local function boolField(label, idx, field)
    local current = ss.GetInt(idx, field) ~= 0
    local v = ss.Checkbox(label, current)
    if v ~= current then
        ss.BeginEdit()
        ss.SetInt(idx, field, v and 1 or 0)
    end
end

local function textField(label, idx, field)
    local current = ss.GetText(idx, field)
    local v = ss.InputText(label, current)
    if ss.IsItemActivated() then ss.BeginEdit() end
    if v ~= current then ss.SetText(idx, field, v) end
end

function Init()
    refreshPresets()
end

function DrawUI()
    local count = ss.InstrumentCount()
    if count <= 0 then
        ss.TextDisabled("No instruments in this song yet.")
        return
    end

    -- ---- which track are we editing ----
    followSelection = ss.Checkbox("Follow track selection", followSelection)
    if followSelection then
        trackIndex = ss.SelectedInstrument()
    end
    if trackIndex >= count then trackIndex = count - 1 end
    if trackIndex < 0 then trackIndex = 0 end

    local names = {}
    for i = 0, count - 1 do
        names[i + 1] = string.format("%d: %s", i + 1, ss.GetText(i, "name"))
    end

    local picked = ss.Combo("Track", trackIndex, names)
    if picked ~= trackIndex then
        trackIndex = picked
        if followSelection then ss.SelectInstrument(trackIndex) end
    end

    local idx = trackIndex
    ss.Separator()

    -- ---- identity & oscillator ----
    textField("Name", idx, "name")
    enumField("Waveform", idx, "waveform", WAVEFORMS)

    local waveform = ss.GetInt(idx, "waveform")
    if waveform == 0 then -- Square
        numberField("Duty Cycle", idx, "dutyCycle")
        ss.TextDisabled("0.01 - 0.99. 0.5 is a plain square; 0.125 is the classic thin pulse.")
    elseif waveform == 5 then -- Custom
        textField("Custom Wave", idx, "customWaveform")
        local waveName = ss.GetText(idx, "customWaveform")
        if ss.HasCustomWaveform(waveName) then
            ss.TextDisabled("Wavetable '" .. waveName .. "' is registered.")
        else
            ss.TextColored(255, 180, 80, 255,
                "No wavetable named '" .. waveName .. "' -- this instrument will be silent.")
        end
    end

    ss.SeparatorText("Mix")
    numberField("Volume", idx, "volume")
    numberField("Pan", idx, "pan")
    boolField("Mute", idx, "muted")
    ss.SameLine()
    boolField("Solo", idx, "solo")
    enumField("Category", idx, "category", CATEGORIES)

    ss.SeparatorText("Amplitude Envelope")
    numberField("Attack (s)", idx, "attack")
    numberField("Decay (s)", idx, "decay")
    numberField("Sustain (0-1)", idx, "sustain")
    numberField("Release (s)", idx, "release")

    ss.SeparatorText("Arpeggiator")
    enumField("Arp Pattern", idx, "arpPattern", ARPS)
    numberField("Arp Rate (notes/s)", idx, "arpRate")

    ss.SeparatorText("Vibrato")
    boolField("Vibrato Enabled", idx, "vibratoEnabled")
    numberField("Vibrato Rate (Hz)", idx, "vibratoRate")
    numberField("Vibrato Depth (semitones)", idx, "vibratoDepth")

    ss.SeparatorText("Filter")
    enumField("Filter Type", idx, "filterType", FILTERS)
    numberField("Cutoff (0-1)", idx, "filterCutoff")
    -- The engine maps cutoff exponentially over 20Hz..20kHz, so showing the
    -- actual frequency turns an abstract 0..1 into something you can aim at.
    local cutoff = ss.GetNumber(idx, "filterCutoff")
    ss.TextDisabled(string.format("~= %.0f Hz", 20.0 * (1000.0 ^ cutoff)))

    ss.SeparatorText("Second Envelope")
    enumField("Env2 Target", idx, "env2Target", ENV2)
    numberField("Env2 Attack (s)", idx, "env2Attack")
    numberField("Env2 Decay (s)", idx, "env2Decay")
    numberField("Env2 Sustain (0-1)", idx, "env2Sustain")
    numberField("Env2 Release (s)", idx, "env2Release")
    numberField("Env2 Amount", idx, "env2Amount")
    local target = ss.GetInt(idx, "env2Target")
    if target == 1 then
        ss.TextDisabled("Semitones of peak pitch deviation. Try 30 with a 0.05s decay for a kick drum.")
    elseif target == 2 then
        ss.TextDisabled("Extra 0-1 cutoff at the envelope's peak. Negative sweeps the filter closed.")
    end

    -- ---- preset library ----
    ss.SeparatorText("Preset Library")
    presetName = ss.InputText("Save As", presetName)
    if ss.Button("Save Preset") then
        local path = ss.InstrumentsDir() .. "/" .. presetName .. ".ssip"
        if ss.SavePreset(idx, path) then
            status = "Saved '" .. presetName .. "'"
            refreshPresets()
        else
            status = "Save failed."
        end
    end
    ss.SameLine()
    if ss.Button("Refresh List") then
        refreshPresets()
        status = #presets .. " presets found."
    end

    keepTrackName = ss.Checkbox("Keep track name when loading", keepTrackName)
    presetFilter = ss.InputText("Search", presetFilter)

    ss.BeginChildRegion("preset_list", 0, 200)
    local needle = presetFilter:lower()
    local shown = 0
    for i, name in ipairs(presets) do
        if needle == "" or name:lower():find(needle, 1, true) then
            shown = shown + 1
            if ss.Selectable(name, false) then
                local path = ss.InstrumentsDir() .. "/" .. name .. ".ssip"
                if ss.LoadPreset(idx, path, keepTrackName) then
                    status = "Loaded '" .. name .. "'"
                    presetName = name
                else
                    status = "Failed to load '" .. name .. "'"
                end
            end
        end
    end
    if shown == 0 then
        ss.TextDisabled("Nothing matches -- clear the search box.")
    end
    ss.EndChildRegion()

    if status ~= "" then ss.TextDisabled(status) end
end
)LUAEOF";

// Writes `content` to `path`. Unless overwrite is set, a file that already
// exists is left exactly as it is -- never clobber something the user has
// since edited or replaced.
bool WriteScript(const std::string& path, const char* content, bool overwrite)
{
    if (!overwrite)
    {
        FILE* existing = fopen(path.c_str(), "r");
        if (existing)
        {
            fclose(existing);
            return true; // already installed; nothing to do, and not an error
        }
    }

    FILE* f = fopen(path.c_str(), "w");
    if (!f) return false;
    fputs(content, f);
    return fclose(f) == 0;
}

struct BundledPlugin
{
    const char* file;
    const char* name;
    const char* description;
    const char* source;
};

const BundledPlugin kBundledPlugins[] =
{
    { "InstrumentEditor", "Instrument Editor",
      "Every instrument value as an exact text field instead of a slider, plus a preset browser.",
      kInstrumentEditorLua },
    { "WaveEditor", "Wave Editor",
      "Draw or type a formula for a custom oscillator waveform and register it by name.",
      kWaveEditorLua },
    { "Oscilloscope", "Oscilloscope",
      "A colored live scope of the master output.",
      kOscilloscopeLua },
};

} // namespace

int DefaultPluginCount()
{
    return (int)(sizeof(kBundledPlugins) / sizeof(kBundledPlugins[0]));
}

DefaultPluginInfo DefaultPluginAt(int index)
{
    DefaultPluginInfo info{};
    if (index < 0 || index >= DefaultPluginCount()) return info;

    const BundledPlugin& p = kBundledPlugins[index];
    info.file        = p.file;
    info.name        = p.name;
    info.description = p.description;
    return info;
}

bool WriteDefaultPlugin(const std::string& fileStem, bool overwrite)
{
    for (const BundledPlugin& p : kBundledPlugins)
    {
        if (fileStem != p.file) continue;
        return WriteScript(AppPaths::PluginsDir() + "/" + p.file + ".lua", p.source, overwrite);
    }
    return false; // no bundled plugin by that name
}

void WriteDefaultPluginsIfMissing()
{
    for (const BundledPlugin& p : kBundledPlugins)
        WriteDefaultPlugin(p.file, /*overwrite=*/false);
}
