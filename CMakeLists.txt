cmake_minimum_required(VERSION 3.15)
project(RxHighlighter LANGUAGES CXX)

# EuroScope is a 32-bit application, so the plugin must be a 32-bit DLL,
# exactly like UK Controller Plugin and vFPC.
#   cmake -S . -B build -G "Visual Studio 17 2022" -A Win32
if(MSVC AND NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "EuroScope plugins must be built as 32-bit. Configure with: -A Win32")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static CRT so the DLL doesn't depend on a particular VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(EUROSCOPE_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/euroscope" CACHE PATH
    "Folder containing EuroScopePlugIn.h and EuroScopePlugInDll.lib")

if(NOT EXISTS "${EUROSCOPE_SDK_DIR}/EuroScopePlugIn.h" OR NOT EXISTS "${EUROSCOPE_SDK_DIR}/EuroScopePlugInDll.lib")
    message(FATAL_ERROR
        "EuroScope SDK not found in ${EUROSCOPE_SDK_DIR}.\n"
        "Run scripts/fetch-sdk.ps1, or copy EuroScopePlugIn.h and EuroScopePlugInDll.lib there.")
endif()

add_library(RxHighlighter SHARED
    src/dllmain.cpp
    src/RxHighlightPlugin.cpp
    src/RxRadarScreen.cpp
    src/RxState.cpp
    src/VoiceClient.cpp
    src/VoiceMessage.cpp
    src/WebSocketCodec.cpp
)

target_include_directories(RxHighlighter PRIVATE src "${EUROSCOPE_SDK_DIR}")
target_compile_definitions(RxHighlighter PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
# No /permissive-: the EuroScope SDK header relies on MSVC accepting
# "friend class X;" as a forward declaration (UKCP builds the same way).
target_compile_options(RxHighlighter PRIVATE /W4 /EHsc)
target_link_libraries(RxHighlighter PRIVATE "${EUROSCOPE_SDK_DIR}/EuroScopePlugInDll.lib" ws2_32)

# Unit tests for the platform-independent parts.
add_executable(RxHighlighterTests test/test_main.cpp src/RxState.cpp src/VoiceMessage.cpp src/WebSocketCodec.cpp)
target_include_directories(RxHighlighterTests PRIVATE src)
enable_testing()
add_test(NAME RxHighlighterTests COMMAND RxHighlighterTests)
