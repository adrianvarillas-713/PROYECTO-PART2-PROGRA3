cmake_minimum_required(VERSION 3.16)
project(UTEC_Stream CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)
find_package(OpenGL REQUIRED)

include(FetchContent)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.3.8
)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.90.4
)
FetchContent_MakeAvailable(glfw imgui)

add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui_lib PUBLIC glfw OpenGL::GL)

# Tus archivos del motor + los nuevos modulos
set(MOTOR_SOURCES
    buscador.cpp
    catalogo.cpp
    csv.cpp
    estado_usuario.cpp
    indice.cpp
    normalizador.cpp
    recomendador.cpp
    trie.cpp
    arbol_avl.cpp
    patrones.cpp
    paralelo.cpp
)

# 1. Ejecutable con Interfaz Grafica
add_executable(streaming_gui gui_main.cpp ${MOTOR_SOURCES})
target_link_libraries(streaming_gui PRIVATE imgui_lib Threads::Threads)

# 2. Ejecutable original de consola
add_executable(streaming_consola main.cpp ${MOTOR_SOURCES})
target_link_libraries(streaming_consola PRIVATE Threads::Threads)
