#pragma once

#include <array>
#include <bitset>
#include <cstddef>
#include <functional>
#include <optional>
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
	constexpr Flags(uint8_t value)
	: value{value}
	{
	};

	constexpr static auto bitmask(Index i) noexcept -> uint8_t {
		return 1U << static_cast<uint8_t>(i);
	}

	[[nodiscard]]
	constexpr auto get() const noexcept -> uint8_t {
		return value;
	}

	[[nodiscard]]
	constexpr auto test(Index i) const noexcept -> bool {
		return (value & bitmask(i)) != 0;
	}

	constexpr void set(Index i, bool set) noexcept {
		const auto mask = bitmask(i);
		if (set)
			value |= mask;
		else
			value &= static_cast<uint8_t>(~mask);
	}

	constexpr void reset() noexcept {
		value = getDefault();
	}

	constexpr auto operator ==(const Flags& rhs) const noexcept -> bool {
		return value == rhs.value;
	}

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

	constexpr ValueStore(CPU& cpu, uint16_t value, Type type = Type::Memory)
	: value{value},
	  type{type},
	  cpu{cpu}
	{
	};

	constexpr explicit ValueStore(CPU& cpu)
	: value{0},
	  type{Type::Accumulator},
	  cpu{cpu}
	{
	};

	[[nodiscard]]
	constexpr auto read() const noexcept -> uint16_t;
	constexpr void write(uint8_t) noexcept;

	const uint16_t value;

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
	void calculateFlag(uint8_t value, T flag, Args... flags);
	void calculateFlag(uint8_t value, Flags::Index flag) noexcept;
	void compare(size_t a, size_t b) noexcept;
	void addWithCarry(uint8_t value) noexcept;

	// Instructions
	void oADC(ValueStore) noexcept;
	void oAND(ValueStore) noexcept;
	void oASL(ValueStore) noexcept;
	void oBCC(ValueStore) noexcept;
	void oBCS(ValueStore) noexcept;
	void oBEQ(ValueStore) noexcept;
	void oBIT(ValueStore) noexcept;
	void oBMI(ValueStore) noexcept;
	void oBNE(ValueStore) noexcept;
	void oBPL(ValueStore) noexcept;
	void oBRK(ValueStore) noexcept;
	void oBVC(ValueStore) noexcept;
	void oBVS(ValueStore) noexcept;
	void oCLC(ValueStore) noexcept;
	void oCLD(ValueStore) noexcept;
	void oCLI(ValueStore) noexcept;
	void oCLV(ValueStore) noexcept;
	void oCMP(ValueStore) noexcept;
	void oCPX(ValueStore) noexcept;
	void oCPY(ValueStore) noexcept;
	void oDEC(ValueStore) noexcept;
	void oDEX(ValueStore) noexcept;
	void oDEY(ValueStore) noexcept;
	void oEOR(ValueStore) noexcept;
	void oINC(ValueStore) noexcept;
	void oINX(ValueStore) noexcept;
	void oINY(ValueStore) noexcept;
	void oJMP(ValueStore) noexcept;
	void oJSR(ValueStore) noexcept;
	void oLDA(ValueStore) noexcept;
	void oLDX(ValueStore) noexcept;
	void oLDY(ValueStore) noexcept;
	void oLSR(ValueStore) noexcept;
	void oNOP(ValueStore) noexcept;
	void oORA(ValueStore) noexcept;
	void oPHA(ValueStore) noexcept;
	void oPHP(ValueStore) noexcept;
	void oPLA(ValueStore) noexcept;
	void oPLP(ValueStore) noexcept;
	void oROL(ValueStore) noexcept;
	void oROR(ValueStore) noexcept;
	void oRTI(ValueStore) noexcept;
	void oRTS(ValueStore) noexcept;
	void oSBC(ValueStore) noexcept;
	void oSEC(ValueStore) noexcept;
	void oSED(ValueStore) noexcept;
	void oSEI(ValueStore) noexcept;
	void oSTA(ValueStore) noexcept;
	void oSTX(ValueStore) noexcept;
	void oSTY(ValueStore) noexcept;
	void oTAX(ValueStore) noexcept;
	void oTAY(ValueStore) noexcept;
	void oTSX(ValueStore) noexcept;
	void oTXA(ValueStore) noexcept;
	void oTXS(ValueStore) noexcept;
	void oTYA(ValueStore) noexcept;

	friend class ValueStore;
};
