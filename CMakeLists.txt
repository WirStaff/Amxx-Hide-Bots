cmake_minimum_required(VERSION 3.15)
project(HideBots
        VERSION        1.0.0
        DESCRIPTION    "Changing the number of bots in A2S and Steam API"
        HOMEPAGE_URL   "https://t.me/wirstaff_inc"
        LANGUAGES      CXX
)

set(CMAKE_POLICY_DEFAULT_CMP0077 NEW) 
set(CMAKE_POLICY_DEFAULT_CMP0117 NEW) 

set(AMXX_HIDE_BOTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

set(CMAKE_MODULE_PATH
        ${AMXX_HIDE_BOTS_ROOT}/cmake
        ${CMAKE_MODULE_PATH}
)

set(AMXX_HIDE_BOTS_BIN_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/out/bin" CACHE PATH "Directory for built module binaries")
set(AMXX_HIDE_BOTS_PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/out/pdb" CACHE PATH "Directory for built debug symbols")

if (CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${AMXX_HIDE_BOTS_BIN_OUTPUT_DIRECTORY})
	set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${AMXX_HIDE_BOTS_BIN_OUTPUT_DIRECTORY})
	set(CMAKE_PDB_OUTPUT_DIRECTORY ${AMXX_HIDE_BOTS_PDB_OUTPUT_DIRECTORY})

	foreach (config ${CMAKE_CONFIGURATION_TYPES})
		string(TOUPPER ${config} config_upper)

		set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${config_upper} ${AMXX_HIDE_BOTS_BIN_OUTPUT_DIRECTORY}/${config})
		set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config_upper} ${AMXX_HIDE_BOTS_BIN_OUTPUT_DIRECTORY}/${config})
		set(CMAKE_PDB_OUTPUT_DIRECTORY_${config_upper} ${AMXX_HIDE_BOTS_PDB_OUTPUT_DIRECTORY}/${config})
	endforeach ()
else ()
	set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${AMXX_HIDE_BOTS_BIN_OUTPUT_DIRECTORY}/${CMAKE_BUILD_TYPE})
	set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY})
	set(CMAKE_PDB_OUTPUT_DIRECTORY ${AMXX_HIDE_BOTS_PDB_OUTPUT_DIRECTORY}/${CMAKE_BUILD_TYPE})
endif ()

set(BUILD_SHARED_LIBS OFF) 

if (NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    message(FATAL_ERROR "Unsupported compiler: ${CMAKE_CXX_COMPILER_ID}. Only GCC and MSVC are supported")
endif ()

if (MSVC AND NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR
            "MSVC builds must target 32-bit Win32. "
            "The AMXX SDK uses x86-only inline assembly that MSVC cannot compile for x64. "
            "Reconfigure with: cmake --fresh -S . -B build -A Win32")
endif ()

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(GCC ON)
endif ()

set(CMAKE_CXX_STANDARD 17)

if (GCC)
    add_compile_options(
            -m32
            -Wa,--noexecstack    
    )

    add_link_options(
            -m32
            -static-libstdc++    
            -static-libgcc       
            -Wl,-z,noexecstack   
    )
elseif (MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>") 
endif ()

add_subdirectory(src)

set(MAIN_TARGET_NAME hide_bots)

add_custom_target(BUILD_ALL)
add_dependencies(BUILD_ALL ${MAIN_TARGET_NAME})

set(MAIN_TARGET_COPY_TO_PATH "$ENV{${MAIN_TARGET_NAME}_COPY_TO_PATH}")

if (NOT "${MAIN_TARGET_COPY_TO_PATH}" STREQUAL "")
    add_custom_target(INSTALL_ALL
            COMMENT "Copying '${MAIN_TARGET_NAME}' library to '${MAIN_TARGET_COPY_TO_PATH}'"
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${MAIN_TARGET_NAME}> "${MAIN_TARGET_COPY_TO_PATH}")

    add_custom_target(BUILD_AND_INSTALL_ALL)
    add_dependencies(BUILD_AND_INSTALL_ALL BUILD_ALL)
    add_dependencies(BUILD_AND_INSTALL_ALL INSTALL_ALL)
else ()
    message(STATUS "Skipping INSTALL_ALL and BUILD_AND_INSTALL_ALL targets because environment variable '${MAIN_TARGET_NAME}_COPY_TO_PATH' is not set.")
endif ()
