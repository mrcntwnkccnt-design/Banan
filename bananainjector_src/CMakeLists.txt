cmake_minimum_required(VERSION 3.10)
project(BananaInjector)

# Не принуждаем к конкретному компилятору — CMake-проекты подойдут для MSVC или MinGW
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Для MSVC: статическая CRT (Release) — соответствие требованию в исходниках
if(MSVC)
    if(POLICY CMP0091)
        cmake_policy(SET CMP0091 NEW)
    endif()
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Исходники
# Явно указанные исходники (избегаем сборки всех backend'ов imgui)
file(GLOB_RECURSE INJECTOR_SRC
    "Injector/*.cpp"
    "Cheat/*.cpp"
    "kiero/*.cpp"
)

# ImGui — только необходимые исходники (core + dx11 + win32 backend)
set(IMGUI_SRC
    ${CMAKE_SOURCE_DIR}/imgui/imgui.cpp
    ${CMAKE_SOURCE_DIR}/imgui/imgui_draw.cpp
    ${CMAKE_SOURCE_DIR}/imgui/imgui_tables.cpp
    ${CMAKE_SOURCE_DIR}/imgui/imgui_widgets.cpp
    ${CMAKE_SOURCE_DIR}/imgui/imgui_demo.cpp
    ${CMAKE_SOURCE_DIR}/imgui/backends/imgui_impl_dx11.cpp
    ${CMAKE_SOURCE_DIR}/imgui/backends/imgui_impl_win32.cpp
)
list(APPEND INJECTOR_SRC ${IMGUI_SRC})

# Заголовки

# Указываем корневую папку в include, чтобы строки типа "imgui/imgui.h" работали
include_directories(
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Injector
    ${CMAKE_SOURCE_DIR}/Cheat
    ${CMAKE_SOURCE_DIR}/kiero
)

# Local wrapper to handle case-sensitive include of Windows.h
include_directories(${CMAKE_SOURCE_DIR}/wininclude)

# MinHook (скачать и распаковать в папку MinHook)
include_directories(${CMAKE_SOURCE_DIR}/MinHook/include)
file(GLOB MINHOOK_SRC "MinHook/src/*.c")

add_library(MinHook STATIC ${MINHOOK_SRC})

add_executable(bananainjector.exe ${INJECTOR_SRC})

target_link_libraries(bananainjector.exe MinHook)

# Линковка платформенных библиотек — для MSVC/Visual Studio
if(MSVC)
    target_link_libraries(bananainjector.exe ws2_32 psapi shlwapi user32 gdi32 d3d11 dxgi)
else()
    target_link_libraries(bananainjector.exe -static -static-libgcc -static-libstdc++ -lws2_32 -lpsapi -lshlwapi -luser32 -lgdi32 -ld3d11 -ldxgi)
endif()

# Для dll (если потребуется)
# add_library(cheatdll SHARED ${CHEAT_SRC})
# target_link_libraries(cheatdll ...)
