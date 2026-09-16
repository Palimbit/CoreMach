# SPDX-License-Identifier: GPL-2.0-or-later

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
find_package(Threads REQUIRED)

add_executable(blumach_portable_qt WIN32
    src/display_widget.cpp
    src/display_widget.h
    src/machine_dialog.cpp
    src/machine_dialog.h
    src/main.cpp
    src/portable_window.cpp
    src/portable_window.h
    src/session_worker.cpp
    src/session_worker.h
    src/tick_pacer.cpp
    src/tick_pacer.h
)
target_link_libraries(blumach_portable_qt PRIVATE
    blumach_frontend
    blumach_frontend_files
    blumach_platform_null
    blumach_runtime
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Threads::Threads
)
target_compile_features(blumach_portable_qt PRIVATE cxx_std_17)
set_target_properties(blumach_portable_qt PROPERTIES
    CXX_EXTENSIONS OFF
    OUTPUT_NAME BluMach-portable
)

if(MSVC)
    target_compile_options(blumach_portable_qt PRIVATE /W4 /WX)
else()
    target_compile_options(blumach_portable_qt PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
endif()

if(BUILD_TESTING)
    add_executable(blumach_portable_pacer_test
        src/tick_pacer.cpp
        tests/tick_pacer_test.cpp
    )
    target_include_directories(blumach_portable_pacer_test PRIVATE src)
    target_compile_features(blumach_portable_pacer_test PRIVATE cxx_std_17)
    set_target_properties(blumach_portable_pacer_test PROPERTIES
        CXX_EXTENSIONS OFF
    )
    if(MSVC)
        target_compile_options(blumach_portable_pacer_test PRIVATE
            /W4 /WX /UNDEBUG
        )
    else()
        target_compile_options(blumach_portable_pacer_test PRIVATE
            -Wall -Wextra -Wpedantic -Werror -UNDEBUG
        )
    endif()
    add_test(NAME portable-qt.tick-pacer
        COMMAND blumach_portable_pacer_test
    )
endif()

if(WIN32)
    add_custom_command(TARGET blumach_portable_qt POST_BUILD
        COMMAND $<TARGET_FILE:Qt6::windeployqt>
            --no-translations
            --no-compiler-runtime
            $<TARGET_FILE:blumach_portable_qt>
        VERBATIM
    )
endif()
