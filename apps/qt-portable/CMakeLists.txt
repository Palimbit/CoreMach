# SPDX-License-Identifier: GPL-2.0-or-later

find_package(Qt6 REQUIRED COMPONENTS Core Gui OpenGLWidgets Widgets)
find_package(Threads REQUIRED)
include(BluMachCatalog)
blumach_prepare_catalog(BLUMACH_PORTABLE_CATALOG_QRC)

add_executable(blumach_portable_qt WIN32
    src/display_widget.cpp
    src/display_widget.h
    src/machine_dialog.cpp
    src/machine_dialog.h
    src/main.cpp
    src/portable_window.cpp
    src/portable_window.h
    src/qt_key_map.cpp
    src/qt_key_map.h
    src/portable_catalog.cpp
    src/portable_catalog.h
    src/session_worker.cpp
    src/session_worker.h
    src/worker_wakeup.cpp
    src/worker_wakeup.h
    src/latency_trace.h
    src/snapshot_mailbox.cpp
    src/snapshot_mailbox.h
    src/tick_pacer.cpp
    src/tick_pacer.h
    ${BLUMACH_PORTABLE_CATALOG_QRC}
)
set_target_properties(blumach_portable_qt PROPERTIES AUTORCC ON)
target_link_libraries(blumach_portable_qt PRIVATE
    blumach_frontend
    blumach_frontend_files
    blumach_platform_null
    blumach_runtime
    Qt6::Core
    Qt6::Gui
    Qt6::OpenGLWidgets
    Qt6::Widgets
    Threads::Threads
)
target_compile_features(blumach_portable_qt PRIVATE cxx_std_17)
set_target_properties(blumach_portable_qt PROPERTIES
    CXX_EXTENSIONS OFF
    OUTPUT_NAME BluMach-portable
)
if(APPLE)
    set_target_properties(blumach_portable_qt PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "BluMach Portable"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.blumach.portable"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${BLUMACH_PORTABLE_VERSION_NUMERIC}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${BLUMACH_PORTABLE_VERSION_NUMERIC}"
    )
endif()

include(GNUInstallDirs)
install(TARGETS blumach_portable_qt
    RUNTIME_DEPENDENCY_SET blumach_portable_runtime_dependencies
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    BUNDLE DESTINATION "."
)

if(WIN32)
    get_filename_component(blumach_qt_prefix "${Qt6_DIR}/../../.." ABSOLUTE)
    get_filename_component(blumach_compiler_directory
        "${CMAKE_CXX_COMPILER}" DIRECTORY)
    install(RUNTIME_DEPENDENCY_SET blumach_portable_runtime_dependencies
        DIRECTORIES
            "${blumach_qt_prefix}/bin"
            "${blumach_compiler_directory}"
        PRE_EXCLUDE_REGEXES
            "^api-ms-"
            "^ext-ms-"
        POST_EXCLUDE_REGEXES
            ".*[Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\\\][Ss][Yy][Ss][Tt][Ee][Mm]32.*"
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    )
endif()

qt_generate_deploy_app_script(
    TARGET blumach_portable_qt
    OUTPUT_SCRIPT blumach_portable_deploy_script
    NO_TRANSLATIONS
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT "${blumach_portable_deploy_script}")

if(MSVC)
    target_compile_options(blumach_portable_qt PRIVATE /W4 /WX)
else()
    target_compile_options(blumach_portable_qt PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
endif()

if(BUILD_TESTING)
    add_executable(blumach_portable_wakeup_test
        src/worker_wakeup.cpp tests/worker_wakeup_test.cpp)
    target_include_directories(blumach_portable_wakeup_test PRIVATE src)
    target_link_libraries(blumach_portable_wakeup_test PRIVATE Qt6::Core Threads::Threads)
    target_compile_features(blumach_portable_wakeup_test PRIVATE cxx_std_17)
    if(MSVC)
        target_compile_options(blumach_portable_wakeup_test PRIVATE /W4 /WX /UNDEBUG)
    else()
        target_compile_options(blumach_portable_wakeup_test PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
    endif()
    add_test(NAME portable-qt.worker-wakeup COMMAND blumach_portable_wakeup_test)
    set_tests_properties(portable-qt.worker-wakeup PROPERTIES TIMEOUT 10)

    add_executable(blumach_portable_display_widget_test
        src/display_widget.cpp
        tests/display_widget_test.cpp
    )
    target_include_directories(blumach_portable_display_widget_test PRIVATE src)
    target_link_libraries(blumach_portable_display_widget_test PRIVATE
        Qt6::Gui
        Qt6::OpenGLWidgets
        Qt6::Widgets
    )
    target_compile_features(blumach_portable_display_widget_test PRIVATE
        cxx_std_17
    )
    set_target_properties(blumach_portable_display_widget_test PROPERTIES
        CXX_EXTENSIONS OFF
    )
    if(MSVC)
        target_compile_options(blumach_portable_display_widget_test PRIVATE
            /W4 /WX /UNDEBUG
        )
    else()
        target_compile_options(blumach_portable_display_widget_test PRIVATE
            -Wall -Wextra -Wpedantic -Werror -UNDEBUG
        )
    endif()
    add_test(NAME portable-qt.display-widget
        COMMAND blumach_portable_display_widget_test
    )

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

    add_executable(blumach_portable_qt_key_map_test
        src/qt_key_map.cpp
        tests/qt_key_map_test.cpp
    )
    target_include_directories(blumach_portable_qt_key_map_test PRIVATE src)
    target_link_libraries(blumach_portable_qt_key_map_test PRIVATE
        blumach_engine
        Qt6::Core
    )
    target_compile_features(blumach_portable_qt_key_map_test PRIVATE cxx_std_17)
    set_target_properties(blumach_portable_qt_key_map_test PROPERTIES
        CXX_EXTENSIONS OFF
    )
    if(MSVC)
        target_compile_options(blumach_portable_qt_key_map_test PRIVATE
            /W4 /WX /UNDEBUG
        )
    else()
        target_compile_options(blumach_portable_qt_key_map_test PRIVATE
            -Wall -Wextra -Wpedantic -Werror -UNDEBUG
        )
    endif()
    add_test(NAME portable-qt.key-map
        COMMAND blumach_portable_qt_key_map_test
    )

    add_executable(blumach_portable_snapshot_mailbox_test
        src/snapshot_mailbox.cpp
        tests/snapshot_mailbox_test.cpp
    )
    target_include_directories(blumach_portable_snapshot_mailbox_test PRIVATE
        src
    )
    target_link_libraries(blumach_portable_snapshot_mailbox_test PRIVATE
        blumach_runtime
        Qt6::Gui
    )
    target_compile_features(blumach_portable_snapshot_mailbox_test PRIVATE
        cxx_std_17
    )
    set_target_properties(blumach_portable_snapshot_mailbox_test PROPERTIES
        CXX_EXTENSIONS OFF
    )
    if(MSVC)
        target_compile_options(blumach_portable_snapshot_mailbox_test PRIVATE
            /W4 /WX /UNDEBUG
        )
    else()
        target_compile_options(blumach_portable_snapshot_mailbox_test PRIVATE
            -Wall -Wextra -Wpedantic -Werror -UNDEBUG
        )
    endif()
    add_test(NAME portable-qt.snapshot-mailbox
        COMMAND blumach_portable_snapshot_mailbox_test
    )

    add_executable(blumach_portable_session_state_test
        src/session_worker.cpp
        src/tick_pacer.cpp
        src/worker_wakeup.cpp
        tests/session_worker_persistent_state_test.cpp
    )
    target_include_directories(blumach_portable_session_state_test PRIVATE src)
    target_link_libraries(blumach_portable_session_state_test PRIVATE
        blumach_frontend
        blumach_platform_null
        blumach_runtime
        Qt6::Core
        Qt6::Gui
        Threads::Threads
    )
    target_compile_features(blumach_portable_session_state_test PRIVATE
        cxx_std_17
    )
    set_target_properties(blumach_portable_session_state_test PROPERTIES
        CXX_EXTENSIONS OFF
    )
    if(MSVC)
        target_compile_options(blumach_portable_session_state_test PRIVATE
            /W4 /WX /UNDEBUG
        )
    else()
        target_compile_options(blumach_portable_session_state_test PRIVATE
            -Wall -Wextra -Wpedantic -Werror -UNDEBUG
        )
    endif()
    add_test(NAME portable-qt.session-persistent-state
        COMMAND blumach_portable_session_state_test
    )

    add_executable(blumach_portable_catalog_test
        src/portable_catalog.cpp
        tests/portable_catalog_test.cpp
    )
    target_include_directories(blumach_portable_catalog_test PRIVATE src)
    target_link_libraries(blumach_portable_catalog_test PRIVATE Qt6::Core)
    target_compile_features(blumach_portable_catalog_test PRIVATE cxx_std_17)
    set_target_properties(blumach_portable_catalog_test PROPERTIES
        CXX_EXTENSIONS OFF
    )
    if(MSVC)
        target_compile_options(blumach_portable_catalog_test PRIVATE
            /W4 /WX /UNDEBUG
        )
    else()
        target_compile_options(blumach_portable_catalog_test PRIVATE
            -Wall -Wextra -Wpedantic -Werror -UNDEBUG
        )
    endif()
    add_test(NAME portable-qt.catalog
        COMMAND blumach_portable_catalog_test
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
