-- Only MWR support providers enter the shipping target. Shared VR modules keep
-- their upstream boundaries; the compile-time component gate admits constructors.
return function()
	local support = {
		"arxan", "camera", "command", "console", "d3d11", "dvars", "exception",
		"fastfiles", "filesystem", "game_data", "game_module", "game_text", "input",
		"logger", "lui", "scene_models", "scene_rigid_part", "scene_skeletal_model", "scene_surface_storage",
		"scheduler", "scripting", "steam_proxy", "survey_heap", "thread_names", "ui_scripting"
	}
	local admitted = {}
	for _, name in ipairs(support) do admitted[name .. ".cpp"] = true end
	for _, file in ipairs(os.matchfiles("src/client/component/*.cpp")) do
		if not admitted[path.getname(file)] then removefiles {file} end
	end
	removefiles {"src/client/component/gui/**.cpp", "src/client/component/gsc/**.cpp", "src/client/game/scripting/lua/**.cpp",
		"src/client/component/vr/gameplay/campaign/ending/runtime.cpp"}
	defines {"MWR_NATIVE_HOST"}
end
