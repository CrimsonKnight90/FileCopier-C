cmake_minimum_required(VERSION 3.20)
project(FileCopier LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)
qt_standard_project_setup()

qt_add_executable(FileCopier WIN32
    src/main.cpp
    src/AppController.cpp
    src/core/FileCopyEngine.cpp
    src/jobs/JobManager.cpp
    src/ui/MainWindow.cpp
)

target_include_directories(FileCopier PRIVATE include)

target_link_libraries(FileCopier PRIVATE
    Qt6::Core
    Qt6::Widgets
)