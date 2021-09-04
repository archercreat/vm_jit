#include "matcher.h"
#include "vm.h"

namespace vm
{
    using virt_matcher_t = std::function<bool(const state&, const x86::routine_t&)>;
    using virt_emulator_t = std::function<void(state&, instruction_t&)>;

    static bool is_pop_reg(const x86::zydis_instruction_t& instr)
    {
        return instr.mnemonic == ZYDIS_MNEMONIC_POP &&
            instr.operands[0].type == ZYDIS_OPERAND_TYPE_REGISTER;
    }

    static bool is_push_reg(const x86::zydis_instruction_t& instr)
    {
        return instr.mnemonic == ZYDIS_MNEMONIC_PUSH &&
            instr.operands[0].type == ZYDIS_OPERAND_TYPE_REGISTER;
    }

    static std::unordered_map<opcodes, std::pair<virt_matcher_t, virt_emulator_t>> instructions =
    {
        {
            /*
            *   mov     rcx, [r8]
            *   add     r8, 8
            *   xor     rcx, r10
            *   ror     rcx, 17h
            *   xor     r10, rcx
            *   pop     qword ptr [r9+rcx*8]
            */
            opcodes::PopVreg,
            {
                [](const state& state, const x86::routine_t& routine) -> bool
                {
                    return routine.next([&](const x86::zydis_instruction_t& instr) -> bool
                        {
                            return instr.mnemonic == ZYDIS_MNEMONIC_POP &&
                                instr.operands[0].type == ZYDIS_OPERAND_TYPE_MEMORY &&
                                instr.operands[0].mem.base == state.vreg_r;
                        }
                    ) != -1;
                },
                [](state& state, instruction_t& instr)
                {
                    state.stack.pop_back();
                }
            }
        },
        {
            /*
            *   mov     rcx, [r8]
            *   add     r8, 8
            *   xor     rcx, r10
            *   ror     rcx, 17h
            *   xor     r10, rcx
            *   push    qword ptr [r9+rcx*8]
            */
            opcodes::PushVreg,
            {
                [](const state& state, const x86::routine_t& routine) -> bool
                {
                    return routine.next([&](const x86::zydis_instruction_t& instr) -> bool
                        {
                            return instr.mnemonic == ZYDIS_MNEMONIC_PUSH &&
                                instr.operands[0].type == ZYDIS_OPERAND_TYPE_MEMORY &&
                                instr.operands[0].mem.base == state.vreg_r;
                        }
                    ) != -1;
                },
                [](state& state, instruction_t& instr)
                {
                    state.stack.push_back(instr.operand);
                }
            }
        },
        {
            /*
            *   mov     rcx, [r8]
            *   add     r8, 8
            *   xor     rcx, r10
            *   ror     rcx, 17h
            *   xor     r10, rcx
            *   push    rcx
            */
            opcodes::PushConst,
            {
                [](const state& state, const x86::routine_t& routine) -> bool
                {
                    auto i_push_reg = routine.next(is_push_reg);
                    if (i_push_reg == -1)
                        return false;

                    return routine[(size_t)i_push_reg - 1].is(ZYDIS_MNEMONIC_XOR, {ZYDIS_OPERAND_TYPE_REGISTER, ZYDIS_OPERAND_TYPE_REGISTER}) &&
                        !routine[(size_t)i_push_reg + 1].is(ZYDIS_MNEMONIC_RET);
                },
                [](state& state, instruction_t& instr)
                {
                    state.stack.push_back(instr.operand);
                }
            }

        },
        {
            /*
            *   pop     rax
            *   movzx   rax, byte ptr [rax]
            *   push    rax
            */
            opcodes::Read8,
            {
                [](const state& state, const x86::routine_t& routine) -> bool
                {
                    return routine.next([&](const x86::zydis_instruction_t& instr) -> bool
                        {
                            return instr.mnemonic == ZYDIS_MNEMONIC_MOVZX &&
                                instr.operands[1].type == ZYDIS_OPERAND_TYPE_MEMORY &&
                                instr.operands[1].mem.base == ZYDIS_REGISTER_RAX;
                        }
                    ) != -1;
                },
                [](state& state, instruction_t& instr)
                {

                }
            }
        },
        {
            /*
            *   pop     rax
            *   mov     rax, [rax]
            *   push    rax
            */
            opcodes::Read64,
            {
                [](const state& state, const x86::routine_t& routine) -> bool
                {
                    return routine.next([&](const x86::zydis_instruction_t& instr) -> bool
                        {
                            return instr.mnemonic == ZYDIS_MNEMONIC_MOV &&
                                instr.operands[1].type == ZYDIS_OPERAND_TYPE_MEMORY &&
                                instr.operands[1].mem.base == ZYDIS_REGISTER_RAX &&
                                instr.operands[0].reg.value == ZYDIS_REGISTER_RAX;
                        }
                    ) != -1;
                },
                [](state& state, instruction_t& instr)
                {

                }
            }
        },
        {
            /*
            *   pop     rax
            *   pop     rbx
            *   add     rax, rbx
            *   push    rax
            */
            opcodes::Add,
            {
                [](const state& state, const x86::routine_t& routine) -> bool
                {
                    auto i_pop_r1 = routine.next(is_pop_reg);
                    auto i_pop_r2 = routine.next(is_pop_reg, i_pop_r1 + 1);
                    if (i_pop_r1 == -1 || i_pop_r2 == -1)
                        return false;

                    return routine[(size_t)i_pop_r2 + 1].is(ZYDIS_MNEMONIC_ADD, { ZYDIS_OPERAND_TYPE_REGISTER, ZYDIS_OPERAND_TYPE_REGISTER });
                },
                [](state& state, instruction_t& instr)
                {
                    state.stack.pop_back();
                }
            }
        },
        {
            /*
            *   pop     rax
            *   pop     rbx
            *   and     rax, rbx
            *   not     rax
            *   push    rax
            */
            opcodes::Nand,
            {
                [](const state& state, const x86::routine_t& routine) -> bool
                {
                    auto i_pop_r1 = routine.next(is_pop_reg);
                    auto i_pop_r2 = routine.next(is_pop_reg, i_pop_r1 + 1);
                    if (i_pop_r1 == -1 || i_pop_r2 == -1)
                        return false;

                    return routine[(size_t)i_pop_r2 + 1].is(ZYDIS_MNEMONIC_AND, { ZYDIS_OPERAND_TYPE_REGISTER, ZYDIS_OPERAND_TYPE_REGISTER });
                },
                [](state& state, instruction_t& instr)
                {
                    state.stack.pop_back();
                }
            }
        },
        {
            /*
            *   pop     rax
            *   pop     rbx
            *   mul     rbx
            *   push    rax
            */
            opcodes::Mul,
            {
                [](const state& state, const x86::routine_t& routine) -> bool
                {
                    auto i_pop_r1 = routine.next(is_pop_reg);
                    auto i_pop_r2 = routine.next(is_pop_reg, i_pop_r1 + 1);
                    if (i_pop_r1 == -1 || i_pop_r2 == -1)
                        return false;

                    return routine[(size_t)i_pop_r2 + 1].is(ZYDIS_MNEMONIC_MUL, { ZYDIS_OPERAND_TYPE_REGISTER });
                },
                [](state& state, instruction_t& instr)
                {
                    state.stack.pop_back();
                }
            }
        },
        {
            opcodes::Jnz,
            {
                /*
                *   pop     rax
                *   pop     rbx
                *   pop     rdx
                *   pop     rdi
                *   pop     rsi
                *   cmp     rax, rbx
                *   mov     rcx, 13h
                *   cmovnz  r10, rdx
                *   cmovnz  r8, rdi
                *   cmovnz  rcx, rsi
                */
                [](const state& state, const x86::routine_t& routine) -> bool
                {
                    auto i_pop_r1 = routine.next(is_pop_reg);
                    auto i_pop_r2 = routine.next(is_pop_reg, i_pop_r1 + 1);
                    auto i_pop_r3 = routine.next(is_pop_reg, i_pop_r2 + 1);
                    auto i_pop_r4 = routine.next(is_pop_reg, i_pop_r3 + 1);
                    if (i_pop_r1 == -1 || i_pop_r2 == -1 || i_pop_r3 == -1 || i_pop_r4 == -1)
                        return false;

                    auto i_cmovne = routine.next([](const x86::zydis_instruction_t instr) -> bool
                    {
                            return instr.mnemonic == ZYDIS_MNEMONIC_CMOVNZ;
                    });
                    return i_cmovne != -1;
                },
                [](state& state, instruction_t& instr)
                {
                    state.stack.pop_back();
                    state.stack.pop_back();
                    state.stack.pop_back();
                    instr.operand = state.stack.back();
                    state.stack.pop_back();
                    state.stack.pop_back();
                }
            }
        },
        {
            /*
            *   pop     r15
            *   pop     r14
            *   pop     r13
            *   pop     r12
            *   pop     r11
            *   pop     r10
            *   pop     r9
            *   pop     r8
            *   pop     rbp
            *   pop     rsi
            *   pop     rdi
            *   pop     rdx
            *   pop     rcx
            *   pop     rbx
            *   pop     rax
            *   retn
            */
            opcodes::Exit,
            {
                [](const state& state, const x86::routine_t& routine) -> bool
                {
                    int from = 0;
                    for (int i = 0; i < 15; i++)
                    {
                        from = routine.next(is_pop_reg, from);
                        if (from == -1)
                            return false;
                        from++;
                    }
                    return true;
                },
                [](state& state, instruction_t& instr)
                {
                    for (int i = 0; i < 15; i++)
                        state.stack.pop_back();
                }
            }
        }
    };

    instruction_t match(state& state, const x86::routine_t& routine, uint64_t operand)
    {
        instruction_t out;
        out.operand = operand;

        for (const auto& [op, matcher] : instructions)
        {
            if (matcher.first(state, routine))
            {
                out.op = op;
                matcher.second(state, out);
                return out;
            }
        }
        return out;
    }
}
