#pragma once
#include "disasm.h"
#include "vm.h"

namespace vm
{
    instruction_t match(state& state, const x86::routine_t& routine, uint64_t operand);
}
