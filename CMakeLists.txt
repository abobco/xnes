# compile with opengl, glad, sdl2, and openal

cmake_minimum_required(VERSION 3.3)
set(CMAKE_BUILD_TYPE Debug)

set(XNLIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

set(GLAD_DIR ${XNLIB_DIR}/external/glad)
set(IMGUI_DIR ${XNLIB_DIR}/external/imgui)
set(SDL_DIR ${XNLIB_DIR}/external/SDL2)
set(GLM_DIR ${XNLIB_DIR}/external/glm )
set(OAL_DIR ${XNLIB_DIR}/external/OpenAL-1.1-SDK)

project(opengl_impl)
set( opengl_impl_src  
    ${GLAD_DIR}/src/glad.c
    ${XNLIB_DIR}/external/stb_image_impl.cpp
)
include_directories(${GLAD_DIR}/include )
add_library(opengl_impl STATIC ${opengl_impl_src} )

macro(SDL_Build project_name src_file)
    project( ${project_name} ) 
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -g") 
    set (extra_macro_args ${ARGN})
    

    if (${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
        find_library(SDL2_LIBRARIES NAME SDL2 PATHS ${SDL_DIR}/lib/x64)
        find_file(SDL2_INCLUDE_DIR NAME SDL.h PATHS ${SDL_DIR}/include HINTS SDL2 )
        set(OPENAL_INCLUDE_DIR ${OAL_DIR}/include )
        set(OPENAL_LIBRARY ${OAL_DIR}/libs/Win64/OpenAL32.lib )
    else()
        find_package(SDL2 REQUIRED)
    endif()

    include (FindOpenAL)
    add_executable( ${PROJECT_NAME}  
        ${IMGUI_DIR}/imgui.cpp
        ${IMGUI_DIR}/imgui_draw.cpp 
        ${IMGUI_DIR}/imgui_demo.cpp 
        ${IMGUI_DIR}/imgui_tables.cpp 
        ${IMGUI_DIR}/imgui_widgets.cpp
        ${IMGUI_DIR}/backends/imgui_impl_sdl.h
        ${IMGUI_DIR}/backends/imgui_impl_sdl.cpp
        ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
        src/nes/cpu.cpp
        src/nes/ppu.cpp
        src/nes/mappers.cpp
        
        ${XNLIB_DIR}/graphics/xn_sdl.cpp
        ${XNLIB_DIR}/util/xn_json.cpp
        # src/emulator.cpp
        ${src_file} 
    )

    # find_library(SDL2_LIBRARY NAME SDL2 PATHS ${SDL_DIR}/lib/x64)
    # find_file(SDL2_INCLUDE_DIR NAME SDL.h PATHS ${SDL_DIR}/include HINTS SDL2 )

    set(LIB_LIST 
        ${SDL2_LIBRARIES} 
        opengl_impl 
        ${OPENAL_LIBRARY} 
        ${ALUT_LIB})

    message(${CMAKE_SYSTEM_NAME})
    if (${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
        set(LIB_LIST ${LIB_LIST} 
            pthread 
            dl 
            stdc++fs)
    endif()

    target_link_libraries( ${PROJECT_NAME} ${LIB_LIST})
    target_include_directories(${PROJECT_NAME} PRIVATE
        ${SDL_DIR}/include 
        ${SDL2_INCLUDE_DIR}
        ${XNLIB_DIR}  
        ${GLAD_DIR}/include 
        ${IMGUI_DIR} ${IMGUI_DIR}/backends
        ${GLM_DIR}
        ${OPENAL_INCLUDE_DIR}
        ${ALUT_INCLUDE}
    )


#    target_compile_options(${PROJECT_NAME} PRIVATE /Bt)
endmacro()

SDL_Build( xn_nes src/main.cpp )