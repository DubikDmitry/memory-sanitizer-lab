cmake_minimum_required(VERSION 3.21)

project(MemorySanitizerLab LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

add_executable(memory_lab
    src/main.cpp
)

target_compile_options(memory_lab PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wshadow>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

if(ENABLE_SANITIZERS)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(memory_lab PRIVATE
            -g
            -O1
            -fno-omit-frame-pointer
            -fsanitize=address,undefined
        )
        target_link_options(memory_lab PRIVATE
            -fsanitize=address,undefined
        )
    else()
        message(FATAL_ERROR
            "This lab's combined ASan+UBSan configuration is prepared for GCC/Clang on Linux. "
            "Use the WSL2 Ubuntu instructions in README.md."
        )
    endif()
endif()


option(ENABLE_GUI "Build Qt6 launcher GUI (requires Qt6 Widgets)" OFF)

if(ENABLE_GUI)
    find_package(Qt6 REQUIRED COMPONENTS Widgets)

    add_executable(memory_lab_gui
        src/gui_main.cpp
    )

    set_target_properties(memory_lab_gui PROPERTIES
        AUTOMOC ON
        AUTOUIC ON
        AUTORCC ON
    )

    target_link_libraries(memory_lab_gui PRIVATE Qt6::Widgets)
endif()