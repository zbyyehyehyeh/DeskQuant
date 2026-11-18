cmake_minimum_required(VERSION 3.20)
project(deskquant LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)   # Qt 元对象（Q_OBJECT 需要）

# core：纯 C++ 引擎（头文件，INTERFACE 只加 include 路径，不编译）
add_library(core INTERFACE)
target_include_directories(core INTERFACE core)

find_package(Qt6 REQUIRED COMPONENTS Widgets Charts Network)
set(PYBIND11_FINDPYTHON ON)   # 让 pybind11 用新 FindPython，消除 CMP0148 警告
# 强制用 MSYS2 的 Python（和编译器同目录），避免 find_package 找到 Windows 的 Python
get_filename_component(MSYS2_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
find_program(DESKQUANT_PYTHON NAMES python python3 PATHS "${MSYS2_BIN_DIR}" NO_DEFAULT_PATH REQUIRED)
set(Python3_EXECUTABLE "${DESKQUANT_PYTHON}" CACHE FILEPATH "Python interpreter" FORCE)
find_package(Python3 COMPONENTS Interpreter Development REQUIRED)
find_package(pybind11 CONFIG REQUIRED)

# 主程序（Qt GUI）
add_executable(deskquant
    main.cpp
    gui/backtest_page.cpp
    gui/live_page.cpp
    gui/live_quote.hpp   # Q_OBJECT 类在 header 里，必须列出来让 AUTOMOC 处理
)
target_link_libraries(deskquant PRIVATE core Qt6::Widgets Qt6::Charts Qt6::Network pybind11::embed Python3::Python)
