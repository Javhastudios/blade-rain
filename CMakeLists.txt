cmake_minimum_required(VERSION 3.22)

# Versión mínima de macOS (High Sierra). Tiene que ir ANTES de project() para que se aplique.
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Versión mínima de macOS")

project(BladeRain VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows: el runtime de C++ va dentro del ejecutable (no hace falta instalar Visual C++ Redistributable)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# JUCE se descarga automáticamente al configurar (no hace falta instalarlo).
# Para usar una copia local: -DFETCHCONTENT_SOURCE_DIR_JUCE=/ruta/a/JUCE
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.10
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

set(BR_FORMATS Standalone VST3)
if(APPLE)
    list(APPEND BR_FORMATS AU)
endif()

juce_add_plugin(BladeRain
    PRODUCT_NAME "Blade Rain"
    COMPANY_NAME "Javhastudios"
    BUNDLE_ID "com.javhastudios.bladerain"
    PLUGIN_MANUFACTURER_CODE Javh
    PLUGIN_CODE BrVp
    FORMATS ${BR_FORMATS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Blade Rain usa la entrada de audio como micrófono del vocoder."
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Synth
    AU_MAIN_TYPE kAudioUnitType_MusicDevice)

juce_add_binary_data(BladeRainData SOURCES
    Resources/TR909.ttf
    Resources/Oswald.ttf
    Resources/IBMPlexMono-Regular.ttf
    Resources/IBMPlexMono-SemiBold.ttf)

set(BR_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/ui/Theme.cpp
    Source/ui/Views.cpp)

target_sources(BladeRain PRIVATE ${BR_SOURCES})
target_include_directories(BladeRain PRIVATE Source)

# Los textos de la interfaz llevan acentos y símbolos: en Windows hay que compilar como UTF-8
if(MSVC)
    target_compile_options(BladeRain PRIVATE /utf-8)
endif()

target_compile_definitions(BladeRain PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(BladeRain
    PRIVATE
        BladeRainData
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Comprobación del motor contra el prototipo web (no necesita JUCE)
add_executable(compare_cpp EXCLUDE_FROM_ALL tests/compare_cpp.cpp)

# Prueba de humo del plugin completo (no se compila por defecto):
#   cmake --build build --target br_smoke && ./build/br_smoke_artefacts/Release/br_smoke salida/
juce_add_console_app(br_smoke PRODUCT_NAME "br_smoke")
set_target_properties(br_smoke PROPERTIES EXCLUDE_FROM_ALL TRUE)
target_sources(br_smoke PRIVATE tests/smoke.cpp ${BR_SOURCES})
target_include_directories(br_smoke PRIVATE Source)
target_compile_definitions(br_smoke PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(br_smoke PRIVATE BladeRainData juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
