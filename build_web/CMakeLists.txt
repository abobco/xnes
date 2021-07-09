cmake_minimum_required(VERSION 3.3)

set(SRC_DIR  ${CMAKE_CURRENT_SOURCE_DIR}/../src)
set(BUILD_DIR html)
set(XNLIB_DIR  ${SRC_DIR})

set(GLAD_DIR ${XNLIB_DIR}/external/glad)

set(IMGUI_DIR ${XNLIB_DIR}/external/imgui_emscripten)
set(SDL_DIR ${XNLIB_DIR}/external/SDL2)
set(GLM_DIR ${XNLIB_DIR}/external/glm )

set(EM_SHELL_FILE ${BUILD_DIR}/shell_minimal.html)

set(FILE_ASSETS
    "assets/config/nes_config.json"
    "assets/gamecontrollerdb.txt"
    "assets/shaders/textures.vs"
    "assets/shaders/textures.fs"
    "assets/SwitchController.png"
    
    "assets/roms/castlevania.nes"
    "assets/roms/castlevania2.nes"
    "assets/roms/contra.nes"
    "assets/roms/donkey.nes" 
    "assets/roms/excitebike.nes"
    "assets/roms/lemmings.nes"
    "assets/roms/mega_man_2.nes"
    "assets/roms/nestest.nes"
    "assets/roms/super_mario_bros.nes"
)

project(opengl_impl)
set( opengl_impl_src  
    ${GLAD_DIR}/src/glad.c
    ${XNLIB_DIR}/external/stb_image_impl.cpp
)
include_directories(${GLAD_DIR}/include )
add_library(opengl_impl STATIC ${opengl_impl_src} )

macro(SDL_Build_EMCC project_name src_file ) 
    project( ${project_name} ) 
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set (extra_macro_args ${ARGN})

    add_executable(${PROJECT_NAME}  
        ${IMGUI_DIR}/imgui.h
        ${IMGUI_DIR}/imgui.cpp
        ${IMGUI_DIR}/imgui_demo.cpp
        ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_impl_sdl.h
        ${IMGUI_DIR}/imgui_impl_sdl.cpp
        ${SRC_DIR}/nes/cpu.cpp
        ${SRC_DIR}/nes/ppu.cpp
        ${SRC_DIR}/nes/mappers.cpp
        
        ${XNLIB_DIR}/graphics/xn_sdl.cpp
        ${XNLIB_DIR}/util/xn_json.cpp
        ${src_file} 
    )

    set_target_properties( ${PROJECT_NAME}
        PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${BUILD_DIR}
    )

    set(emcc_compiler_flags "")
    set(emcc_compiler_flags "${emcc_compiler_flags}  -fno-rtti -O3 -std=c++17 --shell-file ${EM_SHELL_FILE}")

    set(emcc_linker_flags "${emcc_linker_flags}  -O3 -std=c++17  -s WASM=1 ")  
    set(emcc_linker_flags "${emcc_linker_flags}  --shell-file ${EM_SHELL_FILE}")  

    # enabled debug assertions, exceptions (slow)
    set(emcc_linker_flags "${emcc_linker_flags} -s ASSERTIONS=1 -s NO_DISABLE_EXCEPTION_CATCHING -s ALLOW_MEMORY_GROWTH=1")
    
    # turn off stuff with performance overhead
    # set(emcc_linker_flags "${emcc_linker_flags}  -s ALLOW_MEMORY_GROWTH=0 -s ASSERTIONS=0  -fno-rtti")
    # set(emcc_linker_flags "${emcc_linker_flags}  --profiling")

    # build with SDL
    set(emcc_linker_flags "${emcc_linker_flags} -s USE_SDL=2")

    # build with opengl
    set(emcc_linker_flags "${emcc_linker_flags} -s USE_WEBGL2=1 -s FULL_ES3=1 -s MIN_WEBGL_VERSION=1 -s MAX_WEBGL_VERSION=2") 
    set(emcc_linker_flags "${emcc_linker_flags} -s GL_UNSAFE_OPTS=1") 
    set(emcc_linker_flags "${emcc_linker_flags} -s STB_IMAGE=1") 

    # build with openal
    set(emcc_linker_flags "${emcc_linker_flags}  -lopenal")  
    
    # foreach(asset ${FILE_ASSETS})
    #     set(emcc_linker_flags "${emcc_linker_flags} --embed-file ${asset}")
    # endforeach()
    set(emcc_linker_flags "${emcc_linker_flags} --preload-file ../assets@assets/")

    target_include_directories(${PROJECT_NAME} PRIVATE
        ${XNLIB_DIR}  
        ${IMGUI_DIR} ${IMGUI_DIR}/backends
        ${GLM_DIR}
    )

    set_target_properties(${PROJECT_NAME}  
    PROPERTIES SUFFIX ".html"
    LINK_FLAGS ${emcc_linker_flags}
    COMPILE_FLAGS ${emcc_compiler_flags} )
    em_link_js_library(${PROJECT_NAME}  ${libraryJsFiles})
    add_definitions(-D_USE_MATH_DEFINES)
    target_include_directories(${PROJECT_NAME} PRIVATE ${XNLIB_DIR} ${GLM_DIR} ${SDL2_INCLUDE_DIRS} ${OPENGL_INCLUDE_DIRS} )
endmacro()

SDL_Build_EMCC(nes ${SRC_DIR}/main.cpp)