#include "lifter.h"
#include "utils.h"
#include <fstream>

namespace lifter
{
	static int load_reg_counter = 0;

	using vm_instruction_lifter = std::function<void(const vm::instruction_t&, lifter&)>;
	static std::unordered_map<vm::opcodes, vm_instruction_lifter> handlers =
	{
        {
            vm::opcodes::PopVreg,
            [](const vm::instruction_t& instr, lifter& cc)
            {
				cc.builder.CreateStore(cc.virtual_pop(), cc.module.getNamedGlobal("vreg_" + 
					std::to_string(instr.operand)));
            }
        },
        {
            vm::opcodes::PushVreg,
            [](const vm::instruction_t& instr, lifter& cc)
            {
				cc.virtual_push(cc.builder.CreateLoad(cc.module.getNamedGlobal("vreg_" + 
					std::to_string(instr.operand))));
            }
        },
        {
            vm::opcodes::PushConst,
            [](const vm::instruction_t& instr, lifter& cc)
            {
				cc.virtual_push(cc.builder.getInt64(instr.operand));
            }
        },
        {
            vm::opcodes::Read8,
            [](const vm::instruction_t& instr, lifter& cc)
            {
				auto* t = cc.virtual_pop();
				auto* t_ptr = cc.builder.CreateIntToPtr(t, cc.builder.getInt8PtrTy());
				auto* t_deref = cc.builder.CreateLoad(t_ptr);
				auto* rs = cc.builder.CreateIntCast(t_deref, cc.builder.getInt64Ty(), false);
				cc.virtual_push(rs);
            }
        },
        {
            vm::opcodes::Read64,
            [](const vm::instruction_t& instr, lifter& cc)
            {
				auto* t = cc.virtual_pop();
				auto* t_ptr = cc.builder.CreateIntToPtr(t, cc.builder.getInt64Ty()->getPointerTo());
				auto* t_deref = cc.builder.CreateLoad(t_ptr);
				auto* rs = cc.builder.CreateIntCast(t_deref, cc.builder.getInt64Ty(), false);
				cc.virtual_push(rs);
            }
        },
        {
            vm::opcodes::Add,
            [](const vm::instruction_t& instr, lifter& cc)
            {
				auto* r1 = cc.virtual_pop();
				auto* r2 = cc.virtual_pop();
				auto* rs = cc.builder.CreateAdd(r1, r2);
				cc.virtual_push(rs);
            }
        },
        {
            vm::opcodes::Nand,
            [](const vm::instruction_t& instr, lifter& cc)
            {
				auto* r1 = cc.virtual_pop();
				auto* r2 = cc.virtual_pop();
				auto* rs = cc.builder.CreateAnd(r1, r2);
				rs = cc.builder.CreateNot(rs);
				cc.virtual_push(rs);
            }
        },
        {
            vm::opcodes::Mul,
            [](const vm::instruction_t& instr, lifter& cc)
            {
				auto* r1 = cc.virtual_pop();
				auto* r2 = cc.virtual_pop();
				auto* rs = cc.builder.CreateMul(r1, r2);
				cc.virtual_push(rs);
            }
        },
        {
            vm::opcodes::Jnz,
            [](const vm::instruction_t& instr, lifter& cc)
            {
				auto* cmp_r1 = cc.virtual_pop();
				auto* cmp_r2 = cc.virtual_pop();
				auto* new_rkey = cc.virtual_pop();
				auto* new_bytecode = cc.virtual_pop();
				auto* new_ror_key = cc.virtual_pop();

				auto* f = cc.module.getFunction("main");

				auto* cond = cc.builder.CreateICmpEQ(cmp_r1, cmp_r2);
				auto* dst_t = llvm::BasicBlock::Create(cc.module.getContext(), 
					std::string("loc_f_") + std::to_string(instr.vip + 1), f);
				if (!cc.instructions.contains(instr.operand))
				{
					auto* dst_f = llvm::BasicBlock::Create(cc.module.getContext(), 
						std::string("loc_f_") + std::to_string(instr.vip + 1), f);
					cc.builder.CreateCondBr(cond, dst_t, dst_f);
					cc.dead_branches.push_back(dst_f);
				}
				else
				{
					auto* i = cc.instructions[instr.operand];
					auto* dst_f = i->getParent()->splitBasicBlock(i, "loc_f_" + std::to_string(instr.vip + 1));
					cc.builder.CreateCondBr(cond, dst_t, dst_f);
				}
				cc.builder.SetInsertPoint(dst_t);
            }
        },
        {
            vm::opcodes::Exit,
            [](const vm::instruction_t& instr, lifter& cc)
            {
				for (int i = 14; i >= 0; i--)
				{
					cc.set_preg(i, cc.virtual_pop());
				}
				cc.builder.CreateRetVoid();
            }
        }
	};



	lifter::lifter(llvm::Module& module) : module(module), ctx(module.getContext()), builder(module.getContext())
	{
		std::vector<llvm::Type*> reg_ty{ builder.getInt64Ty() };
		reg_full_t = llvm::StructType::create(ctx, reg_ty, "RegisterR");

		std::vector<llvm::Type*> input_types;
		for (size_t i = 0; i < 15; i++)
			input_types.push_back(reg_full_t);
		// Create physical registers context struct
		//
		input_t = llvm::StructType::create(ctx, input_types, "ContextTy");
		// Create function signature
		//
		function_t = llvm::FunctionType::get(builder.getVoidTy(), { input_t->getPointerTo() }, false);
		// Create function
		//
		function = llvm::Function::Create(function_t, llvm::Function::ExternalLinkage, "main", module);
		// Create first basic block and set insert point
		//
		auto* head = llvm::BasicBlock::Create(ctx, std::string("loc_") + std::to_string(0), function);
		builder.SetInsertPoint(head);
		// Create virtual registers
		//
		for (int i = 0; i < 15; i++)
			utils::create_global(module, "vreg_" + std::to_string(i), builder.getInt64Ty());
		// Create vm entry
		//
		for (size_t i = 0; i < 15; i++)
			virtual_push(get_preg(i));
	}

	llvm::Value* lifter::get_preg(uint64_t idx)
	{
		std::vector<llvm::Value*> index{
			llvm::ConstantInt::get(llvm::IntegerType::get(ctx, 64), 0),
			llvm::ConstantInt::get(llvm::IntegerType::get(ctx, 32), idx),
			llvm::ConstantInt::get(llvm::IntegerType::get(ctx, 32), 0),
		};

		auto* ptr = llvm::GetElementPtrInst::CreateInBounds(input_t, function->getArg(0), 
			index, "", builder.GetInsertBlock());
		auto p_ptr = builder.CreateLoad(ptr);
		return builder.CreateBitCast(p_ptr, builder.getInt64Ty());
	}

	void lifter::set_preg(uint64_t idx, llvm::Value* v)
	{
		std::vector<llvm::Value*> index{
			llvm::ConstantInt::get(llvm::IntegerType::get(ctx, 64), 0),
			llvm::ConstantInt::get(llvm::IntegerType::get(ctx, 32), idx),
			llvm::ConstantInt::get(llvm::IntegerType::get(ctx, 32), 0),
		};

		auto* ptr = llvm::GetElementPtrInst::CreateInBounds(input_t, function->getArg(0), 
			index, "", builder.GetInsertBlock());
		builder.CreateStore(v, ptr);
	}

	llvm::Value* lifter::virtual_pop()
	{
		auto v = stack.back();
		stack.pop_back();
		return builder.CreateLoad(v);
	}

	void lifter::virtual_push(llvm::Value* v)
	{
		auto t = temp_reg();
		builder.CreateStore(v, t);
		stack.push_back(t);
	}

	llvm::Value* lifter::temp_reg()
	{
		static int temp_reg_counter;
		auto name = "temp_" + std::to_string(temp_reg_counter++);
		return utils::create_global(module, name, builder.getInt64Ty());
	}

	void lifter::add_instruction(const vm::instruction_t& instr)
	{
		// Create nop so we can easily split blocks by vip
		//
		builder.CreateAdd(builder.getInt32(1337), builder.getInt32(1337));
		instructions.insert({ instr.vip, &builder.GetInsertBlock()->back() });
		// Make sure op is present
		//
		assert(handlers.contains(instr.op));
		handlers.at(instr.op)(instr, *this);
	}

	void lifter::compile()
	{
		for (auto* br : dead_branches)
		{
			builder.SetInsertPoint(br);
			builder.CreateBr(br);
		}
		
		llvm::legacy::FunctionPassManager passmgr(&module);
		passmgr.add(llvm::createPromoteMemoryToRegisterPass());
		passmgr.add(llvm::createNewGVNPass());
		passmgr.add(llvm::createReassociatePass());
		passmgr.add(llvm::createDeadCodeEliminationPass());
		passmgr.add(llvm::createInstructionCombiningPass());
		passmgr.add(llvm::createDeadStoreEliminationPass());

		passmgr.run(*module.getFunction("main"));

		utils::dump_to_file(module, "bytecode");
	}
}