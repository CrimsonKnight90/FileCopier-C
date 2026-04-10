cmake_minimum_required(VERSION 3.20)
project(FileCopier VERSION 1.0.0 LANGUAGES CXX)

# ── Estándar C++ ─────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Qt ───────────────────────────────────────────────────────────────────────
# Ajustar CMAKE_PREFIX_PATH si Qt no está en PATH:
#   cmake -DCMAKE_PREFIX_PATH="C:/Qt/6.7.0/msvc2022_64" ..
find_package(Qt6 REQUIRED COMPONENTS Core Widgets)
qt_standard_project_setup()

# ── Flags de optimización ────────────────────────────────────────────────────
if(MSVC)
    add_compile_options(
        /W4           # Warning level 4
        /WX-          # No tratar warnings como errores (quitar - para producción)
        /O2           # Optimización máxima
        /GL           # Link-time code generation
        /MP           # Compilación en paralelo
        /utf-8        # Fuentes en UTF-8
    )
    add_link_options(/LTCG)   # Link-time optimization
else()
    # MinGW / GCC / Clang
    add_compile_options(-O2 -Wall -Wextra -municode)
endif()

# ── Definiciones globales ────────────────────────────────────────────────────
add_compile_definitions(
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
    _WIN32_WINNT=0x0A00   # Windows 10+
)

# ── Fuentes ──────────────────────────────────────────────────────────────────
set(SOURCES
    src/main.cpp

    # Core
    src/core/FileCopyEngine.cpp
    src/core/DirectoryScanner.cpp
    src/core/ThreadPool.cpp
    src/core/BufferManager.cpp

    # Jobs
    src/jobs/JobQueue.cpp
    src/jobs/JobState.cpp
    src/jobs/ResumeManager.cpp

    # UI  (contienen Q_OBJECT → Qt MOC los procesa automáticamente)
    src/ui/MainWindow.cpp
    src/ui/ProgressPanel.cpp
    src/ui/ErrorDialog.cpp
    src/ui/ErrorLog.cpp
    src/ui/TrayIcon.cpp
    src/ui/ConfigDialog.cpp

    # Utils
    src/utils/Logger.cpp
    src/utils/ConfigManager.cpp
    src/utils/ErrorHandler.cpp
    src/utils/Language.cpp
    src/utils/EventBus.cpp
)

set(HEADERS
    include/FileCopyEngine.h
    include/ThreadPool.h
    include/DirectoryScanner.h
    include/BufferManager.h
    include/Jobs.h
    include/Utils.h
    include/EventBus.h
    include/Language.h
    include/ui/MainWindow.h
    include/ui/ProgressPanel.h
    include/ui/ErrorDialog.h
    include/ui/ErrorLog.h
    include/ui/TrayIcon.h
    include/ui/ConfigDialog.h
    include/PathUtils.h
)

# ── Ejecutable ───────────────────────────────────────────────────────────────
# WIN32 → subsistema Windows (no abre consola)
qt_add_executable(FileCopier WIN32
    ${SOURCES}
    ${HEADERS}
)

# ── Includes ─────────────────────────────────────────────────────────────────
target_include_directories(FileCopier PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/ui
)

# ── Links ────────────────────────────────────────────────────────────────────
target_link_libraries(FileCopier PRIVATE
    Qt6::Core
    Qt6::Widgets
    shlwapi
    # WinAPI ya está disponible implícitamente en MSVC/MinGW con _WIN32_WINNT
)

# ── Recurso de icono (opcional) ───────────────────────────────────────────────
if(EXISTS "${CMAKE_SOURCE_DIR}/resources/app.rc")
    target_sources(FileCopier PRIVATE resources/app.rc)
endif()

# ── Despliegue Qt (windeployqt) ───────────────────────────────────────────────
# Copia las DLLs de Qt junto al ejecutable en el build directory.
if(WIN32)
    find_program(WINDEPLOYQT windeployqt HINTS "${Qt6_DIR}/../../../bin")
    if(WINDEPLOYQT)
        add_custom_command(TARGET FileCopier POST_BUILD
            COMMAND ${WINDEPLOYQT}
                --no-translations
                --no-system-d3d-compiler
                "$<TARGET_FILE:FileCopier>"
            COMMENT "Running windeployqt..."
        )
    endif()
endif()

# ── Información de build ──────────────────────────────────────────────────────
message(STATUS "")
message(STATUS "=== FileCopier build configuration ===")
message(STATUS "  Build type   : ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ standard : ${CMAKE_CXX_STANDARD}")
message(STATUS "  Qt version   : ${Qt6_VERSION}")
message(STATUS "  Compiler     : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "=======================================")
message(STATUS "")
