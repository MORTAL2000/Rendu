local sep = "/"
local ext = ""
local copyFix = ""
if os.ishost("windows") then
	sep = "\\"
	ext = ".exe"
	copyFix = "*"
end
cwd = os.getcwd()

-- Workspace definition.

workspace("Rendu")
	-- Configurations
	configurations({ "Release", "Dev"})
	location("build")
	targetdir ("build/%{prj.name}/%{cfg.longname}")
	debugdir ("build/%{prj.name}/%{cfg.longname}")
	architecture("x64")

	-- Configuration specific settings.
	filter("configurations:Release")
		defines({ "NDEBUG" })
		optimize("On")
		flags({ "NoIncrementalLink", "LinkTimeOptimization" })

	filter("configurations:Dev")
		defines({ "DEBUG" })
		symbols("On")

	filter({})
	startproject("ALL")


-- Helper functions for the projects.

function InstallProject(projectName, destination)
	filter("configurations:Release")
		postbuildcommands({
			path.translate( "{CHDIR} "..os.getcwd(), sep),
			path.translate( "{COPY} build/"..projectName.."/Release/"..projectName..ext.." "..destination..copyFix, sep)
		})
	filter("configurations:Dev")
		postbuildcommands({
			path.translate( "{CHDIR} "..os.getcwd(), sep),
			path.translate( "{COPY} build/"..projectName.."/Dev/"..projectName..ext.." "..destination..copyFix, sep)
		})
	filter({})
end

function CPPSetup()
	language("C++")
	cppdialect("C++11")
	systemversion("latest")

	filter("toolset:not msc*")
		buildoptions({ "-Wall", "-Wextra" })
	filter("toolset:msc*")
		buildoptions({ "-W4"})
	filter({})

end	

function GraphicsSetup(srcDir)
	CPPSetup()

	libDir = srcDir.."/libs/"
	-- To support angled brackets in Xcode.
	sysincludedirs({ libDir, libDir.."glfw/include/" })

	-- Libraries for each platform.
	if os.istarget("macosx") then
		links({"glfw3", "nfd", "OpenGL.framework", "Cocoa.framework", "IOKit.framework", "CoreVideo.framework", "AppKit.framework"})
	elseif os.istarget("windows") then
		links({"glfw3", "nfd", "opengl32", "comctl32"})
	else -- Assume linux
		links({"glfw3", "nfd", "GL", "X11", "Xi", "Xrandr", "Xxf86vm", "Xinerama", "Xcursor", "rt", "m", "pthread", "dl", "gtk+-3.0"})
	end

end

function ShaderValidation()
	-- Run the shader validator on all existing shaders.
	-- Output IDE compatible error messages.
	dependson({"ShaderValidator"})
	prebuildcommands({ 
		-- Move to the build directory.
		path.translate("{CHDIR} "..os.getcwd().."/build", sep),
		-- Run the shader validator on the resources directory.
		path.translate( "./shader_validator"..ext.." "..cwd.."/resources/", sep)
	})
end	

function RegisterSourcesAndShaders(srcPath, shdPath)
	files({ srcPath, shdPath })
	removefiles({"**.DS_STORE", "**.thumbs"})
	-- Reorganize file hierarchy in the IDE project.
	vpaths({
	   ["*"] = {srcPath},
	   ["Shaders/*"] = {shdPath}
	})
end

function AppSetup(appName)
	GraphicsSetup("src")
	includedirs({ "src/engine" })
	links({"Engine"})
	kind("ConsoleApp")
	ShaderValidation()
	-- Declare src and resources files.
	srcPath = "src/apps/"..appName.."/**"
	rscPath = "resources/"..appName.."/shaders/**"
	RegisterSourcesAndShaders(srcPath, rscPath)
end	

function ToolSetup(toolName)
	GraphicsSetup("src")
	includedirs({ "src/engine" })
	links({"Engine"})
	kind("ConsoleApp")
	ShaderValidation()
end	

-- Projects

project("Engine")
	GraphicsSetup("src")
	includedirs({ "src/engine" })
	kind("StaticLib")
	-- Some additional files (README, scenes) are hidden, but you can display them in the project by uncommenting them below.
	files({ "src/engine/**.hpp", "src/engine/**.cpp",
			"resources/common/shaders/**",
			"src/libs/*/*.hpp", "src/libs/*/*.cpp", "src/libs/*/*.h",
			"premake5.lua", 
			"README.md",
	--		"resources/**.scene"
	})
	removefiles { "src/libs/nfd/*" }
	removefiles { "src/libs/glfw/*" }
	removefiles({"**.DS_STORE", "**.thumbs"})
	-- Virtual path allow us to get rid of the on-disk hierarchy.
	vpaths({
	   ["Engine/*"] = {"src/engine/**"},
	   ["Shaders/*"] = {"resources/common/shaders/**"},
	   ["Libraries/*"] = {"src/libs/**"},
	   [""] = { "*.*" },
	-- ["Scenes/*"] = {"resources/**.scene"},
	})


group("Apps")

project("PBRDemo")
	AppSetup("pbrdemo")
	
project("Playground")
	AppSetup("playground")

project("Atmosphere")
	AppSetup("atmosphere")

project("SnakeGame")
	AppSetup("snakegame")

project("PathTracer")
	AppSetup("pathtracer")

project("ImageFiltering")
	AppSetup("imagefiltering")


group("Tools")

project("AtmosphericScatteringEstimator")
	ToolSetup()
	files({ "src/tools/AtmosphericScatteringEstimator.cpp" })

project("BRDFEstimator")
	ToolSetup()
	includedirs({ "src/apps/pbrdemo" })
	files({ "src/tools/BRDFEstimator.cpp" })

project("ControllerTest")
	ToolSetup()
	files({ "src/tools/ControllerTest.cpp" })

project("ImageViewer")
	ToolSetup()
	RegisterSourcesAndShaders("src/tools/ImageViewer.cpp", "resources/imageviewer/shaders/**")

project("ObjToScene")
	ToolSetup()
	files({ "src/tools/objtoscene/*.cpp", "src/tools/objtoscene/*.hpp" })

project("ShaderValidator")
	GraphicsSetup("src")
	includedirs({ "src/engine" })
	links({"Engine"})
	kind("ConsoleApp")
	files({ "src/tools/ShaderValidator.cpp" })
	-- Install the shader validation utility in the root build directory.
	InstallProject("%{prj.name}", "build/shader_validator"..ext)
	filter({})


group("Meta")

project("ALL")
	CPPSetup()
	kind("ConsoleApp")
	dependson( {"Engine", "PBRDemo", "Playground", "Atmosphere", "ImageViewer", "ImageFiltering", "AtmosphericScatteringEstimator", "BRDFEstimator", "ControllerTest", "SnakeGame", "PathTracer", "ObjToScene"})

-- Include NFD premake file.

include("src/libs/nfd/premake5.lua")
include("src/libs/glfw/premake5.lua")

-- Actions

newaction {
   trigger     = "clean",
   description = "Clean the build directory",
   execute     = function ()
      print("Cleaning...")
      os.rmdir("./build")
      print("Done.")
   end
}

newaction {
   trigger     = "docs",
   description = "Build the documentation using Doxygen",
   execute     = function ()
      print("Generating documentation...")
      os.execute("doxygen"..ext.." docs/Doxyfile")
      print("Done.")
   end
}

-- Internal private projects can be added here.
if os.isfile("src/internal/premake5.lua") then
	include("src/internal/premake5.lua")
end
	
