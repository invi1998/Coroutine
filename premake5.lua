include "./vendor/premake/premake_customization/solution_items.lua"
include "Dependencies.lua"

workspace "Coroutine"
	architecture "x86_64"
	startproject "INVIEngine"

	configurations
	{
		"Debug",
		"Release",
		"Dist"
	}

	solution_items
	{
		".editorconfig"
	}

	flags
	{
		"MultiProcessorCompile"
	}

outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

group "Core"
	include "INVI_Coroutine"


