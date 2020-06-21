#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <span>

class CPU;

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

struct Flags {
	enum class Index : uint8_t {
		Carry,
		Zero,
		InterruptOff,
		Decimal,
		Break,
		Unused,
		Overflow,
		Negative,
	};

	constexpr Flags() = default;
	constexpr Flags(uint8_t value);

	constexpr static auto bitmask(Index i) noexcept -> uint8_t;
	[[nodiscard]]
	constexpr auto get() const noexcept -> uint8_t;
	[[nodiscard]]
	constexpr auto test(Index i) const noexcept -> bool;
	constexpr void set(Index i, bool set) noexcept;
	constexpr void reset() noexcept;
	constexpr auto operator ==(const Flags& rhs) const noexcept -> bool;

private:
	constexpr static auto getDefault() -> uint8_t {
		return static_cast<uint8_t>(
		bitmask(Index::Unused) | bitmask(Index::InterruptOff));
	}

	uint8_t value = getDefault();
};

class ValueStore {
public:
	enum class Type {
		Implicit,
		Accumulator,
		Memory,
		Value,
	};

	constexpr ValueStore(CPU& cpu, uint16_t value, Type type = Type::Memory);
	constexpr explicit ValueStore(CPU& cpu);

	[[nodiscard]]
	constexpr auto read() const noexcept -> uint16_t;
	constexpr void write(uint8_t) noexcept;
	[[nodiscard]]
	constexpr auto get() const noexcept -> uint16_t;

private:
	const uint16_t value;
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
	// TODO: loadProgram should be constexpr, but GCC says "inline function
	// [...] used but never defined" if it is declared constexpr
	void loadProgram(std::span<const uint8_t> program, uint16_t offset);
	void loadProgram(std::span<const uint8_t> program);
	auto step() noexcept -> bool;

	// Registers
	uint8_t  accumulator{0};
	uint8_t  indexX{0};
	uint8_t  indexY{0};
	uint8_t  stack{initialStackPointer};
	uint16_t pc{initialProgramCounter};

	Flags flags;

	constexpr static auto memorySize            = 65536U;
	using Memory = std::array<uint8_t, memorySize>;
	Memory memory{};

	constexpr void push(uint16_t) = delete;
	constexpr void push2(uint8_t) = delete;

private:
	constexpr static auto stackTop              = 0x100;
	constexpr static auto initialStackPointer   = 0xfd;
	constexpr static auto initialProgramCounter = 0x600;

	bool indirectJumpBug = true;

	// Instruction lookup table
	using Instructions = std::array<Instruction, 256>;
	constexpr static auto getInstructions() -> Instructions;

	// Instruction helpers
	constexpr auto getTarget(AddressMode mode) noexcept -> ValueStore;
	[[nodiscard]]
	constexpr auto read(size_t address) const noexcept -> uint8_t;
	[[nodiscard]]
	constexpr auto read2(size_t address, bool wrapToPage = false) const
		noexcept -> uint16_t;
	constexpr void write(size_t address, uint8_t value) noexcept;
	constexpr void push(uint8_t) noexcept;
	constexpr void push2(uint16_t) noexcept;
	constexpr auto pop() noexcept -> uint8_t;
	constexpr auto pop2() noexcept -> uint16_t;
	constexpr void popFlags() noexcept;
	constexpr void branch(uint16_t) noexcept;

	template<class T, class... Args>
	constexpr void calculateFlag(uint8_t value, T flag, Args... flags);
	constexpr void calculateFlag(uint8_t value, Flags::Index flag) noexcept;
	constexpr void compare(size_t a, size_t b) noexcept;
	constexpr void addWithCarry(uint8_t value) noexcept;

	// Instructions
	constexpr void oADC(ValueStore) noexcept;
	constexpr void oAND(ValueStore) noexcept;
	constexpr void oASL(ValueStore) noexcept;
	constexpr void oBCC(ValueStore) noexcept;
	constexpr void oBCS(ValueStore) noexcept;
	constexpr void oBEQ(ValueStore) noexcept;
	constexpr void oBIT(ValueStore) noexcept;
	constexpr void oBMI(ValueStore) noexcept;
	constexpr void oBNE(ValueStore) noexcept;
	constexpr void oBPL(ValueStore) noexcept;
	constexpr void oBRK(ValueStore) noexcept;
	constexpr void oBVC(ValueStore) noexcept;
	constexpr void oBVS(ValueStore) noexcept;
	constexpr void oCLC(ValueStore) noexcept;
	constexpr void oCLD(ValueStore) noexcept;
	constexpr void oCLI(ValueStore) noexcept;
	constexpr void oCLV(ValueStore) noexcept;
	constexpr void oCMP(ValueStore) noexcept;
	constexpr void oCPX(ValueStore) noexcept;
	constexpr void oCPY(ValueStore) noexcept;
	constexpr void oDEC(ValueStore) noexcept;
	constexpr void oDEX(ValueStore) noexcept;
	constexpr void oDEY(ValueStore) noexcept;
	constexpr void oEOR(ValueStore) noexcept;
	constexpr void oINC(ValueStore) noexcept;
	constexpr void oINX(ValueStore) noexcept;
	constexpr void oINY(ValueStore) noexcept;
	constexpr void oJMP(ValueStore) noexcept;
	constexpr void oJSR(ValueStore) noexcept;
	constexpr void oLDA(ValueStore) noexcept;
	constexpr void oLDX(ValueStore) noexcept;
	constexpr void oLDY(ValueStore) noexcept;
	constexpr void oLSR(ValueStore) noexcept;
	constexpr void oNOP(ValueStore) noexcept;
	constexpr void oORA(ValueStore) noexcept;
	constexpr void oPHA(ValueStore) noexcept;
	constexpr void oPHP(ValueStore) noexcept;
	constexpr void oPLA(ValueStore) noexcept;
	constexpr void oPLP(ValueStore) noexcept;
	constexpr void oROL(ValueStore) noexcept;
	constexpr void oROR(ValueStore) noexcept;
	constexpr void oRTI(ValueStore) noexcept;
	constexpr void oRTS(ValueStore) noexcept;
	constexpr void oSBC(ValueStore) noexcept;
	constexpr void oSEC(ValueStore) noexcept;
	constexpr void oSED(ValueStore) noexcept;
	constexpr void oSEI(ValueStore) noexcept;
	constexpr void oSTA(ValueStore) noexcept;
	constexpr void oSTX(ValueStore) noexcept;
	constexpr void oSTY(ValueStore) noexcept;
	constexpr void oTAX(ValueStore) noexcept;
	constexpr void oTAY(ValueStore) noexcept;
	constexpr void oTSX(ValueStore) noexcept;
	constexpr void oTXA(ValueStore) noexcept;
	constexpr void oTXS(ValueStore) noexcept;
	constexpr void oTYA(ValueStore) noexcept;

	friend class ValueStore;
};

constexpr Flags::Flags(uint8_t value)
: value{value}
{
}

constexpr auto Flags::bitmask(Index i) noexcept -> uint8_t {
	return 1U << static_cast<uint8_t>(i);
}

[[nodiscard]]
constexpr auto Flags::get() const noexcept -> uint8_t {
	return value;
}

[[nodiscard]]
constexpr auto Flags::test(Index i) const noexcept -> bool {
	return (value & bitmask(i)) != 0;
}

constexpr void Flags::set(Index i, bool set) noexcept {
	const auto mask = bitmask(i);
	if (set)
		value |= mask;
	else
		value &= static_cast<uint8_t>(~mask);
}

constexpr void Flags::reset() noexcept {
	value = getDefault();
}

constexpr auto Flags::operator ==(const Flags& rhs) const noexcept -> bool {
	return value == rhs.value;
}

constexpr ValueStore::ValueStore(
	CPU& cpu,
	uint16_t value,
	Type type
) : value{value},
    type{type},
    cpu{cpu}
{
}

constexpr ValueStore::ValueStore(CPU& cpu)
: value{0},
  type{Type::Accumulator},
  cpu{cpu}
{
}

constexpr auto ValueStore::get() const noexcept -> uint16_t {
	return value;
}
