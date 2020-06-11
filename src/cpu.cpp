#include <algorithm>
#include <functional>
#include <stdexcept>

#include "cpu.hpp"

constexpr auto u8Max = 0xffU;
constexpr auto u16Upper = 0xff00U;

constexpr auto setBit(uint8_t index, uint8_t value, bool set) -> uint8_t {
	return value | (static_cast<uint16_t>(set) << index);
}

constexpr auto getBit(uint8_t index, uint8_t value) -> bool {
	return static_cast<bool>((value >> index) & 1U);
}

constexpr auto isNegative(uint8_t value) -> bool {
	// Two's complement: top bit means negative
	return getBit(7, value);
}

constexpr auto wrapToByte(size_t value) -> uint8_t {
	if (value <= u8Max)
		return static_cast<uint8_t>(value);

	// Subtract one if wrapping to remove carry
	return static_cast<uint8_t>((value % u8Max) - 1);
}

constexpr void ValueStore::write(uint8_t newValue) {
	switch(type) {
		case Type::Accumulator: {
			cpu.accumulator = newValue;
			break;
		}
		case Type::Memory: {
			cpu.write(value, newValue);
			break;
		}
		case Type::Value: {
			throw std::logic_error{"Attempt to write to a raw value"};
		}
	}
}

constexpr auto ValueStore::read() const -> uint16_t {
	switch(type) {
		case Type::Accumulator:
			return cpu.accumulator;
		case Type::Memory:
			return cpu.read(value);
		case Type::Value:
			return value;
	}

	throw std::logic_error{"Unhandled ValueStore::read()"};
}

constexpr void CPU::reset() {
	memory = Memory{};
	pc    = initialProgramCounter;
	stack = initialStackPointer;
	flags = initialFlags;
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

auto CPU::step() -> bool {
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
constexpr auto CPU::getTarget(AddressMode mode) -> ValueStore {
	using Mode = AddressMode;
	CPU& self = *this;

	uint16_t targetAddress = 0;

	switch (mode) {
		// Instruction makes target implicit, e.g. CLC
		case Mode::Implicit: {
			targetAddress = 0;
			break;
		}

		// Use value of accumulator, e.g. LSL A
		case Mode::Accumulator:
			return ValueStore(self);

		// Use value at next address e.g. LDX #$00
		case Mode::Immediate: {
			return {self, read(pc++), ValueStore::Type::Value};
		}

		// Use 16-bit value embedded in instruction, e.g. JMP $1234
		case Mode::Absolute: {
			const uint8_t low  = read(pc++),
						  high = read(pc++);
			uint16_t value = (high << 8U) + low;
			return {self, value};
			break;
		}

		// Like Absolute, but add value of register X, e.g. JMP $1234,X
		case Mode::AbsoluteX: {
			targetAddress = getTarget(Mode::Absolute).value + indexX;
			break;
		}

		// Like Absolute, but add value of register Y, e.g. JMP $1234,Y
		case Mode::AbsoluteY: {
			targetAddress = getTarget(Mode::Absolute).value + indexY;
			break;
		}

		// Use the value at the address embedded in the instruction
		// e.g. JMP ($1234)
		case Mode::Indirect: {
			// indirectJumpBug: a hardware bug results in the increment
			// actually flipping the lower byte from 0xff to 0x00
			const uint8_t lowTarget  = read(getTarget(Mode::Absolute).value),
						  highTarget = indirectJumpBug && (lowTarget & u8Max)
				? (lowTarget & u16Upper)
				: lowTarget + 1;

			const auto low  = read(lowTarget),
					   high = read(highTarget);
			targetAddress = (high << 8U) + low;
			break;
		}

		// Like Indirect, but add value of register X, e.g. JMP ($1234,X)
		case Mode::IndirectX: {
			targetAddress = getTarget(Mode::Indirect).value + indexX;
			break;
		}

		// Like Indirect, but add value of register Y, e.g. JMP ($1234,Y)
		case Mode::IndirectY: {
			targetAddress = getTarget(Mode::Indirect).value + indexY;
			break;
		}

		// Use the value embedded in the instruction as a signed offset
		// from the program counter (after the instruction has been decoded)
		case Mode::Relative: {
			const uint8_t value     = getTarget(Mode::Immediate).value,
						  lowerBits = value ^ 0b1000'0000U;
			// Two's complement: when the high bit is set the number is
			// negative, in which case flip the lower bits and add one.
			// If positive the original value is correct
			if (isNegative(value))
				targetAddress = pc - (~lowerBits + 1);
			else
				targetAddress = pc + value;

			break;
		}

		// Use the 4-bit value embedded in the instruction as an offset from the
		// beginning of memory
		case Mode::Zeropage: {
			targetAddress = read(pc++);
			break;
		}

		// Like Zeropage, but add value of register X and wrap within the page
		case Mode::ZeropageX: {
			targetAddress = wrapToByte(
				getTarget(Mode::Immediate).value + indexX);
			break;
		}

		// Like Zeropage, but add value of register Y and wrap within the page
		case Mode::ZeropageY: {
			targetAddress = wrapToByte(
				getTarget(Mode::Immediate).value + indexY);
			break;
		}
	}

	return {self, targetAddress};
}

constexpr void CPU::branch(uint16_t address) {
	pc = address;
}

constexpr auto CPU::read(size_t address) const -> uint8_t {
	return memory[address];
}

constexpr void CPU::write(size_t address, uint8_t value) {
	memory[address] = value;
}

constexpr void CPU::push(uint8_t value) {
	memory[stackTop + stack--] = value;
}

constexpr void CPU::push2(uint16_t value) {
	push(static_cast<uint8_t>(value >> 8U));
	push(static_cast<uint8_t>(value & u8Max));
}

constexpr auto CPU::pop() -> uint8_t {
	return memory[stackTop + ++stack];
}

constexpr auto CPU::pop2() -> uint16_t {
	return pop() + (pop() << 8U);
}

constexpr void CPU::popFlags() {
	auto value = pop();
	value |=  (1U << Unused);
	value &= ~(1U << Break);
	flags = value;
}

void CPU::calculateFlag(uint16_t value, FlagIndex flag) {
	bool result = false;

	switch(flag) {
		case Carry:
			result = (value == u8Max);  break;
		case Zero:
			result = (value == 0);      break;
		case Negative:
			result = isNegative(value); break;
		case Overflow:
			result = (value >  u8Max);  break;
		default:
			throw std::logic_error{"Can't calculate this flag from a value"};
	}

	flags.set(flag, result);
}

template<class T, class... Args>
void CPU::calculateFlag(uint16_t value, T flag, Args... flags) {
	calculateFlag(value, flag);
	calculateFlag(value, flags...);
}

void CPU::compare(size_t a, size_t b) {
	flags.set(Zero,     a == b);
	flags.set(Carry,    a >= b);
	flags.set(Negative, a <  b);
}

void CPU::addWithCarry(uint8_t input) {
	// TODO: implement decimal mode
	const uint16_t result = accumulator + input + (flags.test(Carry) ? 1 : 0);
	calculateFlag(result, Carry, Zero, Overflow, Negative);
	accumulator = wrapToByte(result);
}

void CPU::oADC(ValueStore address) {
	addWithCarry(address.read());
}

void CPU::oAND(ValueStore address) {
	const auto input = address.read();
	accumulator &= input;
	calculateFlag(accumulator, Zero, Negative);
}

void CPU::oASL(ValueStore address) {
	const auto input = address.read();
	flags.set(Carry, getBit(7, input));

	const auto result = input << 1U;
	calculateFlag(result, Zero, Negative);
	address.write(result);
}

void CPU::oBCC(ValueStore target) {
	if (!flags.test(Carry))
		branch(target.value);
}

void CPU::oBCS(ValueStore target) {
	if (flags.test(Carry))
		branch(target.value);
}

void CPU::oBEQ(ValueStore target) {
	if (flags.test(Zero))
		branch(target.value);
}

void CPU::oBIT(ValueStore address) {
	const auto input = address.read();
	flags.set(Zero, !static_cast<bool>(input & accumulator));
	flags.set(Overflow, getBit(6, input));
	flags.set(Negative, isNegative(input));
}

void CPU::oBMI(ValueStore target) {
	if (flags.test(Negative))
		branch(target.value);
}

void CPU::oBNE(ValueStore target) {
	if (!flags.test(Zero))
		branch(target.value);
}

void CPU::oBPL(ValueStore target) {
	if (!flags.test(Negative))
		branch(target.value);
}

void CPU::oBRK(ValueStore) {
	flags.set(InterruptOff, true);
	
	push2(pc);
	push(static_cast<uint8_t>(flags.to_ulong()));
}

void CPU::oBVC(ValueStore target) {
	if (!flags.test(Overflow))
		branch(target.value);
}

void CPU::oBVS(ValueStore target) {
	if (flags.test(Overflow))
		branch(target.value);
}

void CPU::oCLC(ValueStore) {
	flags.set(Carry, false);
}

void CPU::oCLD(ValueStore) {
	flags.set(Decimal, false);
}

void CPU::oCLI(ValueStore) {
	flags.set(InterruptOff, false);
}

void CPU::oCLV(ValueStore) {
	flags.set(Overflow, false);
}

void CPU::oCMP(ValueStore address) {
	const auto input = address.read();
	compare(accumulator, input);
}

void CPU::oCPX(ValueStore address) {
	const auto input = address.read();
	compare(indexX, input);
}

void CPU::oCPY(ValueStore address) {
	const auto input = address.read();
	compare(indexY, input);
}

void CPU::oDEC(ValueStore address) {
	const auto input = address.read();
	const auto result = input - 1;
	calculateFlag(result, Zero, Negative);
	address.write(result);
}

void CPU::oDEX(ValueStore) {
	const auto result = indexX - 1;
	calculateFlag(result, Zero, Negative);
	indexX = result;
}

void CPU::oDEY(ValueStore) {
	const auto result = indexY - 1;
	calculateFlag(result, Zero, Negative);
	indexY = result;
}

void CPU::oEOR(ValueStore address) {
	const auto input = address.read();
	const auto result = accumulator ^ input;
	calculateFlag(result, Zero, Negative);
	address.write(result);
}

void CPU::oINC(ValueStore address) {
	const auto input = address.read();
	const auto result = input + 1;
	calculateFlag(result, Zero, Negative);
	address.write(result);
}

void CPU::oINX(ValueStore) {
	const auto result = indexX - 1;
	calculateFlag(result, Zero, Negative);
	indexX = result;
}

void CPU::oINY(ValueStore) {
	const auto result = indexY - 1;
	calculateFlag(result, Zero, Negative);
	indexY = result;
}

void CPU::oJMP(ValueStore target) {
	pc = target.value;
}

void CPU::oJSR(ValueStore target) {
	push2(pc);
	pc = target.value;
}

void CPU::oLDA(ValueStore address) {
	const auto input = address.read();
	accumulator = input;
	calculateFlag(input, Zero, Negative);
}

void CPU::oLDX(ValueStore address) {
	const auto input = address.read();
	indexX = input;
	calculateFlag(input, Zero, Negative);
}

void CPU::oLDY(ValueStore address) {
	const auto input = address.read();
	indexY = input;
	calculateFlag(input, Zero, Negative);
}

void CPU::oLSR(ValueStore address) {
	const auto input = address.read();
	const auto result = input >> 1U;
	calculateFlag(result, Zero, Negative);
	flags.set(Carry, getBit(0, input));
	address.write(result);
}

void CPU::oNOP(ValueStore) {
}

void CPU::oORA(ValueStore address) {
	const auto input = address.read();
	const auto result = accumulator | input;
	calculateFlag(result, Zero, Negative);
	accumulator = result;
}

void CPU::oPHA(ValueStore) {
	push(accumulator);
}

void CPU::oPHP(ValueStore) {
	push(static_cast<uint8_t>(flags.to_ulong() | (1U << Break)));
}

void CPU::oPLA(ValueStore) {
	accumulator = pop();
	calculateFlag(accumulator, Zero, Negative);
}

void CPU::oPLP(ValueStore) {
	popFlags();
}

void CPU::oROL(ValueStore address) {
	const auto input = address.read();
	const auto result = setBit(0, input << 1U, flags.test(Carry));

	flags.set(Carry, getBit(7, input));
	calculateFlag(result, Zero, Negative);
	address.write(result);
}

void CPU::oROR(ValueStore address) {
	const auto input = address.read();
	const auto result = setBit(7, input >> 1U, flags.test(Carry));

	flags.set(Carry, getBit(0, input));
	calculateFlag(result, Zero, Negative);
	address.write(result);
}

void CPU::oRTI(ValueStore) {
	popFlags();
	pc    = pop2();
}

void CPU::oRTS(ValueStore) {
	pc = pop2();
}

void CPU::oSBC(ValueStore address) {
	addWithCarry(~address.read());
}

void CPU::oSEC(ValueStore) {
	flags.set(Carry, true);
}

void CPU::oSED(ValueStore) {
	flags.set(Decimal, true);
}

void CPU::oSEI(ValueStore) {
	flags.set(InterruptOff, true);
}

void CPU::oSTA(ValueStore address) {
	address.write(accumulator);
}

void CPU::oSTX(ValueStore address) {
	address.write(indexX);
}

void CPU::oSTY(ValueStore address) {
	address.write(indexY);
}

void CPU::oTAX(ValueStore) {
	indexX = accumulator;
	calculateFlag(indexX, Zero, Negative);
}

void CPU::oTAY(ValueStore) {
	indexY = accumulator;
	calculateFlag(indexY, Zero, Negative);
}

void CPU::oTSX(ValueStore) {
	indexX = stack;
	calculateFlag(indexX, Zero, Negative);
}

void CPU::oTXA(ValueStore) {
	accumulator = indexX;
	calculateFlag(accumulator, Zero, Negative);
}

void CPU::oTXS(ValueStore) {
	stack = indexX;
}

void CPU::oTYA(ValueStore) {
	accumulator = indexY;
	calculateFlag(accumulator, Zero, Negative);
}

constexpr auto CPU::getInstructions() -> Instructions {
	using C = CPU;
	using M = AddressMode;

	return {{
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
	}};
}
