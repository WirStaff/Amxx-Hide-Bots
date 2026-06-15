set(TARGET_NAME hide_bots)
set(OUTPUT_NAME hide_bots_amxx)

find_package(HLSDK REQUIRED)
find_package(metamod REQUIRED)
find_package(Steamworks REQUIRED)

if (AMXX_HIDE_BOTS_BUILD_STATIC)
    set(LIBRARY_BUILD_TYPE STATIC)
    set(TARGET_LIBRARIES_SCOPE PUBLIC)
else ()
    set(LIBRARY_BUILD_TYPE SHARED)
    set(TARGET_LIBRARIES_SCOPE PRIVATE)
endif ()

add_library(${TARGET_NAME} ${LIBRARY_BUILD_TYPE}
        a2s_query.cpp
        a2s_query.h
        sdk/amxxmodule.cpp
        sdk/amxxmodule.h
        sdk/moduleconfig.h
        sendto_hook.cpp
        sendto_hook.h
        steam_bots.cpp
        steam_bots.h
        module.cpp
)

target_link_libraries(${TARGET_NAME} ${TARGET_LIBRARIES_SCOPE}
        HLSDK::HLSDK_includes
        metamod::metamod
)
add_library(hide_bots::hide_bots ALIAS ${TARGET_NAME})

target_include_directories(${TARGET_NAME} PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${STEAMWORKS_INCLUDE_DIR}
        ${STEAMWORKS_INCLUDE_DIR}/steam
)

target_compile_definitions(${TARGET_NAME} PRIVATE
        NOMINMAX
        HAVE_STDINT_H
        NO_MSVC8_AUTO_COMPAT
)

if (UNIX)
    target_link_libraries(${TARGET_NAME} ${TARGET_LIBRARIES_SCOPE}
            ${CMAKE_DL_LIBS}
    )

    target_compile_definitions(${TARGET_NAME} PRIVATE
            LINUX
    )
else ()
    target_compile_definitions(${TARGET_NAME} PRIVATE
            _WIN32
            _WIN32_WINNT=0x0600
            WIN32
            WINVER=0x0600
    )
endif ()

if (GCC)
    target_compile_options(${TARGET_NAME} PRIVATE
            -fno-rtti  
            -Werror    
    )
elseif (MSVC)
    target_compile_options(${TARGET_NAME} PRIVATE
            /GR-       
            /WX        
            /EHsc      
            $<$<CONFIG:Release>:/Zi>  
    )

    target_link_options(${TARGET_NAME} PRIVATE
            $<$<CONFIG:Release>:/LTCG>            
            $<$<CONFIG:Release>:/INCREMENTAL:NO>  
            $<$<CONFIG:Release>:/DEBUG>           
            $<$<CONFIG:Release>:/OPT:REF>         
            $<$<CONFIG:Release>:/OPT:ICF>         
    )
endif ()

set_target_properties(${TARGET_NAME} PROPERTIES
        PREFIX ""
        OUTPUT_NAME "${OUTPUT_NAME}$<$<BOOL:${UNIX}>:_i386>"
        PDB_NAME ${OUTPUT_NAME}
)

if (GCC)
    set_target_properties(${TARGET_NAME} PROPERTIES
            LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/HideBots.version
            LINK_FLAGS "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/HideBots.version"
    )
endif ()
