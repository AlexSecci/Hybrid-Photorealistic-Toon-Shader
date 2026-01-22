cmake_minimum_required(VERSION 3.20)
project(CelShadingRenderer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# GLFW
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.3.8
)
FetchContent_MakeAvailable(glfw)

# GLM
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 0.9.9.8
)
FetchContent_GetProperties(glm)
if(NOT glm_POPULATED)
    FetchContent_Populate(glm)
    add_library(glm INTERFACE)
    target_include_directories(glm INTERFACE ${glm_SOURCE_DIR})
endif()

# Assimp
FetchContent_Declare(
    assimp
    GIT_REPOSITORY https://github.com/assimp/assimp.git
    GIT_TAG v5.2.5
)
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL_PDB OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(assimp)

# GLAD
# Generate GLAD 4.6 Core using the official generator
FetchContent_Declare(
    glad_src
    GIT_REPOSITORY https://github.com/Dav1dde/glad.git
    GIT_TAG v0.1.36
)
FetchContent_GetProperties(glad_src)
if(NOT glad_src_POPULATED)
    FetchContent_Populate(glad_src)
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(GLAD_OUT_DIR ${CMAKE_BINARY_DIR}/glad_gen)
file(MAKE_DIRECTORY ${GLAD_OUT_DIR})

# Execute glad generator during configuration
execute_process(
    COMMAND ${CMAKE_COMMAND} -E env PYTHONPATH=${glad_src_SOURCE_DIR} ${Python3_EXECUTABLE} -m glad --generator=c --out-path=${GLAD_OUT_DIR} --profile=core --api=gl=4.6
    RESULT_VARIABLE GLAD_GEN_RESULT
)

if(NOT GLAD_GEN_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to generate GLAD")
endif()

add_library(glad ${GLAD_OUT_DIR}/src/glad.c)
target_include_directories(glad PUBLIC ${GLAD_OUT_DIR}/include)

# nlohmann_json
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.2
)
FetchContent_GetProperties(json)
if(NOT json_POPULATED)
    FetchContent_Populate(json)
    add_library(nlohmann_json INTERFACE)
    target_include_directories(nlohmann_json INTERFACE ${json_SOURCE_DIR}/include)
    add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)
endif()

# ImGui
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.89.9
)
FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
    FetchContent_Populate(imgui)
    
    add_library(imgui
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    )
    target_include_directories(imgui PUBLIC 
        ${imgui_SOURCE_DIR} 
        ${imgui_SOURCE_DIR}/backends
    )
    target_link_libraries(imgui PUBLIC glfw glad)
endif()

# Main Project
file(GLOB_RECURSE SOURCES "src/*.cpp" "src/*.h")

add_executable(CelShadingRenderer ${SOURCES})

target_include_directories(CelShadingRenderer PRIVATE 
    src 
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

target_link_libraries(CelShadingRenderer PRIVATE
    glfw
    glm
    assimp
    imgui
    glad
    nlohmann_json::nlohmann_json
)

# Copy assets to build directory
add_custom_command(TARGET CelShadingRenderer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/assets
    $<TARGET_FILE_DIR:CelShadingRenderer>/assets
)
