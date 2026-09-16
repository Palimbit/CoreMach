# SPDX-License-Identifier: GPL-2.0-or-later

function(blumach_prepare_catalog output_variable)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)

    set(catalog_source_dir "${PROJECT_SOURCE_DIR}/src/qt/catalog/source")
    set(catalog_build_dir "${CMAKE_CURRENT_BINARY_DIR}/catalog")
    set(catalog_qrc "${catalog_build_dir}/blumach_catalog.qrc")
    file(GLOB_RECURSE catalog_source_files CONFIGURE_DEPENDS
        "${catalog_source_dir}/*.json")
    file(GLOB implementation_documents CONFIGURE_DEPENDS
        "${PROJECT_SOURCE_DIR}/doc/machines/*-implementation.md")

    file(MAKE_DIRECTORY "${catalog_build_dir}")
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" -B
                "${PROJECT_SOURCE_DIR}/tools/catalog_builder.py"
                build --source "${catalog_source_dir}"
                --output "${catalog_build_dir}"
        RESULT_VARIABLE catalog_result
        ERROR_VARIABLE catalog_error
    )
    if(NOT catalog_result EQUAL 0)
        message(FATAL_ERROR
            "Unable to generate the BluMach catalogue:\n${catalog_error}")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${PROJECT_SOURCE_DIR}/tools/catalog_builder.py"
        "${PROJECT_SOURCE_DIR}/tools/catalog_audit.py"
        ${catalog_source_files}
        ${implementation_documents}
    )
    set(${output_variable} "${catalog_qrc}" PARENT_SCOPE)
endfunction()
