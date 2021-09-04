#include "disasm.h"

namespace x86
{
    namespace reg
    {
        zydis_register_t extend(const zydis_register_t& reg)
        {
            return ZydisRegisterGetLargestEnclosing(ZYDIS_MACHINE_MODE_LONG_64, reg);
        }

        std::string to_string(const zydis_register_t& reg)
        {
            return { ZydisRegisterGetString(reg) };
        }

        bool is_selector(const zydis_register_t& reg)
        {
            switch (reg)
            {
            case ZYDIS_REGISTER_SS:
            case ZYDIS_REGISTER_GS:
            case ZYDIS_REGISTER_FS:
            case ZYDIS_REGISTER_DS:
            case ZYDIS_REGISTER_ES:
            case ZYDIS_REGISTER_CS:
                return true;
            default:
                break;
            }
            return false;
        }

        uint16_t size(const zydis_register_t& reg)
        {
            return ZydisRegisterGetWidth(ZYDIS_MACHINE_MODE_LONG_64, reg) / 8;
        }
    }

    std::string instruction_t::to_string() const
    {
        char buffer[256];
        char out[512];

        ZydisFormatter formatter;
        ZydisFormatterInit(&formatter, ZYDIS_FORMATTER_STYLE_INTEL);
        ZydisFormatterFormatInstruction(&formatter, &instr, buffer, sizeof(buffer), address);
        std::snprintf(out, 512, "0x%016llx %s", address, buffer);
        return { out };
    }

    bool instruction_t::is_jmp() const
    {
        switch (instr.mnemonic)
        {
        case ZYDIS_MNEMONIC_JB:
        case ZYDIS_MNEMONIC_JBE:
        case ZYDIS_MNEMONIC_JCXZ:
        case ZYDIS_MNEMONIC_JECXZ:
        case ZYDIS_MNEMONIC_JKNZD:
        case ZYDIS_MNEMONIC_JKZD:
        case ZYDIS_MNEMONIC_JL:
        case ZYDIS_MNEMONIC_JLE:
        case ZYDIS_MNEMONIC_JNB:
        case ZYDIS_MNEMONIC_JNBE:
        case ZYDIS_MNEMONIC_JMP:
        case ZYDIS_MNEMONIC_JNL:
        case ZYDIS_MNEMONIC_JNLE:
        case ZYDIS_MNEMONIC_JNO:
        case ZYDIS_MNEMONIC_JNP:
        case ZYDIS_MNEMONIC_JNS:
        case ZYDIS_MNEMONIC_JNZ:
        case ZYDIS_MNEMONIC_JO:
        case ZYDIS_MNEMONIC_JP:
        case ZYDIS_MNEMONIC_JRCXZ:
        case ZYDIS_MNEMONIC_JS:
        case ZYDIS_MNEMONIC_JZ:
            return true;
        default:
            break;
        }
        return false;
    }

    bool instruction_t::is(ZydisMnemonic mnemonic, const std::vector<ZydisOperandType>& operands)
    {
        if (instr.mnemonic != mnemonic ||
            instr.operand_count < operands.size())
            return false;

        for (int i = 0; i < operands.size(); i++)
        {
            if (instr.operands[i].type != operands[i])
                return false;
        }

        return true;
    }

    int routine_t::next(const fn_instruction_filter_t& filter, int from) const
    {
        if (from >= stream.size()) return -1;
        for (int i = from; i < stream.size(); i++)
            if (filter(stream[i].instr)) return i;
        return -1;
    }

    int routine_t::next(ZydisMnemonic& opcode, std::vector<ZydisOperandType>& params, int from) const
    {
        if (from >= stream.size()) return -1;
        for (int i = from; i < stream.size(); i++)
        {
            auto& ins = stream[i].instr;
            if (ins.mnemonic == opcode && params.size() <= ins.operand_count)
            {
                bool found = true;
                for (int j = 0; j < params.size(); j++)
                    if (params[j] != ins.operands[j].type)
                        found = false;
                if (found)
                    return i;
            }
        }
        return -1;
    }

    int routine_t::prev(const fn_instruction_filter_t& filter, int from) const
    {
        if (from == -1) from = (int)stream.size() - 1;
        if (from >= stream.size()) return -1;
        for (int i = from; i >= 0; i--)
            if (filter(stream[i].instr)) return i;
        return -1;
    }

    int routine_t::prev(ZydisMnemonic& opcode, std::vector<ZydisOperandType>& params, int from) const
    {
        if (from == -1) from = (int)stream.size() - 1;
        if (from >= stream.size()) return -1;
        for (int i = from; i >= 0; i--)
        {
            auto& ins = stream[i].instr;
            if (ins.mnemonic == opcode && params.size() <= ins.operand_count)
            {
                bool found = true;
                for (int j = 0; j < params.size(); j++)
                    if (params[j] != ins.operands[j].type)
                        found = false;
                if (found)
                    return i;
            }
        }
        return -1;
    }

    void routine_t::dump() const
    {
        char buffer[256];
        ZydisFormatter formatter;
        ZydisFormatterInit(&formatter, ZYDIS_FORMATTER_STYLE_INTEL);

        for (const auto& [addr, instr, raw] : stream)
        {
            ZydisFormatterFormatInstruction(&formatter, &instr, buffer, sizeof(buffer), addr);
            std::printf("> 0x%016llx %s\n", addr, buffer);
        }
    }

    std::vector<uint8_t> routine_t::to_raw() const
    {
        std::vector<uint8_t> out;
        for (const auto& i : stream)
            out.insert(out.end(), i.raw.begin(), i.raw.end());
        return out;
    }

    routine_t unroll(uintptr_t address)
    {
        routine_t routine;

        ZydisDecoder decoder;
        ZydisDecodedInstruction zydis_ins;
        ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_ADDRESS_WIDTH_64);

        while (ZYAN_SUCCESS(ZydisDecoderDecodeBuffer(&decoder, reinterpret_cast<void*>(address), 0x1000, &zydis_ins)))
        {
            instruction_t instr;
            instr.address = address;
            instr.instr = zydis_ins;
            instr.raw = { (uint8_t*)address, (uint8_t*)address + zydis_ins.length };

            if (instr.is_jmp())
            {
                if (zydis_ins.operands[0].type == ZYDIS_OPERAND_TYPE_REGISTER)
                {
                    routine.stream.push_back(instr);
                    return routine;
                }

                ZydisCalcAbsoluteAddress(&zydis_ins, &zydis_ins.operands[0], address, &address);
            }
            else if (zydis_ins.mnemonic == ZYDIS_MNEMONIC_RET)
            {
                routine.stream.push_back(instr);
                return routine;
            }
            else
            {
                routine.stream.push_back(instr);
                address += zydis_ins.length;
            }
        }
        return routine;
    }
}
