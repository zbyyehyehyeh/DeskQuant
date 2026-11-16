cmake_minimum_required(VERSION 3.20)
project(deskquant LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)   # Qt 元对象（Q_OBJECT 需要）

# core：纯 C++ 引擎（头文件，INTERFACE 只加 include 路径，不编译）
add_library(core INTERFACE)
target_include_directories(core INTERFACE core)

find_package(Qt6 REQUIRED COMPONENTS Widgets Charts)

# 主程序（Qt GUI）
add_executable(deskquant
    main.cpp
    gui/backtest_page.cpp
)
target_link_libraries(deskquant PRIVATE core Qt6::Widgets Qt6::Charts)