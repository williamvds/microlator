#include <functional>

#include "cpu.hpp"

constexpr auto stackTop   = 0x100;

constexpr uint8_t setBit(uint8_t index, uint8_t value, bool set) {
	return value | (set << index);
}

constexpr bool getBit(uint8_t index, uint8_t value) {
	return (value >> index) & 1;
}

constexpr bool isNegative(uint8_t value) {
	// Two's complement: top bit means negative
	return getBit(7, value);
}

constexpr uint8_t wrapToByte(size_t value) {
	// Subtract one if wrapping to remove carry
	if (value <= 0xff)
		return value;
	else
		return (value % 0xff) - 1;
}

constexpr void CPU::reset() {
	memory = Memory{};
	pc    = 0;
	stack = 0xff;
	flags = 0;
}

void CPU::execute() {
	const auto opcode = read(pc++);

	const auto& instruction = instructions[opcode];
	if (!instruction.function)
		return;

	const auto target = getTarget(instruction.addressMode);
	std::invoke(instruction.function, this, target);
}

// Get the target address depending on the addressing mode
constexpr uint16_t CPU::getTarget(AddressMode mode) {
	using Mode = AddressMode;

	switch (mode) {
		// Instruction makes target implicit, e.g. CLC
		case Mode::Implicit:
			return 0;

		// Use value of accumulator, e.g. LSL A
		case Mode::Accumulator:
			return accumulator;

		// Use value of accumulator, e.g. LSL A
		case Mode::Immediate:
			return pc++;

		// Use 16-bit value embedded in instruction, e.g. JMP $1234
		case Mode::Absolute: {
			const uint8_t low  = read(pc++),
						  high = read(pc++);
			return (high << 8) + low;
		}

		// Like Absolute, but add value of register X, e.g. JMP $1234,X
		case Mode::AbsoluteX:
			return getTarget(Mode::Absolute) + indexX;

		// Like Absolute, but add value of register Y, e.g. JMP $1234,Y
		case Mode::AbsoluteY:
			return getTarget(Mode::Absolute) + indexY;

		// Use the value at the address embedded in the instruction
		// e.g. JMP ($1234)
		case Mode::Indirect: {
			// indirectJumpBug: a hardware bug results in the increment
			// actually flipping the lower byte from 0xff to 0x00
			const uint8_t lowTarget  = read(getTarget(Mode::Absolute)),
						  highTarget = indirectJumpBug && (lowTarget & 0xff)
				? (lowTarget & 0xff00)
				: lowTarget + 1;

			const auto low  = read(lowTarget),
					   high = read(highTarget);
			return (high << 8) + low;
		}

		// Like Indirect, but add value of register X, e.g. JMP ($1234,X)
		case Mode::IndirectX:
			return getTarget(Mode::Indirect) + indexX;

		// Like Indirect, but add value of register Y, e.g. JMP ($1234,Y)
		case Mode::IndirectY:
			return getTarget(Mode::Indirect) + indexY;

		// Use the value embedded in the instruction as a signed offset
		// from the program counter (after the instruction has been decoded)
		case Mode::Relative: {
			const uint8_t value     = getTarget(Mode::Immediate),
						  lowerBits = value ^ 0b1000'0000;
			// Two's complement: when the high bit is set the number is
			// negative, in which case flip the lower bits and add one.
			// If positive the original value is correct
			if (isNegative(value))
				return pc - (~lowerBits + 1);
			else
				return pc + value;
		}

		// Use the 4-bit value embedded in the instruction as an offset from the
		// beginning of memory
		case Mode::Zeropage:
			 return read(pc++);

		// Like Zeropage, but add value of register X and wrap within the page
		case Mode::ZeropageX:
			 return wrapToByte(getTarget(Mode::Immediate) + indexX);

		// Like Zeropage, but add value of register Y and wrap within the page
		case Mode::ZeropageY:
			 return wrapToByte(getTarget(Mode::Immediate) + indexY);
	}

	return 0;
}

constexpr void CPU::branch(uint16_t address) {
	pc = address;
}

constexpr uint8_t CPU::read(size_t address) {
	return memory[address];
}

constexpr void CPU::write(size_t address, uint8_t value) {
	memory[address] = value;
}

constexpr void CPU::push(uint8_t value) {
	memory[stackTop + stack--] = value;
}

constexpr void CPU::push2(uint16_t value) {
	push(static_cast<uint8_t>(value >> 8));
	push(static_cast<uint8_t>(value & 0xff));
}

constexpr auto CPU::pop() -> uint8_t {
	return memory[stackTop + ++stack];
}

constexpr auto CPU::pop2() -> uint16_t {
	return (pop() << 8) + pop();
}

void CPU::setZeroNegative(uint8_t value) {
	flags[Zero]     = value == 0;
	flags[Negative] = isNegative(value);
}

void CPU::setOverflowCarry(size_t value) {
	flags[Carry]    = value == 0xff;
	flags[Overflow] = value >  0xff;
}

void CPU::compare(size_t a, size_t b) {
	flags[Zero]     = a == b;
	flags[Carry]    = a >  b;
	flags[Negative] = a <  b;
}

void CPU::addWithCarry(uint8_t input) {
	// TODO: implement decimal mode
	const uint16_t result = accumulator + input + (flags[Carry] ? 1 : 0);
	setOverflowCarry(result);
	setZeroNegative(result);
	accumulator = wrapToByte(result);
}

void CPU::oADC(uint16_t address) {
	addWithCarry(read(address));
}

void CPU::oAND(uint16_t address) {
	const auto input = read(address);
	accumulator &= input;
	setZeroNegative(accumulator);
}

void CPU::oASL(uint16_t address) {
	const auto input = read(address);
	flags[Carry] = getBit(7, input);

	const auto result = input << 1;
	setZeroNegative(result);
	write(address, result);
}

void CPU::oBCC(uint16_t target) {
	if (!flags[Carry])
		branch(target);
}

void CPU::oBCS(uint16_t target) {
	if (flags[Carry])
		branch(target);
}

void CPU::oBEQ(uint16_t target) {
	if (flags[Zero])
		branch(target);
}

void CPU::oBIT(uint16_t address) {
	const auto input = read(address);
	setZeroNegative(input);
}

void CPU::oBMI(uint16_t target) {
	if (flags[Negative])
		branch(target);
}

void CPU::oBNE(uint16_t target) {
	if (!flags[Zero])
		branch(target);
}

void CPU::oBPL(uint16_t target) {
	if (!flags[Negative])
		branch(target);
}

void CPU::oBRK(uint16_t) {
	flags[InterruptOff] = 1;
	
	push2(pc);
	push(static_cast<uint8_t>(flags.to_ulong()));
}

void CPU::oBVC(uint16_t address) {
	if (!flags[Overflow])
		branch(address);
}

void CPU::oBVS(uint16_t address) {
	if (flags[Overflow])
		branch(address);
}

void CPU::oCLC(uint16_t) {
	flags[Carry] = 0;
}

void CPU::oCLD(uint16_t) {
	flags[Decimal] = 0;
}

void CPU::oCLI(uint16_t) {
	flags[InterruptOff] = 0;
}

void CPU::oCLV(uint16_t) {
	flags[Overflow] = 0;
}

void CPU::oCMP(uint16_t address) {
	const auto input = read(address);
	compare(accumulator, input);
}

void CPU::oCPX(uint16_t address) {
	const auto input = read(address);
	compare(indexX, input);
}

void CPU::oCPY(uint16_t address) {
	const auto input = read(address);
	compare(indexY, input);
}

void CPU::oDEC(uint16_t address) {
	const auto input = read(address);
	const auto result = input - 1;
	setZeroNegative(result);
	write(address, result);
}

void CPU::oDEX(uint16_t) {
	const auto result = indexX - 1;
	setZeroNegative(result);
	indexX = result;
}

void CPU::oDEY(uint16_t) {
	const auto result = indexY - 1;
	setZeroNegative(result);
	indexY = result;
}

void CPU::oEOR(uint16_t address) {
	const auto input = read(address);
	const auto result = accumulator ^ input;
	setZeroNegative(result);
	write(address, result);
}

void CPU::oINC(uint16_t address) {
	const auto input = read(address);
	const auto result = input + 1;
	setZeroNegative(result);
	write(address, result);
}

void CPU::oINX(uint16_t) {
	const auto result = indexX - 1;
	setZeroNegative(result);
	indexX = result;
}

void CPU::oINY(uint16_t) {
	const auto result = indexY - 1;
	setZeroNegative(result);
	indexY = result;
}

void CPU::oJMP(uint16_t address) {
	pc = address;
}

void CPU::oJSR(uint16_t address) {
	push2(pc);
	pc = address;
}

void CPU::oLDA(uint16_t address) {
	const auto input = read(address);
	accumulator = input;
	setZeroNegative(input);
}

void CPU::oLDX(uint16_t address) {
	const auto input = read(address);
	indexX = input;
	setZeroNegative(input);
}

void CPU::oLDY(uint16_t address) {
	const auto input = read(address);
	indexY = input;
	setZeroNegative(input);
}

void CPU::oLSR(uint16_t address) {
	const auto input = read(address);
	const auto result = input >> 1;
	setZeroNegative(result);
	flags[Carry] = getBit(0, input);
	write(address, result);
}

void CPU::oNOP(uint16_t) {
}

void CPU::oORA(uint16_t address) {
	const auto input = read(address);
	const auto result = accumulator | input;
	setZeroNegative(result);
	accumulator = result;
}

void CPU::oPHA(uint16_t) {
	push(accumulator);
}

void CPU::oPHP(uint16_t) {
	push(static_cast<uint8_t>(flags.to_ulong()));
}

void CPU::oPLA(uint16_t) {
	accumulator = pop();
}

void CPU::oPLP(uint16_t) {
	flags = pop();
}

void CPU::oROL(uint16_t address) {
	const auto input = read(address);
	const auto result = setBit(0, input << 1, flags[Carry]);

	flags[Carry] = getBit(7, input);
	setZeroNegative(result);
	write(address, result);
}

void CPU::oROR(uint16_t address) {
	const auto input = read(address);
	const auto result = setBit(7, input >> 1, flags[Carry]);

	flags[Carry] = getBit(0, input);
	setZeroNegative(result);
	write(address, result);
}

void CPU::oRTI(uint16_t) {
	flags = pop();
	pc    = pop2();
}

void CPU::oRTS(uint16_t) {
	pc = pop2();
}

void CPU::oSBC(uint16_t address) {
	addWithCarry(~read(address));
}

void CPU::oSEC(uint16_t) {
	flags[Carry] = 1;
}

void CPU::oSED(uint16_t) {
	flags[Decimal] = 1;
}

void CPU::oSEI(uint16_t) {
	flags[InterruptOff] = 1;
}

void CPU::oSTA(uint16_t address) {
	write(address, accumulator);
}

void CPU::oSTX(uint16_t address) {
	write(address, indexX);
}

void CPU::oSTY(uint16_t address) {
	write(address, indexY);
}

void CPU::oTAX(uint16_t) {
	indexX = accumulator;
	setZeroNegative(indexX);
}

void CPU::oTAY(uint16_t) {
	indexY = accumulator;
	setZeroNegative(indexY);
}

void CPU::oTSX(uint16_t) {
	indexX = stack;
	setZeroNegative(indexX);
}

void CPU::oTXA(uint16_t) {
	accumulator = indexX;
	setZeroNegative(accumulator);
}

void CPU::oTXS(uint16_t) {
	stack = indexX;
}

void CPU::oTYA(uint16_t) {
	accumulator = indexY;
	setZeroNegative(accumulator);
}
