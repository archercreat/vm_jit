#pragma once
#pragma warning( push )
#pragma warning(disable : 4624)
#pragma warning(disable : 4996)
#pragma warning(disable : 4244)
#pragma warning(disable : 4267)
#pragma warning(disable : 4146)
#include <llvm/Support/raw_ostream.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/InlineAsm.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/NoFolder.h>
#include <llvm/IR/IRPrintingPasses.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Pass.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Transforms/InstCombine/InstCombine.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/Transforms/Scalar/GVN.h>
#include <llvm/Transforms/Utils.h>

#pragma warning( pop )
#include <memory>

#include "../vm.h"

namespace lifter
{
	struct lifter
	{
		llvm::FunctionType* function_t = nullptr;
		llvm::Function* function = nullptr;
		llvm::Type* reg_full_t = nullptr;
		llvm::Type* input_t = nullptr;
		llvm::Module& module;
		llvm::LLVMContext& ctx;
		llvm::IRBuilder<llvm::NoFolder> builder;
		
		std::unordered_map<uint64_t, llvm::Instruction*> instructions;
		std::vector<llvm::Value*> stack;
		std::vector<llvm::BasicBlock*> dead_branches;

		lifter(llvm::Module& module);

		llvm::Value* get_preg(uint64_t idx);
		void set_preg(uint64_t idx, llvm::Value* v);

		llvm::Value* virtual_pop();
		void virtual_push(llvm::Value* v);

		llvm::Value* temp_reg();

		void add_instruction(const vm::instruction_t& instr);

		void compile();
	};
}