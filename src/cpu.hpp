#pragma once

#include <cstddef>
#include <array>
#include <bitset>
#include <optional>
#include <functional>

constexpr auto MEMORY_SIZE_BYTES = 65536;

class CPU;

enum Flag {
	Carry,
	Zero,
	InterruptOff,
	Decimal,
	Break,
	Unused,
	Overflow,
	Negative,
};

enum class AddressMode {
	Implicit,
	Accumulator, A    = Accumulator,
	Immediate,   Imm  = Immediate,
	Absolute,    Abs  = Absolute,
	AbsoluteX,   AbsX = AbsoluteX,
	AbsoluteY,   AbsY = AbsoluteY,
	Indirect,    Ind  = Indirect,
	IndirectX,   IndX = IndirectX,
	IndirectY,   IndY = IndirectY,
	Relative,    Rel  = Relative,
	Zeropage,    Zpg  = Zeropage,
	ZeropageX,   ZpgX = ZeropageX,
	ZeropageY,   ZpgY = ZeropageY,
};

class ValueStore {
public:
	enum class Type {
		Accumulator,
		Memory,
	};

	constexpr ValueStore(CPU& cpu, uint16_t address, Type type = Type::Memory)
	: address{address},
	  type{type},
	  cpu{cpu}
	{
	};

	constexpr ValueStore(CPU& cpu)
	: ValueStore{cpu, 0}
	{
	};

	constexpr auto read() -> uint8_t;
	constexpr void write(uint8_t);

	const uint16_t address;

private:
	const Type type;
	CPU& cpu;
};

struct Instruction {
	using Function = void(CPU::*)(ValueStore);
	Function    function    = nullptr;
	AddressMode addressMode = AddressMode::Implicit;
};

class CPU {
public:
	constexpr void reset();
	void step();

	// Registers
	uint8_t  accumulator{0};
	uint8_t  indexX{0};
	uint8_t  indexY{0};
	uint8_t  stack{initialStackPointer};
	uint16_t pc{initialProgramCounter};
	std::bitset<8> flags{Unused};

	using Memory = std::array<uint8_t, MEMORY_SIZE_BYTES>;
	Memory memory{};

private:
	constexpr static auto stackTop              = 0x100;
	constexpr static auto initialStackPointer   = 0xff;
	constexpr static auto initialProgramCounter = 0x600;

	bool indirectJumpBug = true;

	// Instruction lookup table
	using Instructions = std::array<Instruction, 256>;
	constexpr static auto getInstructions() -> Instructions;

	// Instruction helpers
	constexpr auto getTarget(AddressMode mode) -> ValueStore;
	constexpr auto read(size_t address) -> uint8_t;
	constexpr void write(size_t address, uint8_t value);
	constexpr void push(uint8_t);
	constexpr void push(uint16_t) = delete;
	constexpr void push2(uint16_t);
	constexpr void push2(uint8_t) = delete;
	constexpr auto pop() -> uint8_t;
	constexpr auto pop2() -> uint16_t;
	constexpr void branch(uint16_t);

	void setZeroNegative(uint8_t);
	void setOverflowCarry(size_t);
	void compare(size_t a, size_t b);
	void addWithCarry(uint8_t value);

	// Instructions
	void oADC(ValueStore);
	void oAND(ValueStore);
	void oASL(ValueStore);
	void oBCC(ValueStore);
	void oBCS(ValueStore);
	void oBEQ(ValueStore);
	void oBIT(ValueStore);
	void oBMI(ValueStore);
	void oBNE(ValueStore);
	void oBPL(ValueStore);
	void oBRK(ValueStore);
	void oBVC(ValueStore);
	void oBVS(ValueStore);
	void oCLC(ValueStore);
	void oCLD(ValueStore);
	void oCLI(ValueStore);
	void oCLV(ValueStore);
	void oCMP(ValueStore);
	void oCPX(ValueStore);
	void oCPY(ValueStore);
	void oDEC(ValueStore);
	void oDEX(ValueStore);
	void oDEY(ValueStore);
	void oEOR(ValueStore);
	void oINC(ValueStore);
	void oINX(ValueStore);
	void oINY(ValueStore);
	void oJMP(ValueStore);
	void oJSR(ValueStore);
	void oLDA(ValueStore);
	void oLDX(ValueStore);
	void oLDY(ValueStore);
	void oLSR(ValueStore);
	void oNOP(ValueStore);
	void oORA(ValueStore);
	void oPHA(ValueStore);
	void oPHP(ValueStore);
	void oPLA(ValueStore);
	void oPLP(ValueStore);
	void oROL(ValueStore);
	void oROR(ValueStore);
	void oRTI(ValueStore);
	void oRTS(ValueStore);
	void oSBC(ValueStore);
	void oSEC(ValueStore);
	void oSED(ValueStore);
	void oSEI(ValueStore);
	void oSTA(ValueStore);
	void oSTX(ValueStore);
	void oSTY(ValueStore);
	void oTAX(ValueStore);
	void oTAY(ValueStore);
	void oTSX(ValueStore);
	void oTXA(ValueStore);
	void oTXS(ValueStore);
	void oTYA(ValueStore);

	friend class ValueStore;
};
