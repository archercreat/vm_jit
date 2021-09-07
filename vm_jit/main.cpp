#include "vm.h"
#include "matcher.h"
#include "jitter/jitter.h"
#include "lifter/lifter.h"
#include <fstream>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

static constexpr uint64_t vm_entry_offset = 0x2C07C;
static constexpr uint64_t vip = 0x140067050;
static constexpr uint64_t rkey = 0x1337DEAD6969CAFE;

void print_instruction(vm::instruction_t& instr)
{
    std::string name;
    switch (instr.op)
    {
    case vm::opcodes::PopVreg:	 name = "VM_POP_VREG"; break;
    case vm::opcodes::PushVreg:	 name = "VM_PUSH_VREG"; break;
    case vm::opcodes::PushConst: name = "VM_PUSH_CONST"; break;
    case vm::opcodes::Read8:	 name = "VM_READ_8"; break;
    case vm::opcodes::Read64:	 name = "VM_READ_64"; break;
    case vm::opcodes::Add:		 name = "VM_ADD"; break;
    case vm::opcodes::Nand:		 name = "VM_NAND"; break;
    case vm::opcodes::Mul:		 name = "VM_MUL"; break;
    case vm::opcodes::Jnz:		 name = "VM_JNZ"; break;
    case vm::opcodes::Exit:		 name = "VM_EXIT"; break;
    default:					 name = "INVALID"; break;
    }
    std::printf("0x%llx %s 0x%llx\n", instr.vip, name.c_str(), instr.operand);
}

int main(int argc, const char** argv)
{
    if (argc != 3)
    {
        std::printf("Usage: %s vm.exe -llvm or -asmjit\n", argv[0]);
        return 0;
    }

    bool is_llvm = !std::strcmp(argv[2], "-llvm");
    bool is_jit = !std::strcmp(argv[2], "-asmjit");

    LoadLibraryExA(argv[1], NULL, DONT_RESOLVE_DLL_REFERENCES);

    llvm::LLVMContext ctx;
    llvm::Module program("Module", ctx);
    auto lifter = lifter::lifter(program);

    auto jitter = jitter::jitter();

    auto state = vm::state(vip, rkey);

    // Initial ror key
    //
    uint64_t ror_key = 5;

    while (true)
    {
        // Save instruction VIP for later use
        //
        vm::vip_t temp_vip = state.vip;
        auto next_handler = state.decrypt_vip(ror_key);
        auto routine = x86::unroll(next_handler);
        // Extract ror keys
        //
        auto ror_keys = vm::extract_ror_keys(routine);

        uint64_t operand = 0;

        if (ror_keys.size() > 1)
        {
            // Extract operand
            //
            assert(ror_keys.size() == 2);
            operand = state.decrypt_vip(ror_keys[0]);
        }

        auto instr = vm::match(state, routine, operand);
        instr.vip = temp_vip;

        // Jit instruction
        //
        if (is_llvm) lifter.add_instruction(instr);
        if (is_jit) jitter.add_instruction(instr);

        // Process control flow
        //
        if (instr.op == vm::opcodes::Invalid)
        {
            routine.dump();
            break;
        }
        else if (instr.op == vm::opcodes::Jnz)
        {
            ror_key = vm::extact_jcc_key(routine);
        }
        else if (instr.op == vm::opcodes::Exit)
        {
            break;
        }
        else
        {
            // Last key is always next handler decryption key
            //
            ror_key = ror_keys.back();
        }
    }

    if (is_llvm) lifter.compile();
    
    if (is_jit)
    {
        auto& f = jitter.compile();
        // Copy and patch file
        //
        std::ifstream is(argv[1], std::ios::in | std::ifstream::binary);
        std::ofstream of("output.exe", std::ios::out | std::ios::binary);
        of << is.rdbuf();
        of.seekp(vm_entry_offset);
        of.write((const char*)f.data(), f.size());
        of.close();
        is.close();
    }
}
