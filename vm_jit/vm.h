#pragma once
#include "disasm.h"

#include <cstdint>
#include <intrin.h>

namespace vm
{
	using vip_t = uint64_t;

	enum class opcodes : uint8_t
	{
		PopVreg,
		PushVreg,
		PushConst,
		Read8,
		Read64,
		Add,
		Nand,
		Mul,
		Jnz,
		Exit,
		Invalid
	};

	struct instruction_t
	{
		opcodes op = opcodes::Invalid;
		vip_t vip = ~0ull;
		uint64_t operand = ~0ull;
	};

	struct state
	{
		vip_t vip;
		uint64_t rkey;

		const x86::zydis_register_t vip_r = ZYDIS_REGISTER_R8;
		const x86::zydis_register_t vreg_r = ZYDIS_REGISTER_R9;
		const x86::zydis_register_t rkey_r = ZYDIS_REGISTER_R10;

		std::vector<uint64_t> stack;

		state(vip_t vip, uint64_t rkey)
			: vip(vip), rkey(rkey), stack(15, 0) {}

		uint64_t decrypt_vip(uint64_t ror_key);
	};

	std::vector<uint64_t> extract_ror_keys(const x86::routine_t& routine);
	uint64_t extact_jcc_key(const x86::routine_t& routine);
}
