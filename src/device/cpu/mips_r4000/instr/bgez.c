static r4k_exc_t instr_bgez(r4k_cpu_t *cpu, r4k_instr_t instr)
{
	bool cond;

	if (CPU_64BIT_MODE(cpu))
		cond = ((cpu->regs[instr.i.rs].val & SBIT64) == 0);
	else
		cond = ((cpu->regs[instr.i.rs].lo & SBIT32) == 0);

	if (cond) {
		cpu->pc_next.ptr +=
		    (((int64_t) sign_extend_16_64(instr.i.imm)) << TARGET_SHIFT);
		cpu->branch = BRANCH_COND;
		return r4k_excJump;
	}

	return r4k_excNone;
}

static void mnemonics_bgez(ptr64_t addr, r4k_instr_t instr,
    string_t *mnemonics, string_t *comments)
{
	string_printf(mnemonics, "bgez");
	disassemble_rs_offset(addr, instr, mnemonics, comments);
}
