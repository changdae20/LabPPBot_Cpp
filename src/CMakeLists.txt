SET(LabPPBot_Cpp_SRC
    util.h
    util.cpp
    core.h
    core.cpp
    HTTPRequest.hpp
    utf8_core.h
    checked.h
)

add_subdirectory(protobuf)

link_directories(
	#${CMAKE_LIBRARY_OUTPUT_DIRECTORY}
)

set(protobuf_DIR ${CMAKE_SOURCE_DIR}/vcpkg/packages/protobuf_x64-windows/share/protobuf)
set(fmt_DIR ${CMAKE_SOURCE_DIR}/vcpkg/packages/fmt_x64-windows/share/fmt)

find_package(fmt CONFIG REQUIRED)

SET(LabPPBot_Cpp_LIB
    COMPILED_PROTOBUF
)

add_library(${PROJECT_NAME}
    ${LabPPBot_Cpp_SRC}
)

add_executable(main
	${PROJECT_SOURCE_DIR}/src/main.cpp
    ${PROTOBUF_SRC_FILES}
)

target_link_libraries(main
    COMPILED_PROTOBUF
    LabPPBot_Cpp
)

###################################################################################################
# - library targets -------------------------------------------------------------------------------

file(GLOB OPENCV_DLL_FILES
	"${OpenCV_INCLUDE_DIRS}/../x64/vc15/bin/*.dll"
)

file(COPY ${OPENCV_DLL_FILES} DESTINATION "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Release")
	
target_link_libraries(${PROJECT_NAME}
    ${OpenCV_LIBS}
    ${LabPPBot_Cpp_LIB}
    libprotoc 
    libprotobuf 
    libprotobuf-lite
    fmt::fmt
)