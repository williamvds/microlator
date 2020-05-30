cmake_minimum_required(VERSION 3.5)

project(libmicrolator
	VERSION   0.1.0
	LANGUAGES CXX
)

set(CMAKE_CXX_EXTENSIONS OFF)

add_library(microlator
	src/cpu.cpp
)

target_include_directories(microlator
PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(microlator
PRIVATE
	cxx_std_20
	cxx_auto_type
	cxx_trailing_return_types
	cxx_constexpr
)

target_compile_options(microlator
PRIVATE
	-Wall
	-Wextra
	-Werror
	-Wpedantic

PUBLIC
	$<$<CONFIG:Debug>:
		-fsanitize=address
		-fsanitize=undefined
	>
)

target_link_libraries(microlator
PUBLIC
	$<$<CONFIG:Debug>:
		-fsanitize=address
		-fsanitize=undefined
	>
)

include(CTest)
if (BUILD_TESTING)
	add_subdirectory(test)
	enable_testing()
endif()
