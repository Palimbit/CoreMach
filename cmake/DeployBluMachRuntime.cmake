# Deploy non-system DLL dependencies that windeployqt does not copy when Qt
# comes from an MSYS2/MinGW environment.

if(NOT DEFINED BLUMACH_EXECUTABLE OR NOT EXISTS "${BLUMACH_EXECUTABLE}")
    message(FATAL_ERROR "BLUMACH_EXECUTABLE does not name a built executable")
endif()

if(NOT DEFINED BLUMACH_RUNTIME_SEARCH_DIR OR
   NOT IS_DIRECTORY "${BLUMACH_RUNTIME_SEARCH_DIR}")
    message(FATAL_ERROR "BLUMACH_RUNTIME_SEARCH_DIR is not a directory")
endif()

get_filename_component(BLUMACH_OUTPUT_DIR "${BLUMACH_EXECUTABLE}" DIRECTORY)

file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES "${BLUMACH_EXECUTABLE}"
    DIRECTORIES
        "${BLUMACH_OUTPUT_DIR}"
        "${BLUMACH_RUNTIME_SEARCH_DIR}"
    RESOLVED_DEPENDENCIES_VAR BLUMACH_RESOLVED_DEPENDENCIES
    UNRESOLVED_DEPENDENCIES_VAR BLUMACH_UNRESOLVED_DEPENDENCIES
    PRE_EXCLUDE_REGEXES
        "api-ms-.*"
        "ext-ms-.*"
    POST_EXCLUDE_REGEXES
        ".*[\\/]Windows[\\/]System32[\\/].*"
        ".*[\\/]WINDOWS[\\/]SYSTEM32[\\/].*"
        "AzureAttestManager\\.dll"
        "AzureAttestNormal\\.dll"
        "HvsiFileTrust\\.dll"
        "PdmUtilities\\.dll"
        "wpaxholder\\.dll")

foreach(BLUMACH_DEPENDENCY IN LISTS BLUMACH_RESOLVED_DEPENDENCIES)
    cmake_path(IS_PREFIX BLUMACH_RUNTIME_SEARCH_DIR
               "${BLUMACH_DEPENDENCY}" NORMALIZE BLUMACH_FROM_TOOLCHAIN)
    if(BLUMACH_FROM_TOOLCHAIN)
        file(COPY "${BLUMACH_DEPENDENCY}" DESTINATION "${BLUMACH_OUTPUT_DIR}")
    endif()
endforeach()

if(BLUMACH_UNRESOLVED_DEPENDENCIES)
    list(JOIN BLUMACH_UNRESOLVED_DEPENDENCIES ", " BLUMACH_UNRESOLVED_TEXT)
    message(WARNING "Unresolved BluMach runtime dependencies: ${BLUMACH_UNRESOLVED_TEXT}")
endif()
