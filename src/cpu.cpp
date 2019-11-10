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

constexpr void ValueStore::write(uint8_t value) {
	switch(type) {
		case Type::Accumulator: {
			cpu.accumulator = 1;
			break;
		}
		case Type::Memory: {
			cpu.write(address, value);
		}
	}
}

constexpr auto ValueStore::read() -> uint8_t {
	switch(type) {
		case Type::Accumulator:
			return cpu.accumulator;
		case Type::Memory:
			return cpu.read(address);
	}

	return 0;
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
			return {self};

		// Use value of accumulator, e.g. LSL A
		case Mode::Immediate: {
			targetAddress = pc++;
			break;
		}

		// Use 16-bit value embedded in instruction, e.g. JMP $1234
		case Mode::Absolute: {
			const uint8_t low  = read(pc++),
						  high = read(pc++);
			targetAddress = (high << 8) + low;
			break;
		}

		// Like Absolute, but add value of register X, e.g. JMP $1234,X
		case Mode::AbsoluteX: {
			targetAddress = getTarget(Mode::Absolute).address + indexX;
			break;
		}

		// Like Absolute, but add value of register Y, e.g. JMP $1234,Y
		case Mode::AbsoluteY: {
			targetAddress = getTarget(Mode::Absolute).address + indexY;
			break;
		}

		// Use the value at the address embedded in the instruction
		// e.g. JMP ($1234)
		case Mode::Indirect: {
			// indirectJumpBug: a hardware bug results in the increment
			// actually flipping the lower byte from 0xff to 0x00
			const uint8_t lowTarget  = read(getTarget(Mode::Absolute).address),
						  highTarget = indirectJumpBug && (lowTarget & 0xff)
				? (lowTarget & 0xff00)
				: lowTarget + 1;

			const auto low  = read(lowTarget),
					   high = read(highTarget);
			targetAddress = (high << 8) + low;
			break;
		}

		// Like Indirect, but add value of register X, e.g. JMP ($1234,X)
		case Mode::IndirectX: {
			targetAddress = getTarget(Mode::Indirect).address + indexX;
			break;
		}

		// Like Indirect, but add value of register Y, e.g. JMP ($1234,Y)
		case Mode::IndirectY: {
			targetAddress = getTarget(Mode::Indirect).address + indexY;
			break;
		}

		// Use the value embedded in the instruction as a signed offset
		// from the program counter (after the instruction has been decoded)
		case Mode::Relative: {
			const uint8_t value     = getTarget(Mode::Immediate).address,
						  lowerBits = value ^ 0b1000'0000;
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
				getTarget(Mode::Immediate).address + indexX);
			break;
		}

		// Like Zeropage, but add value of register Y and wrap within the page
		case Mode::ZeropageY: {
			targetAddress = wrapToByte(
				getTarget(Mode::Immediate).address + indexY);
			break;
		}
	}

	return {self, targetAddress};
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

void CPU::oADC(ValueStore address) {
	addWithCarry(address.read());
}

void CPU::oAND(ValueStore address) {
	const auto input = address.read();
	accumulator &= input;
	setZeroNegative(accumulator);
}

void CPU::oASL(ValueStore address) {
	const auto input = address.read();
	flags[Carry] = getBit(7, input);

	const auto result = input << 1;
	setZeroNegative(result);
	address.write(result);
}

void CPU::oBCC(ValueStore target) {
	if (!flags[Carry])
		branch(target.read());
}

void CPU::oBCS(ValueStore target) {
	if (flags[Carry])
		branch(target.read());
}

void CPU::oBEQ(ValueStore target) {
	if (flags[Zero])
		branch(target.read());
}

void CPU::oBIT(ValueStore address) {
	const auto input = address.read();
	setZeroNegative(input);
}

void CPU::oBMI(ValueStore target) {
	if (flags[Negative])
		branch(target.read());
}

void CPU::oBNE(ValueStore target) {
	if (!flags[Zero])
		branch(target.read());
}

void CPU::oBPL(ValueStore target) {
	if (!flags[Negative])
		branch(target.read());
}

void CPU::oBRK(ValueStore) {
	flags[InterruptOff] = 1;
	
	push2(pc);
	push(static_cast<uint8_t>(flags.to_ulong()));
}

void CPU::oBVC(ValueStore address) {
	if (!flags[Overflow])
		branch(address.read());
}

void CPU::oBVS(ValueStore address) {
	if (flags[Overflow])
		branch(address.read());
}

void CPU::oCLC(ValueStore) {
	flags[Carry] = 0;
}

void CPU::oCLD(ValueStore) {
	flags[Decimal] = 0;
}

void CPU::oCLI(ValueStore) {
	flags[InterruptOff] = 0;
}

void CPU::oCLV(ValueStore) {
	flags[Overflow] = 0;
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
	setZeroNegative(result);
	address.write(result);
}

void CPU::oDEX(ValueStore) {
	const auto result = indexX - 1;
	setZeroNegative(result);
	indexX = result;
}

void CPU::oDEY(ValueStore) {
	const auto result = indexY - 1;
	setZeroNegative(result);
	indexY = result;
}

void CPU::oEOR(ValueStore address) {
	const auto input = address.read();
	const auto result = accumulator ^ input;
	setZeroNegative(result);
	address.write(result);
}

void CPU::oINC(ValueStore address) {
	const auto input = address.read();
	const auto result = input + 1;
	setZeroNegative(result);
	address.write(result);
}

void CPU::oINX(ValueStore) {
	const auto result = indexX - 1;
	setZeroNegative(result);
	indexX = result;
}

void CPU::oINY(ValueStore) {
	const auto result = indexY - 1;
	setZeroNegative(result);
	indexY = result;
}

void CPU::oJMP(ValueStore address) {
	pc = address.read();
}

void CPU::oJSR(ValueStore address) {
	push2(pc);
	pc = address.read();
}

void CPU::oLDA(ValueStore address) {
	const auto input = address.read();
	accumulator = input;
	setZeroNegative(input);
}

void CPU::oLDX(ValueStore address) {
	const auto input = address.read();
	indexX = input;
	setZeroNegative(input);
}

void CPU::oLDY(ValueStore address) {
	const auto input = address.read();
	indexY = input;
	setZeroNegative(input);
}

void CPU::oLSR(ValueStore address) {
	const auto input = address.read();
	const auto result = input >> 1;
	setZeroNegative(result);
	flags[Carry] = getBit(0, input);
	address.write(result);
}

void CPU::oNOP(ValueStore) {
}

void CPU::oORA(ValueStore address) {
	const auto input = address.read();
	const auto result = accumulator | input;
	setZeroNegative(result);
	accumulator = result;
}

void CPU::oPHA(ValueStore) {
	push(accumulator);
}

void CPU::oPHP(ValueStore) {
	push(static_cast<uint8_t>(flags.to_ulong()));
}

void CPU::oPLA(ValueStore) {
	accumulator = pop();
}

void CPU::oPLP(ValueStore) {
	flags = pop();
}

void CPU::oROL(ValueStore address) {
	const auto input = address.read();
	const auto result = setBit(0, input << 1, flags[Carry]);

	flags[Carry] = getBit(7, input);
	setZeroNegative(result);
	address.write(result);
}

void CPU::oROR(ValueStore address) {
	const auto input = address.read();
	const auto result = setBit(7, input >> 1, flags[Carry]);

	flags[Carry] = getBit(0, input);
	setZeroNegative(result);
	address.write(result);
}

void CPU::oRTI(ValueStore) {
	flags = pop();
	pc    = pop2();
}

void CPU::oRTS(ValueStore) {
	pc = pop2();
}

void CPU::oSBC(ValueStore address) {
	addWithCarry(~address.read());
}

void CPU::oSEC(ValueStore) {
	flags[Carry] = 1;
}

void CPU::oSED(ValueStore) {
	flags[Decimal] = 1;
}

void CPU::oSEI(ValueStore) {
	flags[InterruptOff] = 1;
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
	setZeroNegative(indexX);
}

void CPU::oTAY(ValueStore) {
	indexY = accumulator;
	setZeroNegative(indexY);
}

void CPU::oTSX(ValueStore) {
	indexX = stack;
	setZeroNegative(indexX);
}

void CPU::oTXA(ValueStore) {
	accumulator = indexX;
	setZeroNegative(accumulator);
}

void CPU::oTXS(ValueStore) {
	stack = indexX;
}

void CPU::oTYA(ValueStore) {
	accumulator = indexY;
	setZeroNegative(accumulator);
}
