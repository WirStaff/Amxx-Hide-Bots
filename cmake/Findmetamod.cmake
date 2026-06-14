if (TARGET metamod::metamod)
    return()
endif ()

include(FindPackageHandleStandardArgs)

find_path(METAMOD_INCLUDE_DIR
        NAMES metamod.h
        PATHS ${AMXX_HIDE_BOTS_ROOT}/dep/metamod
        NO_DEFAULT_PATH
        NO_CACHE
)

find_package_handle_standard_args(metamod REQUIRED_VARS METAMOD_INCLUDE_DIR)

add_library(metamod::metamod INTERFACE IMPORTED GLOBAL)

target_include_directories(metamod::metamod INTERFACE
        ${METAMOD_INCLUDE_DIR}
)
