#include <iostream>
#include <sstream>

#include <catch2/catch.hpp>

#include "cpu.hpp"
#include "nestest.hpp"

namespace emu = microlator;

namespace Catch {
	template<>
	struct StringMaker<emu::Flags> {
		static auto convert(const emu::Flags& value) -> std::string {
			constexpr auto flags = std::to_array<char>({
				'C', 'Z', 'I', 'D', 'B', '-', 'V', 'N'
			});

			std::ostringstream os;
			os << '[';
			for (size_t i = 0; i <= flags.size(); i++) {
				size_t bit = flags.size() - i;
				os <<
					(value.test(static_cast<emu::Flags::Index>(bit))
					? flags.at(bit)
					: ' ');
			}
			os << ']';

			return {os.str()};
		}
	};
} // namespace Catch

TEST_CASE("CPU can execute", "[cpu]") {
	auto cpu = emu::CPU();
	cpu.step();

	REQUIRE(cpu.pc == 0x601);
}

TEST_CASE("CPU passes nestest", "[cpu]") {
	auto cpu = emu::CPU();
	cpu.loadProgram(nestestProgram, 0x8000);
	cpu.loadProgram(nestestProgram, 0xC000);

	const auto *prev = nestestStates.begin();
	for (const auto *it = nestestStates.begin(); it != nestestStates.end(); ++it) {
		const auto state = *it;

		INFO("Last instruction: " << prev->dis);
		INFO("PC: " << std::hex << state.pc);

		REQUIRE(static_cast<unsigned>(cpu.pc) == static_cast<unsigned>(state.pc));
		REQUIRE(static_cast<unsigned>(cpu.accumulator) == static_cast<unsigned>(state.a));
		REQUIRE(static_cast<unsigned>(cpu.indexX) == static_cast<unsigned>(state.x));
		REQUIRE(static_cast<unsigned>(cpu.indexY) == static_cast<unsigned>(state.y));
		REQUIRE(cpu.flags == state.p);
		REQUIRE(static_cast<unsigned>(cpu.stack)
			 == static_cast<unsigned>(state.sp));

		if (!cpu.step())
			break;

		prev = it;
	}
}
