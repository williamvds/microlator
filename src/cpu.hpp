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

struct Instruction {
	using Function = void(CPU::*)(uint16_t);
	Function    function    = nullptr;
	AddressMode addressMode = AddressMode::Implicit;
};

class CPU {
public:
	constexpr void reset();
	void execute();

	// Registers
	uint8_t  accumulator{0};
	uint8_t  indexX{0};
	uint8_t  indexY{0};
	uint8_t  stack{0};
	uint16_t pc{0xff};
	std::bitset<8> flags{0};

	using Memory = std::array<uint8_t, MEMORY_SIZE_BYTES>;
	Memory memory{};

private:
	bool indirectJumpBug = true;

	// Instruction helpers
	constexpr auto getTarget(AddressMode mode) -> uint16_t;
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
	void oADC(uint16_t);
	void oAND(uint16_t);
	void oASL(uint16_t);
	void oBCC(uint16_t);
	void oBCS(uint16_t);
	void oBEQ(uint16_t);
	void oBIT(uint16_t);
	void oBMI(uint16_t);
	void oBNE(uint16_t);
	void oBPL(uint16_t);
	void oBRK(uint16_t);
	void oBVC(uint16_t);
	void oBVS(uint16_t);
	void oCLC(uint16_t);
	void oCLD(uint16_t);
	void oCLI(uint16_t);
	void oCLV(uint16_t);
	void oCMP(uint16_t);
	void oCPX(uint16_t);
	void oCPY(uint16_t);
	void oDEC(uint16_t);
	void oDEX(uint16_t);
	void oDEY(uint16_t);
	void oEOR(uint16_t);
	void oINC(uint16_t);
	void oINX(uint16_t);
	void oINY(uint16_t);
	void oJMP(uint16_t);
	void oJSR(uint16_t);
	void oLDA(uint16_t);
	void oLDX(uint16_t);
	void oLDY(uint16_t);
	void oLSR(uint16_t);
	void oNOP(uint16_t);
	void oORA(uint16_t);
	void oPHA(uint16_t);
	void oPHP(uint16_t);
	void oPLA(uint16_t);
	void oPLP(uint16_t);
	void oROL(uint16_t);
	void oROR(uint16_t);
	void oRTI(uint16_t);
	void oRTS(uint16_t);
	void oSBC(uint16_t);
	void oSEC(uint16_t);
	void oSED(uint16_t);
	void oSEI(uint16_t);
	void oSTA(uint16_t);
	void oSTX(uint16_t);
	void oSTY(uint16_t);
	void oTAX(uint16_t);
	void oTAY(uint16_t);
	void oTSX(uint16_t);
	void oTXA(uint16_t);
	void oTXS(uint16_t);
	void oTYA(uint16_t);

	// Instruction lookup table
	using C = CPU;
	using M = AddressMode;
	constexpr static std::array<Instruction, 256> instructions = {{
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
};
