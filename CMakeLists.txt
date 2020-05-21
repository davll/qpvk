cmake_minimum_required(VERSION 3.16)

project(qpvk C CXX)

find_package(SDL2 CONFIG REQUIRED)

add_executable(qpvk)

target_sources(qpvk
PRIVATE
    main.c
    cgltf.c
    log.c
    argparse.c
    stb_image.c
    stb_image_write.c
    volk.c
    vk_mem_alloc.cc
)

target_link_libraries(qpvk
PRIVATE
    SDL2::SDL2
)

set_target_properties(qpvk
PROPERTIES
    C_STANDARD 99
    CXX_STANDARD 11
)

target_compile_options(qpvk
PRIVATE
    "-g"
    "-Wall"
)

target_compile_definitions(qpvk
PRIVATE
    "LOG_USE_COLOR"
)
