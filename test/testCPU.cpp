#include <catch2/catch.hpp>

#include "cpu.hpp"

TEST_CASE("CPU can execute", "[cpu]") {
	auto cpu = CPU();
	cpu.execute();

	REQUIRE(cpu.pc == 0x601);
}
