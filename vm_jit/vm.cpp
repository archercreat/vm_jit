#include "vm.h"

namespace vm
{
	uint64_t state::decrypt_vip(uint64_t ror_key)
	{
		/*
		* .text:000000014002CCAB                 mov     rax, [r8]	; r8 - vip
		* .text:000000014002CCAE                 add     r8, 8
		* .text:000000014002CCB2                 xor     rax, r10   ; r10 - rkey
		* .text:000000014002CCB5                 ror     rax, 5
		* .text:000000014002CCB9                 xor     r10, rax
		*/
		auto v = *reinterpret_cast<uint64_t*>(vip);
		vip += sizeof(vip);

		v = v ^ rkey;
		v = _rotr64(v, (int)ror_key);
		rkey ^= v;

		return v;
	}

	std::vector<uint64_t> extract_ror_keys(const x86::routine_t& routine)
	{
		std::vector<uint64_t> out;
		int from = 0;
		auto f_ror = [&](const x86::zydis_instruction_t& instr) -> bool
		{
			return instr.mnemonic == ZYDIS_MNEMONIC_ROR &&
				instr.operands[0].type == ZYDIS_OPERAND_TYPE_REGISTER &&
				instr.operands[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE;
		};

		while (true)
		{
			from = routine.next(f_ror, from);
			if (from == -1)
				break;

			assert(from < routine.size() - 1);
			assert(from > 0);

			const auto& before = routine[(size_t)from - 1].instr;
			const auto& after = routine[(size_t)from + 1].instr;

			if (before.mnemonic == ZYDIS_MNEMONIC_XOR &&
				after.mnemonic == ZYDIS_MNEMONIC_XOR)
			{
				out.push_back(routine[from].instr.operands[1].imm.value.u);
			}

			from++;
		}

		return out;
	}

	uint64_t extact_jcc_key(const x86::routine_t& routine)
	{
		auto f_ror = [&](const x86::zydis_instruction_t& instr) -> bool
		{
			return instr.mnemonic == ZYDIS_MNEMONIC_ROR &&
				instr.operands[0].type == ZYDIS_OPERAND_TYPE_REGISTER &&
				instr.operands[0].reg.value == ZYDIS_REGISTER_RAX &&
				instr.operands[1].type == ZYDIS_OPERAND_TYPE_REGISTER &&
				instr.operands[1].reg.value == ZYDIS_REGISTER_CL;
		};

		auto f_load = [&](const x86::zydis_instruction_t& instr) -> bool
		{
			return instr.mnemonic == ZYDIS_MNEMONIC_MOV &&
				instr.operands[0].type == ZYDIS_OPERAND_TYPE_REGISTER &&
				instr.operands[0].reg.value == ZYDIS_REGISTER_RCX &&
				instr.operands[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE;
		};

		// Check if decryption present
		//
		auto i_ror = routine.prev(f_ror);
		assert(i_ror != -1);
		// Find last rcx load
		//
		auto i_load = routine.prev(f_load, i_ror);
		assert(i_load != -1);

		return routine[i_load].instr.operands[1].imm.value.u;
	}
}
