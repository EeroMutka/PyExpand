
function specify_warnings()
	flags "FatalWarnings" -- treat all warnings as errors
	buildoptions "/w14062" -- error on unhandled enum members in switch cases
	buildoptions "/w14456" -- error on shadowed locals
	buildoptions "/wd4101" -- allow unused locals
	linkoptions "-IGNORE:4099" -- disable linker warning: "PDB was not found ...; linking object as if no debug info"
end

workspace "PyExpand"
	architecture "x64"
	configurations { "Debug", "Release" }
	location("." .. _ACTION)

project "PyExpand"
	kind "ConsoleApp"
	language "C++"
	cppdialect "C++20"
	targetdir ".build"
	
	specify_warnings()
	
	includedirs "."
	files "src/**"
	
	filter "configurations:Debug"
		symbols "On"

	filter "configurations:Release"
		optimize "On"
