cmake_minimum_required(VERSION 3.10)

PROJECT ("LabPPBot_Cpp")

include (MacroAddFileDependencies)
include(FindProtobuf)

add_definitions(
  -D_WIN32_WINNT=0x0600
)

if (WIN32)
    if (MSVC)
        # Standard:          /DWIN32 /D_WINDOWS /W3 /Zm1000 /EHsc /GR
        # Reference: https://msdn.microsoft.com/en-us/library/fwkeyyhe.aspx
        # WIN32 --> Win32 console application
        # Wx  --> Warning level
        # Zi  --> Produces a program database (PDB) for complete debug information
        # EHsc --> Exception handling model (catches C++ exceptions only)
        # GR(-) --> Enables / Disables Run-Time Type Information
        # MP  --> Multi-processes compilation
        # openmp --> Enables openmp
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP /bigobj")
    endif (MSVC)
endif (WIN32)

# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fpermissive")

###########################################
# - variables -----------------------------

set(ENABLE_EXAMPLE_PROJECT ON CACHE BOOL "Make example project")

###########################################
# - build type ----------------------------
# Set a default build type if none was specified
IF(NOT MSVC)
  set(DEFAULT_BUILD_TYPE "Release")

  if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Setting build type to '${DEFAULT_BUILD_TYPE}' since none specified.")
  set(CMAKE_BUILD_TYPE "${DEFAULT_BUILD_TYPE}" CACHE
      STRING "Choose the type of build." FORCE)
  # Set the possible values of build type for cmake-gui
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
      "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
  endif()
endif()

###########################################
# - compiler options ----------------------

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
ADD_DEFINITIONS(-D_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS)

add_definitions(-DWIN32_LEAN_AND_MEAN )

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

if(NOT CMAKE_DEBUG_POSTFIX)
  set(CMAKE_DEBUG_POSTFIX d)
endif()

###########################################
# - Windows crt library option ------------

foreach(flag_var
    CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
    CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO)
    string(REGEX REPLACE "/MD" "/MT" ${flag_var} "${${flag_var}}")
endforeach(flag_var)

###########################################
# - prebuilt paths setting ----------------
# set include path

find_package( OpenCV REQUIRED )

if( OpenCV_FOUND )
    message(STATUS "OpenCV IncludeDir ${OpenCV_INCLUDE_DIRS}")
    message(STATUS "OpenCV Libs ${OpenCV_LIBS}")
endif()

include_directories(
  ${OpenCV_INCLUDE_DIRS}
  ${CMAKE_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}/src/protobuf
  ${CMAKE_SOURCE_DIR}/vcpkg/packages/protobuf_x64-windows/include
  ${CMAKE_SOURCE_DIR}/vcpkg/packages/fmt_x64-windows/include
  ${CMAKE_SOURCE_DIR}/vcpkg/packages/freetype_x64-windows/include
)

# INCLUDE_DIRECTORIES
get_property(dirs DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY INCLUDE_DIRECTORIES)
foreach(dir ${dirs})
  message(STATUS " * Include_directories List : ${dir}")
endforeach()

link_directories(
    ${CMAKE_SOURCE_DIR}/vcpkg/packages/protobuf_x64-windows/lib
)

###########################################
# - subdirectory --------------------------
add_subdirectory(src)