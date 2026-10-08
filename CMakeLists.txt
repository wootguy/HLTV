cmake_minimum_required(VERSION 3.6)

option(USE_XZ "Build XZ library for compression in-plugin. Slower build but no script needed to compress demos later." OFF)

project(HLTV)

set(SOURCE
	src/main.h src/main.cpp
	src/SvenTV.h src/SvenTV.cpp
	src/DemoWriter.h src/DemoWriter.cpp
	src/DemoPlayer.h src/DemoPlayer.cpp
	src/DemoStats.h src/DemoStats.cpp
	src/NetClient.h src/NetClient.cpp
	src/netedict.h src/netedict.cpp
	src/DemoFile.h src/DemoFile.cpp
	
)

if (USE_XZ)
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
	
	add_compile_definitions(USE_XZ)
	
	set(SOURCE ${SOURCE} src/lzma.h src/lzma.cpp)
	
	include_directories(${CMAKE_CURRENT_LIST_DIR}/lib/xz/src/liblzma/api)
endif()

add_library(${PROJECT_NAME} SHARED ${SOURCE})

hlcoop_setup_plugin("plugins/server/")

if (USE_XZ)
	set_target_properties(libgnu PROPERTIES FOLDER "External")
	set_target_properties(liblzma PROPERTIES FOLDER "External")

	target_link_libraries(${PROJECT_NAME} PRIVATE liblzma)
endif()
