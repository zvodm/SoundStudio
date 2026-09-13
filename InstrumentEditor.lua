PLUGIN_NAME = "Instrument Editor"

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
