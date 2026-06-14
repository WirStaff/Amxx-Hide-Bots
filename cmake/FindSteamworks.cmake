if (TARGET Steamworks::steamworks)
    return()
endif ()

include(FindPackageHandleStandardArgs)

set(STEAMWORKS_ROOT "${AMXX_HIDE_BOTS_ROOT}/dep/steamworks" CACHE PATH "Root directory of the Steamworks SDK")

set(_steamworks_roots
        "${STEAMWORKS_ROOT}"
        "${STEAMWORKS_ROOT}/sdk"
)

find_path(STEAMWORKS_INCLUDE_DIR
        NAMES steam/steam_api.h
        PATHS ${_steamworks_roots}
        PATH_SUFFIXES public sdk/public
        NO_DEFAULT_PATH
        NO_CACHE
)

if (WIN32)
    find_library(STEAMWORKS_LIBRARY
            NAMES steam_api
            PATHS ${_steamworks_roots}
            PATH_SUFFIXES redistributable_bin redistributable_bin/win32 sdk/redistributable_bin sdk/redistributable_bin/win32
            NO_DEFAULT_PATH
            NO_CACHE
    )

    find_file(STEAMWORKS_RUNTIME
            NAMES steam_api.dll
            PATHS ${_steamworks_roots}
            PATH_SUFFIXES redistributable_bin redistributable_bin/win32 sdk/redistributable_bin sdk/redistributable_bin/win32
            NO_DEFAULT_PATH
            NO_CACHE
    )

    set(_steamworks_required_vars STEAMWORKS_INCLUDE_DIR STEAMWORKS_LIBRARY STEAMWORKS_RUNTIME)
elseif (UNIX)
    find_library(STEAMWORKS_LIBRARY
            NAMES steam_api
            PATHS ${_steamworks_roots}
            PATH_SUFFIXES redistributable_bin/linux32 sdk/redistributable_bin/linux32
            NO_DEFAULT_PATH
            NO_CACHE
    )

    set(STEAMWORKS_RUNTIME "${STEAMWORKS_LIBRARY}")
    set(_steamworks_required_vars STEAMWORKS_INCLUDE_DIR STEAMWORKS_LIBRARY)
else ()
    message(FATAL_ERROR "Unsupported platform for Steamworks")
endif ()

find_package_handle_standard_args(Steamworks
        REQUIRED_VARS ${_steamworks_required_vars}
)

if (Steamworks_FOUND)
    set(_steamworks_include_dirs
            "${STEAMWORKS_INCLUDE_DIR}"
            "${STEAMWORKS_INCLUDE_DIR}/steam"
    )

    if (WIN32)
        add_library(Steamworks::steamworks SHARED IMPORTED GLOBAL)
        set_target_properties(Steamworks::steamworks PROPERTIES
                IMPORTED_IMPLIB "${STEAMWORKS_LIBRARY}"
                IMPORTED_LOCATION "${STEAMWORKS_RUNTIME}"
                INTERFACE_INCLUDE_DIRECTORIES "${_steamworks_include_dirs}"
        )
    else ()
        add_library(Steamworks::steamworks UNKNOWN IMPORTED GLOBAL)
        set_target_properties(Steamworks::steamworks PROPERTIES
                IMPORTED_LOCATION "${STEAMWORKS_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${_steamworks_include_dirs}"
        )
    endif ()
endif ()

mark_as_advanced(STEAMWORKS_ROOT STEAMWORKS_INCLUDE_DIR STEAMWORKS_LIBRARY STEAMWORKS_RUNTIME)
