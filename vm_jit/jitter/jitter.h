#pragma once
#include "../matcher.h"

#include <asmjit/asmjit.h>
#include <unordered_map>
#include <functional>
#include <memory>

namespace jitter
{

    struct jitter
    {
        std::unordered_map<uint64_t, asmjit::Label> labels;
        std::unordered_map<uint64_t, asmjit::x86::Gp> reg_map;

        asmjit::JitRuntime rt;
        asmjit::CodeHolder code;

        std::unique_ptr<asmjit::x86::Compiler> cc;
        std::unique_ptr<asmjit::FileLogger> logger;

        std::vector<asmjit::x86::Gp> stack;
        std::vector<asmjit::Label> dead_branches;

        explicit jitter();

        asmjit::Label get_label(vm::vip_t vip);
        asmjit::Label create_label(vm::vip_t vip);

        asmjit::x86::Gp create_vreg(uint64_t idx);
        asmjit::x86::Gp get_vreg(uint64_t idx);

        asmjit::x86::Gp virtual_pop();
        void virtual_push(asmjit::x86::Gp v);

        void add_instruction(const vm::instruction_t& instr);

        asmjit::CodeBuffer& compile();
    };
}
