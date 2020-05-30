cmake_minimum_required(VERSION 3.5)
find_package(Catch2 REQUIRED)

add_executable(microlator_test
	main.cpp
	testCPU.cpp
)

target_include_directories(microlator_test
PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/test
)

target_link_libraries(microlator_test
	microlator
)

target_compile_options(microlator_test
PRIVATE
	-Wall
	-Wextra
	-Werror
	-Wpedantic
)

target_compile_features(microlator_test
PRIVATE
	cxx_std_20
)

add_test(
	NAME    microlator_test
	COMMAND microlator_test
)
