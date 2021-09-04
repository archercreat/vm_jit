#include "jitter.h"

namespace jitter
{
    using vm_instruction_lifter = std::function<void(const vm::instruction_t&, jitter&)>;
    static std::unordered_map<vm::opcodes, vm_instruction_lifter> handlers =
    {
        {
            vm::opcodes::PopVreg,
            [](const vm::instruction_t& instr, jitter& jit)
            {
                jit.cc->mov(jit.get_vreg(instr.operand), jit.virtual_pop());

            }
        },
        {
            vm::opcodes::PushVreg,
            [](const vm::instruction_t& instr, jitter& jit)
            {
                auto temp_r = jit.cc->newGpq();
                jit.cc->mov(temp_r, jit.get_vreg(instr.operand));
                jit.virtual_push(temp_r);
            }
        },
        {
            vm::opcodes::PushConst,
            [](const vm::instruction_t& instr, jitter& jit)
            {
                auto temp_r = jit.cc->newUInt64();
                jit.cc->mov(temp_r, instr.operand);
                jit.virtual_push(temp_r);
            }
        },
        {
            vm::opcodes::Read8,
            [](const vm::instruction_t& instr, jitter& jit)
            {
                auto temp_r = jit.virtual_pop();
                jit.cc->movzx(temp_r, asmjit::x86::byte_ptr(temp_r));
                jit.virtual_push(temp_r);
            }
        },
        {
            vm::opcodes::Read64,
            [](const vm::instruction_t& instr, jitter& jit)
            {
                auto temp_r = jit.virtual_pop();
                jit.cc->mov(temp_r, asmjit::x86::ptr(temp_r));
                jit.virtual_push(temp_r);
            }
        },
        {
            vm::opcodes::Add,
            [](const vm::instruction_t& instr, jitter& jit)
            {
                auto temp_r1 = jit.virtual_pop();
                auto temp_r2 = jit.virtual_pop();
                jit.cc->add(temp_r1, temp_r2);
                jit.virtual_push(temp_r1);
            }
        },
        {
            vm::opcodes::Nand,
            [](const vm::instruction_t& instr, jitter& jit)
            {
                asmjit::DebugUtils::unused(instr);
                auto temp_r1 = jit.virtual_pop();
                auto temp_r2 = jit.virtual_pop();
                jit.cc->and_(temp_r1, temp_r2);
                jit.cc->not_(temp_r1);
                jit.virtual_push(temp_r1);
            }
        },
        {
            vm::opcodes::Mul,
            [](const vm::instruction_t& instr, jitter& jit)
            {
                // Why all this?
                // Well, for some reason this is the only way to make Mul work.
                // Preserve rax, rdx -> mul -> restore rax, rdx
                auto t1 = jit.cc->newGpq();
                auto t2 = jit.cc->newGpq();
                auto t3 = jit.cc->newGpq();
                jit.cc->mov(t1, asmjit::x86::rax);
                jit.cc->mov(t2, asmjit::x86::rdx);

                auto arg_1 = jit.virtual_pop();
                auto arg_2 = jit.virtual_pop();

                jit.cc->mul(t3, arg_1, arg_2);
                jit.cc->mov(arg_1, asmjit::x86::rax);
                jit.virtual_push(arg_1);

                jit.cc->mov(asmjit::x86::rax, t1);
                jit.cc->mov(asmjit::x86::rdx, t2);
            }
        },
        {
            vm::opcodes::Jnz,
            [](const vm::instruction_t& instr, jitter& jit)
            {
                auto cmp_r1 = jit.virtual_pop();
                auto cmp_r2 = jit.virtual_pop();
                auto new_rkey = jit.virtual_pop();
                auto new_bytecode = jit.virtual_pop();
                auto new_ror_key = jit.virtual_pop();

                jit.cc->cmp(cmp_r1, cmp_r2);
                if (jit.labels.count(instr.operand))
                {
                    jit.cc->jnz(jit.get_label(instr.operand));
                }
                else
                {
                    auto lbl = jit.create_label(instr.operand);
                    jit.cc->jnz(lbl);
                    jit.dead_branches.push_back(lbl);
                }
            }
        },
        {
            vm::opcodes::Exit,
            [](const vm::instruction_t& instr, jitter& jit)
            {
                jit.cc->mov(asmjit::x86::r15, jit.virtual_pop());
                jit.cc->mov(asmjit::x86::r14, jit.virtual_pop());
                jit.cc->mov(asmjit::x86::r13, jit.virtual_pop());
                jit.cc->mov(asmjit::x86::r12, jit.virtual_pop());
                jit.cc->mov(asmjit::x86::r11, jit.virtual_pop());
                jit.cc->mov(asmjit::x86::r10, jit.virtual_pop());
                jit.cc->mov(asmjit::x86::r9,  jit.virtual_pop());
                jit.cc->mov(asmjit::x86::r8,  jit.virtual_pop());
                jit.cc->mov(asmjit::x86::rbp, jit.virtual_pop());
                jit.cc->mov(asmjit::x86::rsi, jit.virtual_pop());
                jit.cc->mov(asmjit::x86::rdi, jit.virtual_pop());
                jit.cc->mov(asmjit::x86::rdx, jit.virtual_pop());
                jit.cc->mov(asmjit::x86::rcx, jit.virtual_pop());
                jit.cc->mov(asmjit::x86::rbx, jit.virtual_pop());
                jit.cc->ret(jit.virtual_pop());
            }
        }
    };

    jitter::jitter()
    {
        // Initialize CodeHolder from our environment
        //
        code.init(rt.environment());
        // Create logger and set maximum verbose level
        //
        logger = std::make_unique<asmjit::FileLogger>(stdout);
        logger->setFlags(
            asmjit::FormatOptions::Flags::kFlagAnnotations |
            asmjit::FormatOptions::Flags::kFlagDebugPasses |
            asmjit::FormatOptions::Flags::kFlagDebugRA |
            asmjit::FormatOptions::Flags::kFlagExplainImms |
            asmjit::FormatOptions::Flags::kFlagHexImms |
            asmjit::FormatOptions::Flags::kFlagHexOffsets |
            asmjit::FormatOptions::Flags::kFlagMachineCode |
            asmjit::FormatOptions::Flags::kFlagPositions |
            asmjit::FormatOptions::Flags::kFlagRegCasts
        );
        code.setLogger(&*logger);
        // Create compiler
        //
        cc = std::make_unique<asmjit::x86::Compiler>(&code);
        // Enable optimizations
        //
        cc->addEncodingOptions(
            asmjit::BaseEmitter::kEncodingOptionOptimizeForSize |
            asmjit::BaseEmitter::kEncodingOptionOptimizedAlign);
        // Create devirtualized function
        //
        cc->addFunc(asmjit::FuncSignatureT<int>());

        // Create virtual registers
        //
        for (int i = 0; i < 15; i++)
            create_vreg(i);

        // Create VM entry
        //
        virtual_push(asmjit::x86::rax);
        virtual_push(asmjit::x86::rbx);
        virtual_push(asmjit::x86::rcx);
        virtual_push(asmjit::x86::rdx);
        virtual_push(asmjit::x86::rdi);
        virtual_push(asmjit::x86::rsi);
        virtual_push(asmjit::x86::rbp);
        virtual_push(asmjit::x86::r8);
        virtual_push(asmjit::x86::r9);
        virtual_push(asmjit::x86::r10);
        virtual_push(asmjit::x86::r11);
        virtual_push(asmjit::x86::r12);
        virtual_push(asmjit::x86::r13);
        virtual_push(asmjit::x86::r14);
        virtual_push(asmjit::x86::r15);
    }

    asmjit::Label jitter::get_label(vm::vip_t vip)
    {
        return labels.at(vip);
    }

    asmjit::Label jitter::create_label(vm::vip_t vip)
    {
        // Make sure label is free
        //
        assert(!labels.count(vip));
        auto label = cc->newLabel();
        labels.insert({ vip, label });
        return label;
    }

    asmjit::x86::Gp jitter::create_vreg(uint64_t idx)
    {
        auto reg = cc->newGpq("VREG_%d", idx);
        vregs.insert({ idx, reg });
        return reg;
    }

    asmjit::x86::Gp jitter::get_vreg(uint64_t idx)
    {
        // Make sure the register is present
        //
        assert(reg_map.count(idx));
        return vregs.at(idx);
    }

    asmjit::x86::Gp jitter::virtual_pop()
    {
        auto v = stack.back();
        stack.pop_back();
        return v;
    }

    void jitter::virtual_push(asmjit::x86::Gp v)
    {
        stack.push_back(v);
    }

    void jitter::add_instruction(const vm::instruction_t& instr)
    {
        cc->bind(create_label(instr.vip));

        // Ensure Opcode is present
        //
        assert(handlers.count(instr.op));
        // Compile instruction
        //
        handlers.at(instr.op)(instr, *this);
    }

    asmjit::CodeBuffer& jitter::compile()
    {
        // Terminate all dead branches
        //
        for (auto& lbl : dead_branches)
        {
            cc->bind(lbl);
            cc->int3();
        }

        cc->endFunc();
        cc->finalize();

        return code.sectionById(0)->buffer();
    }
}
