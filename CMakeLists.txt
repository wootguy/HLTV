cmake_minimum_required(VERSION 3.6)

project(HLTV)

set(BUILD_TESTING OFF)
set(XZ_TOOL_XZ OFF)
set(XZ_TOOL_XZDEC OFF)
set(XZ_TOOL_LZMADEC OFF)
set(XZ_TOOL_LZMAINFO OFF)
if (UNIX)
	set(XZ_SMALL ON) # fixes build error for 32bit
endif()
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/lib/xz)

if (UNIX)
	set_target_properties(liblzma PROPERTIES COMPILE_OPTIONS "-m32" LINK_FLAGS "-m32")
endif()

include_directories(${CMAKE_CURRENT_LIST_DIR}/lib/xz/src/liblzma/api)


add_library(${PROJECT_NAME} SHARED
	src/main.h src/main.cpp
	src/SvenTV.h src/SvenTV.cpp
	src/DemoWriter.h src/DemoWriter.cpp
	src/DemoPlayer.h src/DemoPlayer.cpp
	src/DemoPlayerEnt.h src/DemoPlayerEnt.cpp
	src/DemoStats.h src/DemoStats.cpp
	src/NetClient.h src/NetClient.cpp
	src/netedict.h src/netedict.cpp
	src/DemoFile.h src/DemoFile.cpp
	src/lzma.h src/lzma.cpp
)

hlcoop_setup_plugin("plugins/server/")

target_link_libraries(${PROJECT_NAME} PRIVATE liblzma)
