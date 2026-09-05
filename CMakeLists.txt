cmake_minimum_required(VERSION 3.16)
project(TestArayuzuProjesi LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Quick Network)
qt_standard_project_setup()

qt_add_executable(TestArayuzu
    main.cpp
    TestBridge.cpp TestBridge.h
)

qt_add_qml_module(TestArayuzu
    URI TestArayuzu
    VERSION 1.0
    QML_FILES
        Main.qml
)

target_link_libraries(TestArayuzu PRIVATE
    Qt6::Core
    Qt6::Quick
    Qt6::Network
)