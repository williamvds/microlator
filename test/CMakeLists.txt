cmake_minimum_required(VERSION 3.5)
find_package(Catch2 REQUIRED)

add_executable(microlator_test
	main.cpp
	testCPU.cpp
)

target_link_libraries(microlator_test
	microlator
	-fsanitize=address
	-fsanitize=undefined
)

target_compile_options(microlator_test
PRIVATE
	-fsanitize=address
	-fsanitize=undefined
)

add_test(
	NAME    microlator_test
	COMMAND microlator_test
)
