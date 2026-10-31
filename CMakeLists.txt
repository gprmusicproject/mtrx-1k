cmake_minimum_required(VERSION 3.22)

# CMake counterpart of Retromulator.jucer, which only ships an Xcode exporter.
# The source list, include paths and definitions mirror the .jucer project.

if(MSVC OR WIN32)
	cmake_policy(SET CMP0091 NEW)
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

project(Retromulator VERSION 1.0.0 LANGUAGES C CXX)

# shown after the number in the About page; the file versions of Windows take numbers only
set(MTRX_VERSION_SUFFIX " alpha 3")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(RETROMULATOR_JUCE_PATH "${CMAKE_CURRENT_LIST_DIR}/Modules/JUCE" CACHE PATH "Path to a JUCE checkout")
option(RETROMULATOR_LTO "Link time optimisation for release builds" OFF)

if(NOT EXISTS "${RETROMULATOR_JUCE_PATH}/CMakeLists.txt")
	message(FATAL_ERROR "JUCE not found at ${RETROMULATOR_JUCE_PATH}. Set RETROMULATOR_JUCE_PATH to a JUCE checkout.")
endif()

add_subdirectory("${RETROMULATOR_JUCE_PATH}" JUCE EXCLUDE_FROM_ALL)
juce_add_module("${CMAKE_CURRENT_LIST_DIR}/Modules/SFZero")

# Headers the upstream CMake projects generate next to their sources
set(SYNTHLIB_DEMO_MODE OFF)
configure_file(source/synthLib/buildconfig.h.in ${CMAKE_CURRENT_LIST_DIR}/source/synthLib/buildconfig.h)
configure_file(source/jucePluginLib/version.h.in ${CMAKE_CURRENT_LIST_DIR}/source/jucePluginLib/version.h)
include(source/jucePluginLib/createVersionDateTime.cmake)

include(cmake/RetromulatorSources.cmake)

if(WIN32)
	set(RETROMULATOR_FORMATS VST3 Standalone)
elseif(APPLE)
	set(RETROMULATOR_FORMATS VST3 AU Standalone)
else()
	set(RETROMULATOR_FORMATS VST3 Standalone)
endif()

juce_add_plugin(Retromulator
	# MTRX-1K is a derivative of Retromulator with its own identity: hosts see a
	# different plugin, so the two can be installed side by side. The CMake target keeps
	# the upstream name.
	COMPANY_NAME                "GPR Music Project"
	PLUGIN_MANUFACTURER_CODE    Gprm
	PLUGIN_CODE                 Ts1k
	FORMATS                     ${RETROMULATOR_FORMATS}
	PRODUCT_NAME                "MTRX-1K"
	DESCRIPTION                 "Six voice analog synthesizer emulation"
	BUNDLE_ID                   "com.gprmusicproject.mtrx1k"
	# the maker's symbol: the icon of the standalone app and of its window
	ICON_BIG                    "${CMAKE_CURRENT_LIST_DIR}/Assets/Icon/MTRX-1K.png"

	IS_SYNTH                    TRUE
	NEEDS_MIDI_INPUT            TRUE
	NEEDS_MIDI_OUTPUT           FALSE
	IS_MIDI_EFFECT              FALSE
	EDITOR_WANTS_KEYBOARD_FOCUS FALSE

	VST3_CATEGORIES             Instrument Synth
	AU_MAIN_TYPE                kAudioUnitType_MusicDevice
	AU_EXPORT_PREFIX            Mtrx1KAU

	COPY_PLUGIN_AFTER_BUILD     FALSE
)

juce_add_binary_data(RetromulatorData SOURCES ${RETROMULATOR_RESOURCES})

# Rack panel and patch editor of the GPL editor, not listed in the .jucer
target_sources(Retromulator PRIVATE ${RETROMULATOR_SOURCES} source/plugin/RackPanel.cpp source/plugin/EditPanel.cpp source/plugin/MidiLearn.cpp)

# The effects after the instrument, all MTRX-1K's own
target_sources(Retromulator PRIVATE source/fxLib/fxChain.cpp source/fxLib/fxOwn.cpp)

target_include_directories(Retromulator PRIVATE
	source
	source/plugin
	source/bridge
	source/ronaldo
	source/ronaldo/custom_chips
	source/cpu
	source/lameLib
	source/dsp56300/source
	source/dsp56300/source/dsp56kBase
	source/dsp56300/source/dsp56kEmu
	source/dsp56300/source/asmjit/src
	source/mc68k
	source/mc68k/Musashi
	source/mc68k/Musashi/softfloat
)

target_compile_definitions(Retromulator
	PUBLIC
		JUCE_WEB_BROWSER=0
		JUCE_USE_CURL=0
		JUCE_DISPLAY_SPLASH_SCREEN=0
		JUCE_REPORT_APP_USAGE=0
		JUCE_STRICT_REFCOUNTEDPOINTER=1
		JUCE_MODAL_LOOPS_PERMITTED=1
		JUCE_IGNORE_VST3_MISMATCHED_PARAMETER_ID_WARNING=1
		JUCE_USE_CUSTOM_PLUGIN_STANDALONE_ENTRYPOINT=1
		JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1
		JUCE_VST3_CAN_REPLACE_VST2=0
	PRIVATE
		ASMJIT_STATIC=1
		OUTSIDE_SPEEX=1
		FLOATING_POINT=1
		RANDOM_PREFIX=synthLib_speex
)

# ASIO drivers in the standalone app. The headers come with JUCE; of the two licences
# Steinberg gives them under, this project takes the GPL v3.
if(WIN32)
	target_compile_definitions(Retromulator PUBLIC JUCE_ASIO=1)
endif()

if(MSVC)
	target_compile_options(Retromulator PRIVATE /bigobj)
	target_compile_definitions(Retromulator PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS _USE_MATH_DEFINES)
else()
	target_compile_options(Retromulator PRIVATE -Wno-deprecated-declarations)
endif()

target_link_libraries(Retromulator
	PRIVATE
		RetromulatorData
		SFZero
		juce::juce_audio_basics
		juce::juce_audio_devices
		juce::juce_audio_formats
		juce::juce_audio_plugin_client
		juce::juce_audio_processors
		juce::juce_audio_utils
		juce::juce_core
		juce::juce_cryptography
		juce::juce_data_structures
		juce::juce_events
		juce::juce_graphics
		juce::juce_gui_basics
		juce::juce_gui_extra
	PUBLIC
		juce::juce_recommended_config_flags
)

if(RETROMULATOR_LTO)
	target_link_libraries(Retromulator PUBLIC juce::juce_recommended_lto_flags)
endif()
