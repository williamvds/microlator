#include <algorithm>
#include <cassert>
#include <functional>
#include <stdexcept>

#include "cpu.hpp"

namespace {

constexpr auto u8Max = 0xffU;
constexpr auto u8Modulo = 0x100U;
constexpr auto u16Upper = 0xff00U;

constexpr auto toU8(auto value) { return static_cast<uint8_t>(value); }

constexpr auto toU16(auto value) { return static_cast<uint16_t>(value); }

constexpr auto toBool(auto value) { return static_cast<bool>(value); }

constexpr auto setBit(uint8_t index, uint8_t value, bool set) -> uint8_t {
	return toU8(value | toU8(toU8(set) << index));
}

constexpr auto getBit(uint8_t index, uint8_t value) -> bool {
	return toBool(toU8(value >> index) & 1U);
}

constexpr auto isNegative(uint8_t value) -> bool {
	// Two's complement: top bit means negative
	return getBit(7, value);
}

constexpr auto sign(uint8_t value) -> int8_t {
	return isNegative(value) ? -1 : 1;
}

constexpr auto wrapToByte(size_t value) -> uint8_t {
	return toU8(value % u8Modulo);
}

} // namespace

namespace microlator {

using F = Flags::Index;

constexpr void ValueStore::write(uint8_t newValue) noexcept {
	assert(type != Type::Implicit && type != Type::Value);

	switch (type) {
	case Type::Accumulator: {
		cpu.accumulator = newValue;
		return;
	}
	case Type::Memory: {
		cpu.write(value, newValue);
		return;
	}
	case Type::Implicit:
	case Type::Value:
		break;
	}
}

constexpr auto ValueStore::read() const noexcept -> uint16_t {
	assert(type != Type::Implicit);

	switch (type) {
	case Type::Accumulator:
		return cpu.accumulator;
	case Type::Memory:
		return cpu.read(value);
	case Type::Value:
		return value;
	case Type::Implicit:
		break;
	}

	return {};
}

constexpr void CPU::reset() {
	memory = Memory{};
	pc = initialProgramCounter;
	stack = initialStackPointer;
	flags.reset();
}

void CPU::loadProgram(const std::span<const uint8_t> program, uint16_t offset) {
	if (offset + program.size() > memory.size())
		throw std::invalid_argument{"Program can't fit in memory"};

	std::copy(program.begin(), program.end(), memory.begin() + offset);
	pc = offset;
}

void CPU::loadProgram(const std::span<const uint8_t> program) {
	loadProgram(program, initialProgramCounter);
}

auto CPU::step() noexcept -> bool {
	const auto opcode = read(pc++);

	static auto instructions = CPU::getInstructions();
	const auto instruction = instructions.at(opcode);
	if (!instruction.function)
		return false;

	const auto target = getTarget(instruction.addressMode);
	std::invoke(instruction.function, this, target);

	return true;
}

// Get the target address depending on the addressing mode
constexpr auto CPU::getTarget(AddressMode mode) noexcept -> ValueStore {
	using Mode = AddressMode;

	assert(mode >= Mode::Implicit && mode <= Mode::ZeropageY);

	CPU &self = *this;
	switch (mode) {
	// Instruction makes target implicit, e.g. CLC
	case Mode::Implicit: {
		return {self, 0, ValueStore::Type::Implicit};
	}

	// Use value of accumulator, e.g. LSL A
	case Mode::Accumulator: {
		return ValueStore(self);
	}

	// Use value at next address e.g. LDX #$00
	case Mode::Immediate: {
		return {self, read(pc++), ValueStore::Type::Value};
	}

	// Use 16-bit value embedded in instruction, e.g. JMP $1234
	case Mode::Absolute: {
		const auto address = read2(pc);
		pc += 2;
		return {self, address};
	}

	// Like Absolute, but add value of register X, e.g. JMP $1234,X
	case Mode::AbsoluteX: {
		return {self, toU16(getTarget(Mode::Absolute).get() + indexX)};
	}

	// Like Absolute, but add value of register Y, e.g. JMP $1234,Y
	case Mode::AbsoluteY: {
		return {self, toU16(getTarget(Mode::Absolute).get() + indexY)};
	}

	// Use the value at the address embedded in the instruction
	// e.g. JMP ($1234)
	case Mode::Indirect: {
		// indirectJumpBug: a hardware bug results in the increment
		// actually flipping the lower byte from 0xff to 0x00
		const uint16_t lowTarget = getTarget(Mode::Absolute).get(),
			       highTarget =
				   indirectJumpBug && (lowTarget & u8Max)
				       ? (lowTarget & u16Upper)
				       : lowTarget + 1;

		return {self,
			toU16((read(highTarget) << 8U) + read(lowTarget))};
	}

	// Like Zeropage, but the X index to the indirect address
	// e.g. LDA ($12,X)
	case Mode::IndirectX: {
		const auto indirectAddr =
		    getTarget(Mode::Zeropage).get() + indexX;
		return {self, read2(indirectAddr, true)};
	}

	// Like Indirect, but the Y index to the final address
	// e.g. LDA ($12),Y
	case Mode::IndirectY: {
		const auto indirectAddr = getTarget(Mode::Zeropage).get();
		return {self, toU16(read2(indirectAddr, true) + indexY)};
	}

	// Use the value embedded in the instruction as a signed offset
	// from the program counter (after the instruction has been decoded)
	case Mode::Relative: {
		const uint8_t value = getTarget(Mode::Immediate).get(),
			      lowerBits = value ^ 0b1000'0000U;
		// Two's complement: when the high bit is set the number is
		// negative, in which case flip the lower bits and add one.
		// If positive the original value is correct
		if (isNegative(value))
			return {self, toU16(pc - (~lowerBits + 1))};

		return {self, toU16(pc + value)};
	}

	// Use the 4-bit value embedded in the instruction as an offset from the
	// beginning of memory
	case Mode::Zeropage: {
		return {self, read(pc++)};
	}

	// Like Zeropage, but add value of register X and wrap within the page
	case Mode::ZeropageX: {
		return {self,
			wrapToByte(getTarget(Mode::Immediate).get() + indexX)};
	}

	// Like Zeropage, but add value of register Y and wrap within the page
	case Mode::ZeropageY: {
		return {self,
			wrapToByte(getTarget(Mode::Immediate).get() + indexY)};
	}
	}

	return ValueStore(self);
}

constexpr void CPU::branch(uint16_t address) noexcept { pc = address; }

constexpr auto CPU::read(uint16_t address) const noexcept -> uint8_t {
	return memory[address];
}

constexpr auto CPU::read2(uint16_t address, bool wrapToPage) const noexcept
    -> uint16_t {
	if (wrapToPage)
		address = wrapToByte(address);

	const auto highAddress = address + 1;
	return memory[address] +
	       (memory[wrapToPage ? wrapToByte(highAddress) : highAddress]
		<< 8U);
}

constexpr void CPU::write(uint16_t address, uint8_t value) noexcept {
	memory[address] = value;
}

constexpr void CPU::push(uint8_t value) noexcept {
	memory[stackTop + stack--] = value;
}

constexpr void CPU::push2(uint16_t value) noexcept {
	push(toU8(value >> 8U));
	push(toU8(value & u8Max));
}

constexpr auto CPU::pop() noexcept -> uint8_t {
	return memory[stackTop + ++stack];
}

constexpr auto CPU::pop2() noexcept -> uint16_t {
	return pop() + (pop() << 8U);
}

constexpr void CPU::popFlags() noexcept {
	auto value = pop();
	value |= Flags::bitmask(F::Unused);
	value &= toU8(~Flags::bitmask(F::Break));
	flags = value;
}

constexpr void CPU::calculateFlag(uint8_t value, Flags::Index flag) noexcept {
	bool result = false;
	assert(flag == F::Carry || flag == F::Zero || flag == F::Negative);

	switch (flag) {
	case F::Carry:
		result = (value == u8Max);
		break;
	case F::Zero:
		result = (value == 0);
		break;
	case F::Negative:
		result = isNegative(value);
		break;
	default:
		return;
	}

	flags.set(flag, result);
}

template <class T, class... Args>
constexpr void CPU::calculateFlag(uint8_t value, T flag, Args... flags) {
	calculateFlag(value, flag);
	calculateFlag(value, flags...);
}

constexpr void CPU::compare(uint8_t a, uint8_t b) noexcept {
	flags.set(F::Zero, a == b);
	flags.set(F::Carry, a >= b);
	flags.set(F::Negative, isNegative(a - b));
}

constexpr void CPU::addWithCarry(uint8_t value) noexcept {
	// TODO: implement decimal mode
	const uint8_t result =
	    accumulator + value + (flags.test(F::Carry) ? 1 : 0);
	calculateFlag(result, F::Zero, F::Negative);

	const auto resultSign = sign(result);
	flags.set(F::Overflow, (sign(accumulator) != resultSign) &&
				   (sign(value) != resultSign));
	flags.set(F::Carry, result < accumulator);

	accumulator = result;
}

constexpr void CPU::oADC(ValueStore address) noexcept {
	addWithCarry(address.read());
}

constexpr void CPU::oAND(ValueStore address) noexcept {
	const auto input = address.read();
	accumulator &= input;
	calculateFlag(accumulator, F::Zero, F::Negative);
}

constexpr void CPU::oASL(ValueStore address) noexcept {
	const auto input = address.read();
	flags.set(F::Carry, getBit(7, input));

	const auto result = input << 1U;
	calculateFlag(result, F::Zero, F::Negative);
	address.write(result);
}

constexpr void CPU::oBCC(ValueStore target) noexcept {
	if (!flags.test(F::Carry))
		branch(target.get());
}

constexpr void CPU::oBCS(ValueStore target) noexcept {
	if (flags.test(F::Carry))
		branch(target.get());
}

constexpr void CPU::oBEQ(ValueStore target) noexcept {
	if (flags.test(F::Zero))
		branch(target.get());
}

constexpr void CPU::oBIT(ValueStore address) noexcept {
	const auto input = address.read();
	flags.set(F::Zero, (input & accumulator) == 0U);
	flags.set(F::Overflow, getBit(6, input));
	flags.set(F::Negative, isNegative(input));
}

constexpr void CPU::oBMI(ValueStore target) noexcept {
	if (flags.test(F::Negative))
		branch(target.get());
}

constexpr void CPU::oBNE(ValueStore target) noexcept {
	if (!flags.test(F::Zero))
		branch(target.get());
}

constexpr void CPU::oBPL(ValueStore target) noexcept {
	if (!flags.test(F::Negative))
		branch(target.get());
}

constexpr void CPU::oBRK(ValueStore) noexcept {
	flags.set(F::InterruptOff, true);

	push2(pc);
	push(toU8(flags.get()));
}

constexpr void CPU::oBVC(ValueStore target) noexcept {
	if (!flags.test(F::Overflow))
		branch(target.get());
}

constexpr void CPU::oBVS(ValueStore target) noexcept {
	if (flags.test(F::Overflow))
		branch(target.get());
}

constexpr void CPU::oCLC(ValueStore) noexcept { flags.set(F::Carry, false); }

constexpr void CPU::oCLD(ValueStore) noexcept { flags.set(F::Decimal, false); }

constexpr void CPU::oCLI(ValueStore) noexcept {
	flags.set(F::InterruptOff, false);
}

constexpr void CPU::oCLV(ValueStore) noexcept { flags.set(F::Overflow, false); }

constexpr void CPU::oCMP(ValueStore address) noexcept {
	const auto input = address.read();
	compare(accumulator, input);
}

constexpr void CPU::oCPX(ValueStore address) noexcept {
	const auto input = address.read();
	compare(indexX, input);
}

constexpr void CPU::oCPY(ValueStore address) noexcept {
	const auto input = address.read();
	compare(indexY, input);
}

constexpr void CPU::oDEC(ValueStore address) noexcept {
	const auto input = address.read();
	const auto result = input - 1;
	calculateFlag(result, F::Zero, F::Negative);
	address.write(result);
}

constexpr void CPU::oDEX(ValueStore) noexcept {
	const auto result = indexX - 1;
	calculateFlag(result, F::Zero, F::Negative);
	indexX = result;
}

constexpr void CPU::oDEY(ValueStore) noexcept {
	const auto result = indexY - 1;
	calculateFlag(result, F::Zero, F::Negative);
	indexY = result;
}

constexpr void CPU::oEOR(ValueStore address) noexcept {
	const auto input = address.read();
	accumulator = accumulator ^ input;
	calculateFlag(accumulator, F::Zero, F::Negative);
}

constexpr void CPU::oINC(ValueStore address) noexcept {
	const auto input = address.read();
	const auto result = input + 1;
	calculateFlag(result, F::Zero, F::Negative);
	address.write(result);
}

constexpr void CPU::oINX(ValueStore) noexcept {
	const auto result = indexX + 1;
	calculateFlag(result, F::Zero, F::Negative);
	indexX = result;
}

constexpr void CPU::oINY(ValueStore) noexcept {
	const auto result = indexY + 1;
	calculateFlag(result, F::Zero, F::Negative);
	indexY = result;
}

constexpr void CPU::oJMP(ValueStore target) noexcept { pc = target.get(); }

constexpr void CPU::oJSR(ValueStore target) noexcept {
	push2(toU16(pc - 1));
	pc = target.get();
}

constexpr void CPU::oLDA(ValueStore address) noexcept {
	const auto input = address.read();
	accumulator = input;
	calculateFlag(input, F::Zero, F::Negative);
}

constexpr void CPU::oLDX(ValueStore address) noexcept {
	const auto input = address.read();
	indexX = input;
	calculateFlag(input, F::Zero, F::Negative);
}

constexpr void CPU::oLDY(ValueStore address) noexcept {
	const auto input = address.read();
	indexY = input;
	calculateFlag(input, F::Zero, F::Negative);
}

constexpr void CPU::oLSR(ValueStore address) noexcept {
	const auto input = address.read();
	const auto result = input >> 1U;
	calculateFlag(result, F::Zero, F::Negative);
	flags.set(F::Carry, getBit(0, input));
	address.write(result);
}

constexpr void CPU::oNOP(ValueStore) noexcept {}

constexpr void CPU::oORA(ValueStore address) noexcept {
	const auto input = address.read();
	const auto result = accumulator | input;
	calculateFlag(result, F::Zero, F::Negative);
	accumulator = result;
}

constexpr void CPU::oPHA(ValueStore) noexcept { push(accumulator); }

constexpr void CPU::oPHP(ValueStore) noexcept {
	push(toU8(flags.get() | Flags::bitmask(F::Break)));
}

constexpr void CPU::oPLA(ValueStore) noexcept {
	accumulator = pop();
	calculateFlag(accumulator, F::Zero, F::Negative);
}

constexpr void CPU::oPLP(ValueStore) noexcept { popFlags(); }

constexpr void CPU::oROL(ValueStore address) noexcept {
	const auto input = address.read();
	const auto result = setBit(0, input << 1U, flags.test(F::Carry));

	flags.set(F::Carry, getBit(7, input));
	calculateFlag(result, F::Zero, F::Negative);
	address.write(result);
}

constexpr void CPU::oROR(ValueStore address) noexcept {
	const auto input = address.read();
	const auto result = setBit(7, input >> 1U, flags.test(F::Carry));

	flags.set(F::Carry, getBit(0, input));
	calculateFlag(result, F::Zero, F::Negative);
	address.write(result);
}

constexpr void CPU::oRTI(ValueStore) noexcept {
	popFlags();
	pc = pop2();
}

constexpr void CPU::oRTS(ValueStore) noexcept { pc = pop2() + 1; }

constexpr void CPU::oSBC(ValueStore address) noexcept {
	addWithCarry(~address.read());
}

constexpr void CPU::oSEC(ValueStore) noexcept { flags.set(F::Carry, true); }

constexpr void CPU::oSED(ValueStore) noexcept { flags.set(F::Decimal, true); }

constexpr void CPU::oSEI(ValueStore) noexcept {
	flags.set(F::InterruptOff, true);
}

constexpr void CPU::oSTA(ValueStore address) noexcept {
	address.write(accumulator);
}

constexpr void CPU::oSTX(ValueStore address) noexcept { address.write(indexX); }

constexpr void CPU::oSTY(ValueStore address) noexcept { address.write(indexY); }

constexpr void CPU::oTAX(ValueStore) noexcept {
	indexX = accumulator;
	calculateFlag(indexX, F::Zero, F::Negative);
}

constexpr void CPU::oTAY(ValueStore) noexcept {
	indexY = accumulator;
	calculateFlag(indexY, F::Zero, F::Negative);
}

constexpr void CPU::oTSX(ValueStore) noexcept {
	indexX = stack;
	calculateFlag(indexX, F::Zero, F::Negative);
}

constexpr void CPU::oTXA(ValueStore) noexcept {
	accumulator = indexX;
	calculateFlag(accumulator, F::Zero, F::Negative);
}

constexpr void CPU::oTXS(ValueStore) noexcept { stack = indexX; }

constexpr void CPU::oTYA(ValueStore) noexcept {
	accumulator = indexY;
	calculateFlag(accumulator, F::Zero, F::Negative);
}

constexpr auto CPU::getInstructions() -> Instructions {
	using C = CPU;
	using M = AddressMode;

	return {{
	    // clang-format off
		{&C::oBRK         }, {&C::oORA, M::IndX}, {                 }, {},
		{                 }, {&C::oORA, M::Zpg }, {&C::oASL, M::Zpg }, {},
		{&C::oPHP         }, {&C::oORA, M::Imm }, {&C::oASL, M::A   }, {},
		{                 }, {&C::oORA, M::Abs }, {&C::oASL, M::Abs }, {},
		{&C::oBPL, M::Rel }, {&C::oORA, M::IndY}, {                 }, {},
		{                 }, {&C::oORA, M::ZpgX}, {&C::oASL, M::ZpgX}, {},
		{&C::oCLC         }, {&C::oORA, M::AbsY}, {                 }, {},
		{                 }, {&C::oORA, M::AbsX}, {&C::oASL, M::AbsX}, {},

		{&C::oJSR, M::Abs }, {&C::oAND, M::IndX}, {                 }, {},
		{&C::oBIT, M::Zpg }, {&C::oAND, M::Zpg }, {&C::oROL, M::Zpg }, {},
		{&C::oPLP         }, {&C::oAND, M::Imm }, {&C::oROL, M::A   }, {},
		{&C::oBIT, M::Abs }, {&C::oAND, M::Abs }, {&C::oROL, M::Abs }, {},
		{&C::oBMI, M::Rel }, {&C::oAND, M::IndY}, {                 }, {},
		{                 }, {&C::oAND, M::ZpgX}, {&C::oROL, M::ZpgX}, {},
		{&C::oSEC         }, {&C::oAND, M::AbsY}, {                 }, {},
		{                 }, {&C::oAND, M::AbsX}, {&C::oROL, M::AbsX}, {},

		{&C::oRTI         }, {&C::oEOR, M::IndX}, {                 }, {},
		{                 }, {&C::oEOR, M::Zpg }, {&C::oLSR, M::Zpg }, {},
		{&C::oPHA         }, {&C::oEOR, M::Imm }, {&C::oLSR, M::A   }, {},
		{&C::oJMP, M::Abs }, {&C::oEOR, M::Abs }, {&C::oLSR, M::Abs }, {},
		{&C::oBVC, M::Rel }, {&C::oEOR, M::IndY}, {                 }, {},
		{                 }, {&C::oEOR, M::ZpgX}, {&C::oLSR, M::ZpgX}, {},
		{&C::oCLI         }, {&C::oEOR, M::AbsY}, {                 }, {},
		{                 }, {&C::oEOR, M::AbsX}, {&C::oLSR, M::AbsX}, {},

		{&C::oRTS         }, {&C::oADC, M::IndX}, {                 }, {},
		{                 }, {&C::oADC, M::Zpg }, {&C::oROR, M::Zpg }, {},
		{&C::oPLA         }, {&C::oADC, M::Imm }, {&C::oROR, M::A   }, {},
		{&C::oJMP, M::Ind }, {&C::oADC, M::Abs }, {&C::oROR, M::Abs }, {},
		{&C::oBVS, M::Rel }, {&C::oADC, M::IndY}, {                 }, {},
		{                 }, {&C::oADC, M::ZpgX}, {&C::oROR, M::ZpgX}, {},
		{&C::oSEI         }, {&C::oADC, M::AbsY}, {                 }, {},
		{                 }, {&C::oADC, M::AbsX}, {&C::oROR, M::AbsX}, {},

		{                 }, {&C::oSTA, M::IndX}, {                 }, {},
		{&C::oSTY, M::Zpg }, {&C::oSTA, M::Zpg }, {&C::oSTX, M::Zpg }, {},
		{&C::oDEY         }, {                 }, {&C::oTXA         }, {},
		{&C::oSTY, M::Abs }, {&C::oSTA, M::Abs }, {&C::oSTX, M::Abs }, {},
		{&C::oBCC, M::Rel }, {&C::oSTA, M::IndY}, {                 }, {},
		{&C::oSTY, M::ZpgX}, {&C::oSTA, M::ZpgX}, {&C::oSTX, M::ZpgY}, {},
		{&C::oTYA         }, {&C::oSTA, M::AbsY}, {&C::oTXS         }, {},
		{                 }, {&C::oSTA, M::AbsX}, {                 }, {},

		{&C::oLDY, M::Imm }, {&C::oLDA, M::IndX}, {&C::oLDX, M::Imm }, {},
		{&C::oLDY, M::Zpg }, {&C::oLDA, M::Zpg }, {&C::oLDX, M::Zpg }, {},
		{&C::oTAY         }, {&C::oLDA, M::Imm }, {&C::oTAX         }, {},
		{&C::oLDY, M::Abs }, {&C::oLDA, M::Abs }, {&C::oLDX, M::Abs }, {},
		{&C::oBCS, M::Rel }, {&C::oLDA, M::IndY}, {                 }, {},
		{&C::oLDY, M::ZpgX}, {&C::oLDA, M::ZpgX}, {&C::oLDX, M::ZpgY}, {},
		{&C::oCLV         }, {&C::oLDA, M::AbsY}, {&C::oTSX         }, {},
		{&C::oLDY, M::AbsX}, {&C::oLDA, M::AbsX}, {&C::oLDX, M::AbsY}, {},

		{&C::oCPY, M::Imm }, {&C::oCMP, M::IndX}, {                 }, {},
		{&C::oCPY, M::Zpg }, {&C::oCMP, M::Zpg }, {&C::oDEC, M::Zpg }, {},
		{&C::oINY         }, {&C::oCMP, M::Imm }, {&C::oDEX         }, {},
		{&C::oCPY, M::Abs }, {&C::oCMP, M::Abs }, {&C::oDEC, M::Abs }, {},
		{&C::oBNE, M::Rel }, {&C::oCMP, M::IndY}, {                 }, {},
		{                 }, {&C::oCMP, M::ZpgX}, {&C::oDEC, M::ZpgX}, {},
		{&C::oCLD         }, {&C::oCMP, M::AbsY}, {                 }, {},
		{                 }, {&C::oCMP, M::AbsX}, {&C::oDEC, M::AbsX}, {},

		{&C::oCPX, M::Imm }, {&C::oSBC, M::IndX}, {                 }, {},
		{&C::oCPX, M::Zpg }, {&C::oSBC, M::Zpg }, {&C::oINC, M::Zpg }, {},
		{&C::oINX         }, {&C::oSBC, M::Imm }, {&C::oNOP         }, {},
		{&C::oCPX, M::Abs }, {&C::oSBC, M::Abs }, {&C::oINC, M::Abs }, {},
		{&C::oBEQ, M::Rel }, {&C::oSBC, M::IndY}, {                 }, {},
		{                 }, {&C::oSBC, M::ZpgX}, {&C::oINC, M::ZpgX}, {},
		{&C::oSED         }, {&C::oSBC, M::AbsY}, {                 }, {},
		{                 }, {&C::oSBC, M::AbsX}, {&C::oINC, M::AbsX}, {},
	    // clang-format on
	}};
}

} // namespace microlator
