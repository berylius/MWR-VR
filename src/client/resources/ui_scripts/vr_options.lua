-- MWR VR: Options > VR. Every VR setting this build registers, grouped like
-- the launcher (game:vrsettings reads the launcher schema and locales).
-- Values are the same saved dvars the launcher edits.
local language = 0
pcall(function()
    language = Engine.GetCurrentLanguage()
end)

local function t(key)
    return game:vrsettingtext(language, key)
end

local korean = language == 15 -- LANGUAGE_KOREAN
local extra = korean and {
    button_desc = "VR 설정을 게임 안에서 바꿉니다.",
    measure = "지금 키 측정",
    measure_desc = "T자 자세로 서서 양손 트리거를 게이지가 찰 때까지 2초간 누르세요. 키·어깨·팔 길이를 함께 측정합니다. 메뉴 밖에서는 X 버튼을 3초 누르면 측정 화면을 엽니다."
} or {
    button_desc = "Change VR settings in game.",
    measure = "Measure height now",
    measure_desc = "Stand in a T-pose and hold both triggers for 2 seconds until the gauge fills. Measures height, shoulders and arm length together. Outside menus, hold X for 3 seconds to open the measurement screen."
}

local settings = game:vrsettings(language)
local current_category = nil

local function get(name)
    return game:vrsettingget(name)
end

local function visible(entry)
    if entry.whenName ~= "" and get(entry.whenName) ~= entry.whenValue then return false end
    if entry.conditions then
        for _, condition in ipairs(entry.conditions) do
            if get(condition.name) ~= condition.value then return false end
        end
    end
    return true
end

local function controls_visibility(name)
    for i = 1, #settings do
        if settings[i].whenName == name then
            return true
        end
        if settings[i].conditions then
            for _, condition in ipairs(settings[i].conditions) do
                if condition.name == name then return true end
            end
        end
    end
    return false
end

local function decimals(entry)
    local step = entry.step / entry.scale
    local digits = 0
    while step < 0.999 and digits < 3 do
        step = step * 10
        digits = digits + 1
    end
    return digits
end

local function number_text(entry)
    local value = (tonumber(get(entry.name)) or 0) / entry.scale
    return string.format("%." .. decimals(entry) .. "f", value)
end

local function reopen(menu)
    LUI.FlowManager.RequestLeaveMenu(menu)
    LUI.FlowManager.RequestAddMenu(nil, "vr_options_category")
end

local function changed(menu, entry)
    if controls_visibility(entry.name) then
        reopen(menu)
    end
end

local function add_toggle(menu, entry)
    local function on()
        local value = get(entry.name)
        return value ~= "" and value ~= "0"
    end
    local function flip()
        game:vrsettingset(entry.name, on() and "0" or tostring(entry.enabledLevel))
        changed(menu, entry)
    end
    LUI.Options.AddButtonOptionVariant(menu, GenericButtonSettings.Variants.Select, entry.label, entry.description,
        function()
            return t(on() and "choice.enabled" or "choice.disabled")
        end, flip, flip)
end

local function add_choice(menu, entry)
    local function index()
        local value = get(entry.name)
        for i = 1, #entry.options do
            if entry.options[i].value == value then
                return i
            end
        end
        return 1
    end
    local function step(delta)
        local count = #entry.options
        local next_index = ((index() - 1 + delta) % count) + 1
        game:vrsettingset(entry.name, entry.options[next_index].value)
        changed(menu, entry)
    end
    LUI.Options.AddButtonOptionVariant(menu, GenericButtonSettings.Variants.Select, entry.label, entry.description,
        function()
            return entry.options[index()].label
        end, function()
            step(-1)
        end, function()
            step(1)
        end)
end

local function add_number(menu, entry)
    local function step(delta)
        local value = tonumber(get(entry.name)) or entry.min
        local steps = math.floor((value - entry.min) / entry.step + 0.5) + delta
        local next_value = math.max(entry.min, math.min(entry.max, entry.min + steps * entry.step))
        game:vrsettingset(entry.name, string.format("%.6g", next_value))
    end
    LUI.Options.AddButtonOptionVariant(menu, GenericButtonSettings.Variants.Select, entry.label, entry.description,
        function()
            return number_text(entry)
        end, function()
            step(-1)
        end, function()
            step(1)
        end)
end

local function add_divider(menu, label)
    if not LUI.MenuBuilder.m_types_build["h1_option_menu_titlebar"] or label == "" then
        return
    end
    local element = LUI.UIElement.new({
        leftAnchor = true,
        rightAnchor = true,
        left = 0,
        right = 0,
        topAnchor = true,
        bottomAnchor = false,
        top = 0,
        bottom = 33.33
    })
    element.scrollingToNext = true
    element:addElement(LUI.MenuBuilder.BuildRegisteredType("h1_option_menu_titlebar", {
        title_bar_text = label
    }))
    menu.list:addElement(element)
end

local measure_text = korean and {
    title = "키 · 팔 길이 측정",
    instruction = "똑바로 서서 두 팔을 어깨 높이로 쭉 뻗으세요",
    states = {
        "양손 트리거를 놓은 뒤 시작하세요",
        "T자 자세로 양손 트리거를 계속 누르세요",
        "자세를 유지하세요 · 측정 중",
        "똑바로 서세요 · VR 바닥 높이 설정도 확인하세요",
        "헤드셋과 양손 컨트롤러의 추적을 확인하세요",
        "게임에 들어가 손이 표시된 뒤 측정해 주세요",
        "측정 완료"
    },
    result = "키 %s cm  ·  팔 길이 %.1f cm",
    restart = "다시 측정"
} or {
    title = "Height & arm length",
    instruction = "Stand upright with both arms straight out at shoulder height",
    states = {
        "Release both triggers to begin",
        "Hold both triggers in a T-pose",
        "Keep your pose · measuring",
        "Stand upright · check your VR floor setup",
        "Check headset and both controller tracking",
        "Enter the game and let your hands appear first",
        "Measurement complete"
    },
    result = "Height %s cm  ·  Arm length %.1f cm",
    restart = "Measure again"
}

-- Native LUI has no SVG material loader. These merged white-image spans are
-- generated from height-tpose.svg and embedded with the menu resources.
LUI.MenuBuilder.registerType("vr_height_measure", function(root)
    if Engine.GetDvarInt("vr_heightState") == 0 or Engine.GetDvarInt("vr_heightState") == 7 then
        Engine.ExecNow("vr_measure_height begin")
    end
    local menu = LUI.MenuTemplate.new(root, {menu_title = measure_text.title, uppercase_title = true})
    local function bounds(left, top, right, bottom)
        return {leftAnchor = false, rightAnchor = false, topAnchor = false, bottomAnchor = false,
            left = left, right = right, top = top, bottom = bottom}
    end
    local panel = LUI.UIElement.new(bounds(-300, -200, 300, 210))
    menu:addElement(panel)
    local function rectangle(left, top, right, bottom, color)
        local properties = bounds(left, top, right, bottom)
        properties.material = RegisterMaterial("white")
        properties.color = color
        local image = LUI.UIImage.new(properties)
        panel:addElement(image)
        return image
    end
    local function label(top, bottom, size)
        local properties = bounds(-300, top, 300, bottom)
        properties.font = CoD.TextSettings.BodyFont.Font
        properties.fontSize = size
        properties.alignment = LUI.Alignment.Center
        local text = LUI.UIText.new(properties)
        panel:addElement(text)
        return text
    end
    local instruction = label(-190, -165, 20)
    instruction:setText(measure_text.instruction)
    for _, span in ipairs(VRHeightSilhouette) do
        rectangle((span[1]-160)*0.8, -150+span[2]*0.8,
            (span[3]-160)*0.8, -150+span[4]*0.8, {r = 0.86, g = 0.9, b = 0.94})
    end
    rectangle(-180, 108, 180, 120, {r = 0.17, g = 0.2, b = 0.24})
    local gauge = rectangle(-180, 108, -180, 120, {r = 0.34, g = 0.85, b = 0.65})
    local status = label(134, 158, 20)
    local result = label(166, 190, 20)
    local function refresh()
        Engine.ExecNow("vr_measure_height tick")
        local state = Engine.GetDvarInt("vr_heightState")
        local progress = math.max(0, math.min(1000, Engine.GetDvarInt("vr_heightProgress")))
        gauge:registerAnimationState("fill", bounds(-180, 108, -180+360*progress/1000, 120))
        gauge:animateToState("fill", 0)
        status:setText((measure_text.states[state] or measure_text.states[2]) ..
            (state == 3 and string.format("  %d%%", math.floor(progress/10)) or ""))
        result:setText(state == 7 and string.format(measure_text.result,
            get("vr_playerHeight"), (tonumber(get("vr_measuredArmLength")) or 0)*100) or "")
    end
    menu:registerEventHandler("vr_height_refresh", refresh)
    menu:addElement(LUI.UITimer.new(50, "vr_height_refresh"))
    menu:registerEventHandler("menu_close", function()
        Engine.ExecNow("vr_measure_height cancel")
    end)
    menu:AddButton(measure_text.restart, function() Engine.ExecNow("vr_measure_height begin") end)
    -- Keep the restart button below the measurement panel.
    menu.list:registerAnimationState("height_footer", bounds(-180, 225, 180, 265))
    menu.list:animateToState("height_footer", 0)
    menu:AddBackButton()
    refresh()
    return menu
end)

local function add_measure(menu)
    menu:AddButton(extra.measure, function()
        LUI.FlowManager.RequestAddMenu(nil, "vr_height_measure")
    end, nil, true, nil, {
        desc_text = extra.measure_desc
    })
end

LUI.MenuBuilder.registerType("vr_options_category", function(root)
    local title = "VR"
    for i = 1, #settings do
        if settings[i].category == current_category then
            title = settings[i].categoryLabel
            break
        end
    end
    local menu = LUI.MenuTemplate.new(root, {
        menu_title = title,
        uppercase_title = true,
        menu_top_indent = LUI.MenuTemplate.spMenuOffset,
        menu_width = GenericMenuDims.OptionMenuWidth
    })
    local group = nil
    for i = 1, #settings do
        local entry = settings[i]
        if entry.category == current_category and visible(entry) then
            if entry.group ~= group then
                group = entry.group
                add_divider(menu, entry.groupLabel)
            end
            if entry.type == "toggle" then
                add_toggle(menu, entry)
            elseif entry.type == "choice" then
                add_choice(menu, entry)
            else
                add_number(menu, entry)
            end
            if entry.name == "vr_playerHeight" then
                add_measure(menu)
            end
        end
    end
    LUI.Options.InitScrollingList(menu.list, nil, {
        rows = 11
    })
    LUI.Options.AddOptionTextInfo(menu)
    menu:AddBackButton()
    return menu
end)

LUI.MenuBuilder.registerType("vr_options_menu", function(root)
    local menu = LUI.MenuTemplate.new(root, {
        menu_title = "VR",
        uppercase_title = true
    })
    local seen = {}
    for i = 1, #settings do
        local category = settings[i].category
        if not seen[category] then
            seen[category] = true
            menu:AddButton(settings[i].categoryLabel, function()
                current_category = category
                LUI.FlowManager.RequestAddMenu(nil, "vr_options_category")
            end, nil, true, nil, {
                desc_text = extra.button_desc
            })
        end
    end
    LUI.Options.InitScrollingList(menu.list, nil)
    LUI.Options.AddOptionTextInfo(menu)
    menu:AddBackButton()
    return menu
end)

LUI.addmenubutton("pc_controls", {
    text = "VR",
    description = extra.button_desc,
    callback = function()
        LUI.FlowManager.RequestAddMenu(nil, "vr_options_menu")
    end
})

print("[VR options] loaded; settings=" .. #settings .. " language=" .. tostring(language) .. " titlebar=" ..
    tostring(LUI.MenuBuilder.m_types_build["h1_option_menu_titlebar"] ~= nil))
