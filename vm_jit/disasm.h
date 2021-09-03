#pragma once
#include <Zydis/Zydis.h>
#include <vector>
#include <string>
#include <map>
#include <functional>

namespace x86
{
    using zydis_instruction_t = ZydisDecodedInstruction;
    using zydis_decoded_operand_t = ZydisDecodedOperand;
    using zydis_register_t = ZydisRegister;
    using fn_instruction_filter_t = std::function<bool(const zydis_instruction_t&)>;

    namespace reg
    {
        zydis_register_t extend(const zydis_register_t&);
        std::string to_string(const zydis_register_t&);
        bool is_selector(const zydis_register_t&);
        uint16_t size(const zydis_register_t&);
    };

    struct instruction_t
    {
        uint64_t address;
        zydis_instruction_t instr;
        std::vector<uint8_t> raw;

        std::string to_string() const;
        bool is_jmp() const;
        bool is(ZydisMnemonic mnemonic, const std::vector<ZydisOperandType>& operands = {});
    };

    struct routine_t
    {
        std::vector<instruction_t> stream;

        int next(const fn_instruction_filter_t& filter, int from = 0) const;
        int next(ZydisMnemonic& opcode, std::vector<ZydisOperandType>& params, int from = 0) const;
        int prev(const fn_instruction_filter_t& filter, int from = -1) const;
        int prev(ZydisMnemonic& opcode, std::vector<ZydisOperandType>& params, int from = -1) const;

        void dump() const;

        std::vector<uint8_t> to_raw() const;

        size_t size() const { return stream.size(); }
        auto begin() { return stream.begin(); }
        auto end() { return stream.end(); }
        auto operator[](size_t n) const { return stream[n]; }
    };

    routine_t unroll(uintptr_t address);
}
