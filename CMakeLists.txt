cmake_minimum_required(VERSION 3.26)

project(DynamicSunAttenuation
    VERSION 1.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(spdlog CONFIG REQUIRED)
find_path(SIMPLEINI_INCLUDE_DIRS "SimpleIni.h" REQUIRED)

# Resolucion flexible de CommonLibSF:
# 1) Submodulo local en extern/CommonLibSF
# 2) Variable de entorno CommonLibSFPath (tu copia local de libxse/commonlibsf)
# 3) Paquete preinstalado (find_package)
# 4) Descarga automatica via FetchContent
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/extern/CommonLibSF/CMakeLists.txt")
    message(STATUS "Usando CommonLibSF local desde extern/CommonLibSF")
    add_subdirectory(extern/CommonLibSF)
elseif(DEFINED ENV{CommonLibSFPath} AND EXISTS "$ENV{CommonLibSFPath}/CMakeLists.txt")
    message(STATUS "Usando CommonLibSF desde CommonLibSFPath: $ENV{CommonLibSFPath}")
    add_subdirectory("$ENV{CommonLibSFPath}" "${CMAKE_BINARY_DIR}/_deps/commonlibsf")
else()
    find_package(CommonLibSF CONFIG QUIET)
    if(NOT CommonLibSF_FOUND)
        message(STATUS "Descargando libxse/commonlibsf via FetchContent...")
        include(FetchContent)
        FetchContent_Declare(
            CommonLibSF
            GIT_REPOSITORY https://github.com/libxse/commonlibsf.git
            GIT_TAG        main
            GIT_SHALLOW    TRUE
        )
        FetchContent_MakeAvailable(CommonLibSF)
    endif()
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

if(TARGET CommonLibSF::CommonLibSF)
    target_link_libraries(${PROJECT_NAME} PRIVATE CommonLibSF::CommonLibSF)
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE CommonLibSF)
endif()

target_link_libraries(${PROJECT_NAME}
    PRIVATE
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
            /EHsc
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

# Copia el INI junto a la DLL compilada dentro de build/.../package/Data/SFSE/Plugins/
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/dist/Data/SFSE/Plugins/${PROJECT_NAME}.ini"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/${PROJECT_NAME}.ini"
    COMMENT "Preparando paquete mod en $<TARGET_FILE_DIR:${PROJECT_NAME}>"
)

# Copia automatica directa a la instalacion de Starfield / MO2 si SFPath esta definido
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
