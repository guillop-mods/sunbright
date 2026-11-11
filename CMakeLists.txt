cmake_minimum_required(VERSION 3.26)

project(DynamicSunAttenuation
    VERSION 1.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_path(SIMPLEINI_INCLUDE_DIRS "SimpleIni.h" REQUIRED)
find_path(BINARY_IO_INCLUDE_DIRS "binary_io/binary_io.hpp" REQUIRED)

set(COMMONLIBSF_DIR "${CMAKE_CURRENT_SOURCE_DIR}/extern/CommonLibSF")
if(NOT EXISTS "${COMMONLIBSF_DIR}/include/RE/Starfield.h")
    message(FATAL_ERROR "No se encontro CommonLibSF en ${COMMONLIBSF_DIR}")
endif()

file(GLOB_RECURSE COMMONLIBSF_SOURCES CONFIGURE_DEPENDS "${COMMONLIBSF_DIR}/src/*.cpp")

add_library(CommonLibSF STATIC ${COMMONLIBSF_SOURCES})
target_include_directories(CommonLibSF
    PUBLIC
        "${COMMONLIBSF_DIR}/include"
        ${BINARY_IO_INCLUDE_DIRS}
    PRIVATE
        "${COMMONLIBSF_DIR}/src"
)
target_link_libraries(CommonLibSF
    PUBLIC
        fmt::fmt
        spdlog::spdlog
        Version.lib
        Dbghelp.lib
)

if(MSVC)
    target_compile_options(CommonLibSF PRIVATE /utf-8 /permissive- /W0 /EHa /MP /Zc:preprocessor)
    target_compile_definitions(CommonLibSF PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
endif()

set(HEADERS
    include/PCH.h
    include/Config.h
    include/MathUtils.h
    include/SkyDiagnostics.h
    include/SunController.h
    include/Hooks.h
)

set(SOURCES
    src/Config.cpp
    src/SkyDiagnostics.cpp
    src/SunController.cpp
    src/Hooks.cpp
    src/main.cpp
)

add_library(${PROJECT_NAME} MODULE
    ${HEADERS}
    ${SOURCES}
)

target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${SIMPLEINI_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        CommonLibSF
        spdlog::spdlog
)

target_precompile_headers(${PROJECT_NAME}
    PRIVATE
        include/PCH.h
)

if(MSVC)
    target_compile_options(${PROJECT_NAME}
        PRIVATE
            /utf-8
            /permissive-
            /W4
            /WX-
            /EHa
            /MP
            /Zc:preprocessor
            /external:anglebrackets
            /external:W0
    )

    target_compile_definitions(${PROJECT_NAME}
        PRIVATE
            WIN32_LEAN_AND_MEAN
            NOMINMAX
            _CRT_SECURE_NO_WARNINGS
    )
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME "${PROJECT_NAME}"
    PDB_NAME "${PROJECT_NAME}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/package/Data/SFSE/Plugins"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/package/Data/SFSE/Plugins"
)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/dist/Data/SFSE/Plugins/${PROJECT_NAME}.ini"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/${PROJECT_NAME}.ini"
    COMMENT "Preparando paquete mod en $<TARGET_FILE_DIR:${PROJECT_NAME}>"
)

if(DEFINED ENV{SFPath})
    set(SF_PLUGIN_DIR "$ENV{SFPath}/Data/SFSE/Plugins")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SF_PLUGIN_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:${PROJECT_NAME}>"
            "${SF_PLUGIN_DIR}/$<TARGET_FILE_NAME:${PROJECT_NAME}>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/dist/Data/SFSE/Plugins/${PROJECT_NAME}.ini"
            "${SF_PLUGIN_DIR}/${PROJECT_NAME}.ini"
        COMMENT "Desplegando ${PROJECT_NAME}.dll e INI en ${SF_PLUGIN_DIR}"
    )
endif()
