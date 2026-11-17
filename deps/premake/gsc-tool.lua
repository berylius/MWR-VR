gsc_tool = {
	source = path.join(dependencies.basePath, "gsc-tool"),
}

function gsc_tool.import()
	links {"xsk-gsc-h1-catalog"}
	gsc_tool.includes()
end

function gsc_tool.includes()
	includedirs {path.join(gsc_tool.source, "include")}
end

function gsc_tool.project()
	-- The MWR host needs H1 names/IDs, not an H2 compiler/context or its VM hooks.
	project "xsk-gsc-h1-catalog"
		kind "StaticLib"
		language "C++"
		files {
			path.join(gsc_tool.source, "src/gsc/engine/h1_func.cpp"),
			path.join(gsc_tool.source, "src/gsc/engine/h1_meth.cpp"),
			path.join(gsc_tool.source, "src/gsc/engine/h1_token.cpp"),
		}
		gsc_tool.includes()
		filter "action:vs*"
			buildoptions {"/Zc:__cplusplus"}
		filter {"action:vs*", "configurations:Release"}
			disablewarnings {"4244"}
		filter {}
end

table.insert(dependencies, gsc_tool)
