find_package(SDL2 REQUIRED)

include_directories(${SDL2_INCLUDE_DIRS})

add_executable(client 
    client.cpp 
    ../common/protocol.hpp 
    ../common/utils.hpp
)

target_link_libraries(client ${SDL2_LIBRARIES} SDL2_mixer SDL2_ttf)
