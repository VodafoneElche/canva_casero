cmake_minimum_required(VERSION 3.16)
project(LienzoCasero VERSION 0.11.0 LANGUAGES C)
if(WIN32)
    enable_language(RC)
endif()

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# raylib: se descarga solo la primera vez que se compila
include(FetchContent)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
set(CUSTOMIZE_BUILD ON CACHE BOOL "" FORCE)
set(SUPPORT_FILEFORMAT_JPG ON CACHE BOOL "" FORCE)
set(SUPPORT_FILEFORMAT_BMP ON CACHE BOOL "" FORCE)
set(SUPPORT_FILEFORMAT_GIF ON CACHE BOOL "" FORCE)
set(SUPPORT_FILEFORMAT_TGA ON CACHE BOOL "" FORCE)
set(SUPPORT_MODULE_RAUDIO OFF CACHE BOOL "" FORCE)  # no hace falta sonido
set(SUPPORT_CLIPBOARD_IMAGE ON CACHE BOOL "" FORCE) # pegar fotos con Ctrl+V
FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 5.5
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(raylib)

add_compile_definitions(APP_VERSION="${PROJECT_VERSION}")

set(SOURCES
    src/main.c
    src/doc.c
    src/render.c
    src/ui.c
    src/templates.c
    src/platform.c
    src/svg.c src/magic.c src/pdf.c src/eps.c src/update.c
)
# nanosvg es código de otra persona: sin avisos
set_source_files_properties(src/svg.c PROPERTIES COMPILE_OPTIONS "-w")

if(WIN32)
    # Icono y datos del programa (los usa Windows en el Explorador y la barra de tareas)
    list(APPEND SOURCES assets/lienzo.rc)
    add_executable(LienzoCasero WIN32 ${SOURCES})
    target_link_libraries(LienzoCasero PRIVATE raylib comdlg32 shell32 ole32 urlmon winmm gdi32)
    if(MINGW)
        # Sin dependencias de DLL de MinGW: un solo .exe
        target_link_options(LienzoCasero PRIVATE -static -static-libgcc)
    endif()
    if(MSVC)
        target_compile_definitions(LienzoCasero PRIVATE _CRT_SECURE_NO_WARNINGS)
        set_property(TARGET LienzoCasero PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
else()
    add_executable(LienzoCasero ${SOURCES})
    target_link_libraries(LienzoCasero PRIVATE raylib m pthread)
endif()

if(NOT MSVC)
    target_compile_options(LienzoCasero PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
endif()

# Copiar las fuentes incluidas junto al programa (carpeta "fuentes")
add_custom_command(TARGET LienzoCasero POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets/fonts $<TARGET_FILE_DIR:LienzoCasero>/fuentes
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets/dibujos $<TARGET_FILE_DIR:LienzoCasero>/dibujos)

option(LIENZO_TESTS "Compilar el modo de pruebas automáticas" OFF)
if(LIENZO_TESTS)
    target_compile_definitions(LienzoCasero PRIVATE LIENZO_TESTS)
    # Herramienta para ver qué piezas salen de un PDF
    add_executable(pdfdump tests/pdfdump.c src/pdf.c src/magic.c src/svg.c)
    target_link_libraries(pdfdump raylib)
    add_executable(eps2svg tests/eps2svg.c src/eps.c)
    target_link_libraries(eps2svg m)
endif()

install(TARGETS LienzoCasero RUNTIME DESTINATION .)
install(DIRECTORY assets/fonts/ DESTINATION fuentes)
install(DIRECTORY assets/dibujos/ DESTINATION dibujos)
