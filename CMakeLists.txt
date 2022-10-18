cmake_minimum_required (VERSION 3.0)
project(usd2glb)

add_subdirectory(tinyusdz)

set (SOURCES
main.cpp
image.h
)


set (INCLUDE_DIR
tinyusdz/src
tinyusdz/src/external
glm/glm
.
)


if (WIN32) 
set (DEFINES  ${DEFINES}
-D"_CRT_SECURE_NO_DEPRECATE"  
-D"_SCL_SECURE_NO_DEPRECATE" 
-D"_CRT_SECURE_NO_WARNINGS"
)
else()
add_definitions(-std=c++17)
add_compile_options(-fPIC)
endif()

include_directories(${INCLUDE_DIR})
add_definitions(${DEFINES})
add_executable(usd2glb ${SOURCES})
target_link_libraries(usd2glb tinyusdz_static) 

