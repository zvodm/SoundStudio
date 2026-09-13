PLUGIN_NAME = "Oscilloscope"

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
    ss.Text("Master output")

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
