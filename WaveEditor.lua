PLUGIN_NAME = "Wave Editor"

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
