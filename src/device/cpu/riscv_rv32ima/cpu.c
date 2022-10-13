#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "cpu.h"
#include "debug.h"
#include "csr.h"
#include "../../../assert.h"
#include "../../../utils.h"
#include "../../../physmem.h"
#include "../../../main.h"
#include "../../../list.h"

typedef struct {
    item_t item;
    ptr36_t addr;
    rv_instr_func_t instrs[FRAME_SIZE / sizeof(rv_instr_t)];
} cache_item_t;

#define PHYS2CACHEINSTR(phys) (((phys) & FRAME_MASK)/sizeof(rv_instr_t))

static void cache_item_init(cache_item_t* cache_item){
    item_init(&cache_item->item);
    cache_item->addr = 0;
    // Skip instrs init
}

list_t rv_instruction_cache = LIST_INITIALIZER;

static bool cache_hit(ptr36_t phys, cache_item_t** cache_item){
    ptr36_t target_page = ALIGN_DOWN(phys, FRAME_SIZE);
    cache_item_t* c;
    for_each(rv_instruction_cache, c, cache_item_t){
        if(c->addr == target_page){
            *cache_item = c;
            return true;
        }
    }
    return false;
}

static void cache_item_page_decode(rv_cpu_t* cpu, cache_item_t* cache_item) {
    for(size_t i = 0; i < FRAME_SIZE / sizeof(rv_instr_t); ++i){
        ptr36_t addr = cache_item->addr + (i * sizeof(rv_instr_t));
        rv_instr_t instr_data = (rv_instr_t)physmem_read32(cpu->csr.mhartid, addr, false);
        cache_item->instrs[i] = rv_instr_decode(instr_data);
    }
}

static void update_cache_item(rv_cpu_t* cpu, cache_item_t* cache_item) {
    frame_t* frame = physmem_find_frame(cache_item->addr);
    ASSERT(frame != NULL);

    if(frame->valid) return;

    cache_item_page_decode(cpu, cache_item);

    frame->valid = true;
    return;
}

static cache_item_t* cache_try_add(rv_cpu_t* cpu, ptr36_t phys) {
    frame_t* frame = physmem_find_frame(phys);
    if(frame == NULL) return NULL;

    cache_item_t* cache_item = safe_malloc(sizeof(cache_item_t));

    cache_item_init(cache_item);
    cache_item->addr = ALIGN_DOWN(phys, FRAME_SIZE);

    list_append(&rv_instruction_cache, &cache_item->item);

    cache_item_page_decode(cpu, cache_item);

    frame->valid = true;
    return cache_item;
}

static rv_instr_func_t fetch_instr(rv_cpu_t* cpu, ptr36_t phys){
    cache_item_t* cache_item = NULL;

    if(cache_hit(phys, &cache_item)){
        ASSERT(cache_item != NULL);
        update_cache_item(cpu, cache_item);
        return cache_item->instrs[PHYS2CACHEINSTR(phys)];
    }
    
    cache_item = cache_try_add(cpu, phys);

    if(cache_item != NULL) {
        return cache_item->instrs[PHYS2CACHEINSTR(phys)];
    }
    alert("Trying to fetch instructions from outside of physical memory");
    return rv_instr_decode((rv_instr_t)physmem_read32(cpu->csr.mhartid, phys, true));
}


static void init_regs(rv_cpu_t *cpu) {
    ASSERT(cpu != NULL);

    // expects that default value for any variable is 0

    cpu->pc = RV_START_ADDRESS;
    cpu->pc_next = RV_START_ADDRESS + 4;
}


void rv_cpu_init(rv_cpu_t *cpu, unsigned int procno){

    ASSERT(cpu!=NULL);

    memset(cpu, 0, sizeof(rv_cpu_t));

    init_regs(cpu);

    rv_init_csr(&cpu->csr, procno);

    cpu->priv_mode = rv_mmode;
}

void rv_cpu_done(rv_cpu_t *cpu) {
    // Clean whole cache for simplicity whenever any cpu is done
    while(!is_empty(&rv_instruction_cache)){
        cache_item_t* cache_item = (cache_item_t*)(rv_instruction_cache.head);
        list_remove(&rv_instruction_cache, &cache_item->item);
        safe_free(cache_item);
    }
}
typedef struct {
    unsigned int v: 1;
    unsigned int r: 1;
    unsigned int w: 1;
    unsigned int x: 1;
    unsigned int u: 1;
    unsigned int g: 1;
    unsigned int a: 1;
    unsigned int d: 1;
    unsigned int rsw: 2;
    unsigned int ppn: 22;
} sv32_pte_t;

static_assert((sizeof(sv32_pte_t) == 4), "wrong size of sv32_pte_t");

#define is_pte_leaf(pte) (pte.r | pte.w | pte.x)
#define is_pte_valid(pte) (pte.v && (!pte.w || pte.r))
#define pte_ppn0(pte) (pte.ppn & 0x0003FF)
#define pte_ppn1(pte) (pte.ppn & 0x3FFC00)

// Dirty hack
typedef union {
    sv32_pte_t pte;
    uint32_t val;
} sv32_pte_helper_t;
#define pte_from_uint(val) (((sv32_pte_helper_t)(val)).pte)
#define uint_from_pte(pte) (((sv32_pte_helper_t)(pte)).val)

#define sv32_effective_priv(cpu) (rv_csr_mstatus_mprv(cpu) ? rv_csr_mstatus_mpp(cpu) : cpu->priv_mode)
#define effective_priv(cpu) (rv_csr_satp_is_bare(cpu) ? cpu->priv_mode : sv32_effective_priv(cpu))

static bool is_access_allowed(rv_cpu_t *cpu, sv32_pte_t pte, bool wr, bool fetch) {
    if(wr && !pte.w) return false;

    if(fetch && !pte.x) return false;

    // Page is executable and I can read from executable pages
    bool rx = rv_csr_sstatus_mxr(cpu) && pte.x;
    
    if(!wr && !fetch && !pte.r && !rx) return false;

    if(sv32_effective_priv(cpu) == rv_smode){
        if(!rv_csr_sstatus_sum(cpu) && pte.u) return false;
        if(fetch && pte.u) return false;
    }

    if(sv32_effective_priv(cpu) == rv_umode){
        if(!pte.u) return false;
    }

    return true;
}

static ptr36_t make_phys_from_ppn(uint32_t virt, sv32_pte_t pte, bool megapage){
    ptr36_t page_offset = virt & 0x00000FFF;
    ptr36_t virt_vpn0   = virt & 0x003FF000;
    ptr36_t pte_ppn0    = (ptr36_t)pte_ppn0(pte) << 12;
    ptr36_t pte_ppn1    = (ptr36_t)pte_ppn1(pte) << 12;
    ptr36_t phys_ppn0   = megapage ? virt_vpn0 : pte_ppn0;

    return pte_ppn1 | phys_ppn0 | page_offset;
}

rv_exc_t rv_convert_addr(rv_cpu_t *cpu, uint32_t virt, ptr36_t *phys, bool wr, bool fetch, bool noisy){
    ASSERT(cpu != NULL);
    ASSERT(phys != NULL);
    ASSERT(!(wr && fetch));

    #define page_fault_exception (fetch ? rv_exc_instruction_page_fault : (wr ? rv_exc_store_amo_page_fault : rv_exc_load_page_fault))

    bool satp_active = (!rv_csr_satp_is_bare(cpu)) && (sv32_effective_priv(cpu) <= rv_smode);

    if(!satp_active){
        *phys = virt;
        return rv_exc_none;
    }

    uint32_t vpn0     =    (virt & 0x003FF000) >> 12;
    uint32_t vpn1     =    (virt & 0xFFC00000) >> 22;
    uint32_t ppn      =     rv_csr_satp_ppn(cpu);

    // name of variables according to spec
    int PAGESIZE = 12;
    int PTESIZE = 4;

    ptr36_t a = ((ptr36_t)ppn) << PAGESIZE;
    ptr36_t pte_addr = a + vpn1*PTESIZE;

    // PMP or PMA check goes here if implemented
    uint32_t pte_val = physmem_read32(cpu->csr.mhartid, pte_addr, noisy);

    sv32_pte_t pte = pte_from_uint(pte_val);

    if(!is_pte_valid(pte)) return page_fault_exception;

    bool is_megapage = false;

    if(is_pte_leaf(pte)) {
        // MEGAPAGE
        // Missaligned megapage
        if(pte_ppn0(pte) != 0) return page_fault_exception;
        is_megapage = true;
    }
    else {
        // Non leaf PTE, make second translation step

        // PMP or PMA check goes here if implemented
        a = ((ptr36_t)pte.ppn) << PAGESIZE;
        pte_addr = a + vpn0*PTESIZE;

        pte_val = physmem_read32(cpu->csr.mhartid, pte_addr, noisy);
        pte = pte_from_uint(pte_val);

        if(!is_pte_valid(pte)) return page_fault_exception;

        // Non-leaf on last level
        if(!is_pte_leaf(pte)) return page_fault_exception;
    }

    if(!is_access_allowed(cpu, pte, wr, fetch)) return page_fault_exception;

    pte.a = 1;
    pte.d |= wr ? 1 : 0;

    pte_val = uint_from_pte(pte);

    if(noisy){
        physmem_write32(cpu->csr.mhartid, pte_addr, pte_val, true);
    }
    *phys = make_phys_from_ppn(virt, pte, is_megapage);
    return rv_exc_none;

    #undef page_fault_exception
}

#define read_address_misaligned_exception (fetch ? rv_exc_instruction_address_misaligned : rv_exc_load_address_misaligned)

#define try_read_memory_mapped_regs_body(cpu, virt, value, width, type)         \
    if(!IS_ALIGNED(virt, width/8)) return false;                                \
    if(effective_priv(cpu) != rv_mmode) return false;                           \
    int offset = (virt & 0x7) * 8;                                              \
    if(ALIGN_DOWN(virt, 8) == RV_MTIME_ADDRESS){                                \
        *value = (type)EXTRACT_BITS(cpu->csr.mtime, offset, offset + width);    \
        return true;                                                            \
    }                                                                           \
    if(ALIGN_DOWN(virt, 8) == RV_MTIMECMP_ADDRESS){                             \
        *value = (type)EXTRACT_BITS(cpu->csr.mtimecmp, offset, offset + width); \
        return true;                                                            \
    }                                                                           \
    return false;                                                           

static bool try_read_memory_mapped_regs_32(rv_cpu_t *cpu, uint32_t virt, uint32_t* value){
    try_read_memory_mapped_regs_body(cpu, virt, value, 32, uint32_t)
}

static bool try_read_memory_mapped_regs_16(rv_cpu_t *cpu, uint32_t virt, uint16_t* value){
    try_read_memory_mapped_regs_body(cpu, virt, value, 16, uint16_t)
}

static bool try_read_memory_mapped_regs_8(rv_cpu_t *cpu, uint32_t virt, uint8_t* value){
    try_read_memory_mapped_regs_body(cpu, virt, value, 8, uint8_t)
}

#undef try_read_memory_mapped_regs_body

static bool try_write_memory_mapped_regs(rv_cpu_t *cpu, uint32_t virt, uint32_t value, int width){
    if(!IS_ALIGNED(virt, width / 8)) return false;

    if(effective_priv(cpu) != rv_mmode) return false;                           
    int offset = (virt & 0x7) * 8;                                              
    if(ALIGN_DOWN(virt, 8) == RV_MTIME_ADDRESS){                                
        cpu->csr.mtime = WRITE_BITS(cpu->csr.mtime, value, offset, offset + width);
        return true;                                                            
    }                                                                           
    if(ALIGN_DOWN(virt, 8) == RV_MTIMECMP_ADDRESS){                             
        cpu->csr.mtimecmp = WRITE_BITS(cpu->csr.mtimecmp, value, offset, offset + width); 
        return true;                                                            
    }                                                                           
    return false;                                                           
}

#define throw_ex(cpu, virt, ex, noisy)  \
    if(noisy){                          \
        cpu->csr.tval_next = virt;      \
    }                                   \
    return ex;                          \

rv_exc_t rv_read_mem32(rv_cpu_t *cpu, uint32_t virt, uint32_t *value, bool fetch, bool noisy){
    ASSERT(cpu != NULL);
    ASSERT(value != NULL);


    if(try_read_memory_mapped_regs_32(cpu, virt, value)) return rv_exc_none;

    ptr36_t phys;
    rv_exc_t ex = rv_convert_addr(cpu, virt, &phys, false, fetch, noisy);

    // Address translation exceptions have priority to alignment exceptions

    if(ex != rv_exc_none){
        throw_ex(cpu, virt, ex, noisy);
    }
    
    if(!IS_ALIGNED(virt, 4)){
        throw_ex(cpu, virt, read_address_misaligned_exception, noisy);
    }

    *value = physmem_read32(cpu->csr.mhartid, phys, true);
    return rv_exc_none;
}

rv_exc_t rv_read_mem16(rv_cpu_t *cpu, uint32_t virt, uint16_t *value, bool fetch, bool noisy){
    ASSERT(cpu != NULL);
    ASSERT(value != NULL);

    if(try_read_memory_mapped_regs_16(cpu, virt, value)) return rv_exc_none;

    ptr36_t phys;
    rv_exc_t ex = rv_convert_addr(cpu, virt, &phys, false, fetch, noisy);
    
    if(ex != rv_exc_none){
        throw_ex(cpu, virt, ex, noisy);
    }

    if(!IS_ALIGNED(virt, 2)){
        throw_ex(cpu, virt, read_address_misaligned_exception, noisy);
    }

    *value = physmem_read16(cpu->csr.mhartid, phys, true);
    return rv_exc_none;
}

rv_exc_t rv_read_mem8(rv_cpu_t *cpu, uint32_t virt, uint8_t *value, bool noisy){
    ASSERT(cpu != NULL);
    ASSERT(value != NULL);

    if(try_read_memory_mapped_regs_8(cpu, virt, value)) return rv_exc_none;

    ptr36_t phys;
    rv_exc_t ex = rv_convert_addr(cpu, virt, &phys, false, false, noisy);
    
    if(ex != rv_exc_none){
        throw_ex(cpu, virt, ex, noisy);
    }

    *value = physmem_read8(cpu->csr.mhartid, phys, true);
    return rv_exc_none;
}

rv_exc_t rv_write_mem8(rv_cpu_t *cpu, uint32_t virt, uint8_t value, bool noisy){
    ASSERT(cpu != NULL);

    if(try_write_memory_mapped_regs(cpu, virt, value, 8)) return rv_exc_none;

    ptr36_t phys;
    rv_exc_t ex = rv_convert_addr(cpu, virt, &phys, true, false, noisy);
    
    if(ex != rv_exc_none){
        throw_ex(cpu, virt, ex, noisy);
    }

    if(physmem_write8(cpu->csr.mhartid, phys, value, true)){
        return rv_exc_none;
    }
    
    // writing invalid memory
    // throw_ex(cpu, virt, rv_exc_store_amo_access_fault, noisy);
    return rv_exc_none;
}

rv_exc_t rv_write_mem16(rv_cpu_t *cpu, uint32_t virt, uint16_t value, bool noisy){
    ASSERT(cpu != NULL);

    if(try_write_memory_mapped_regs(cpu, virt, value, 16)) return rv_exc_none;

    ptr36_t phys;
    rv_exc_t ex = rv_convert_addr(cpu, virt, &phys, true, false, noisy);

    // address translation exceptions have priority to alignment exceptions
    if(ex != rv_exc_none){
        throw_ex(cpu, virt, ex, noisy);
    }

    if(!IS_ALIGNED(virt, 2)){
        throw_ex(cpu, virt, rv_exc_store_amo_address_misaligned, noisy);
    }

    if(physmem_write16(cpu->csr.mhartid, phys, value, true)){
        return rv_exc_none;
    }

    // writing invalid memory
    // throw_ex(cpu, virt, rv_exc_store_amo_access_fault, noisy);
    return rv_exc_none;
}


rv_exc_t rv_write_mem32(rv_cpu_t *cpu, uint32_t virt, uint32_t value, bool noisy){
    ASSERT(cpu != NULL);

    if(try_write_memory_mapped_regs(cpu, virt, value, 32)) return rv_exc_none;

    ptr36_t phys;
    rv_exc_t ex = rv_convert_addr(cpu, virt, &phys, true, false, noisy);
    
    if(ex != rv_exc_none){
        throw_ex(cpu, virt, ex, noisy);
    }

    if(!IS_ALIGNED(virt, 4)){
        throw_ex(cpu, virt, rv_exc_store_amo_address_misaligned, noisy);
    }

    if(physmem_write32(cpu->csr.mhartid, phys, value, true)){
        return rv_exc_none;
    }

    // writing invalid memory
    // throw_ex(cpu, virt, rv_exc_store_amo_access_fault, noisy);
    return rv_exc_none;
}

#undef throw_ex

void rv_cpu_set_pc(rv_cpu_t *cpu, uint32_t value){
    ASSERT(cpu != NULL);
    if(!IS_ALIGNED(value, 4)) return;
    /* Set both pc and pc_next
     * This should be called from the debugger to jump somewhere
     * and in case the new instruction does not modify pc_next,
     * the processor would then jump back to where it was before this call
     */
    cpu->pc = value;
    cpu->pc_next = value+4;    
}

static void m_trap(rv_cpu_t* cpu, rv_exc_t ex){
    ASSERT(ex != rv_exc_none);

    bool is_interrupt = ex & RV_INTERRUPT_EXC_BITS;
    cpu->stdby = false;
    
    cpu->csr.mepc = is_interrupt ? cpu->pc_next : cpu->pc;
    cpu->csr.mcause = ex;
    cpu->csr.mtval = cpu->csr.tval_next;

    // MPIE = MIE
    {
        bool mie_set = rv_csr_mstatus_mie(cpu);

        if(mie_set){
            cpu->csr.mstatus |= rv_csr_mstatus_mpie_mask;
        }
        else {
            cpu->csr.mstatus &= ~rv_csr_mstatus_mpie_mask;
        }
    }
    // MIE = 0
    {
        cpu->csr.mstatus &= ~rv_csr_mstatus_mie_mask;
    }
    // MPP = cpu->priv_mode
    {
        cpu->csr.mstatus &= ~rv_csr_mstatus_mpp_mask;
        cpu->csr.mstatus |= ((uint32_t)cpu->priv_mode << rv_csr_mstatus_mpp_pos) & rv_csr_mstatus_mpp_mask;
    }

    cpu->priv_mode = rv_mmode;

    int mode = cpu->csr.mtvec & rv_csr_mtvec_mode_mask;
    uint32_t base = cpu->csr.mtvec & ~rv_csr_mtvec_mode_mask;

    if(mode == rv_csr_mtvec_mode_direct){
        cpu->pc_next = base;
    }
    else if(mode == rv_csr_mtvec_mode_vectored){
        if(is_interrupt) {
            cpu->pc_next = base + 4 * (ex & ~RV_INTERRUPT_EXC_BITS);
        }
        else {
            cpu->pc_next = base;
        }
    }
    else {
        ASSERT(false);
    }
}

static void s_trap(rv_cpu_t* cpu, rv_exc_t ex){
    ASSERT(ex != rv_exc_none);

    bool is_interrupt = ex & RV_INTERRUPT_EXC_BITS;
    cpu->stdby = false;

    cpu->csr.sepc = is_interrupt ? cpu->pc_next : cpu->pc;
    cpu->csr.scause = ex;
    cpu->csr.stval = cpu->csr.tval_next;

    // SPIE = SIE
    {
        bool sie_set = rv_csr_sstatus_sie(cpu);

        if(sie_set){
            cpu->csr.mstatus |= rv_csr_sstatus_spie_mask;
        }
        else {
            cpu->csr.mstatus &= ~rv_csr_sstatus_spie_mask;
        }
    }
    // SIE = 0
    {
        cpu->csr.mstatus &= ~rv_csr_sstatus_sie_mask;
    }
    // SPP = cpu->priv_mode
    {
        cpu->csr.mstatus &= ~rv_csr_sstatus_spp_mask;
        cpu->csr.mstatus |= ((uint32_t)cpu->priv_mode << rv_csr_sstatus_spp_pos) & rv_csr_sstatus_spp_mask;
    }

    cpu->priv_mode = rv_smode;

    int mode = cpu->csr.stvec & rv_csr_mtvec_mode_mask;
    uint32_t base = cpu->csr.stvec & ~rv_csr_mtvec_mode_mask;

    if(mode == rv_csr_mtvec_mode_direct){
        cpu->pc_next = base;
    }
    else if(mode == rv_csr_mtvec_mode_vectored){
        if(is_interrupt) {
            cpu->pc_next = base + 4 * (ex & ~RV_INTERRUPT_EXC_BITS);
        }
        else {
            cpu->pc_next = base;
        }
    }
    else {
        ASSERT(false);
    }
}

static void handle_exception(rv_cpu_t* cpu, rv_exc_t ex){
    uint32_t mask = RV_EXCEPTION_MASK(ex);
    bool delegated = cpu->csr.medeleg & mask;

    if(delegated && cpu->priv_mode != rv_mmode){
        s_trap(cpu, ex);
    }
    else {
        m_trap(cpu, ex);
    }
}

static void try_handle_interrupt(rv_cpu_t* cpu){

    // Effective mip includes the external SEIP
    // Full explanation RISC-V Privileged spec section 3.1.9 Machine Interrupt Registers (mip and mie)
    uint32_t mip = cpu->csr.mip | (cpu->csr.external_SEIP ? rv_csr_sei_mask : 0);

    // no interrupt pending
    if(mip == 0) return;

    // PRIORITY: MEI, MSI, MTI, SEI, SSI, STI
    #define trap_if_set(cpu, mask, interrupt, trap_func)    \
        if(mask & RV_EXCEPTION_MASK(interrupt)){            \
            trap_func(cpu, interrupt);                      \
            return;                                         \
        }

    // TRAP to M-mode
    // ((priv_mode == M && MIE) || (priv_mode < M)) && MIP[i] && MIE[i] && !MIDELEG[i]

    bool can_trap_to_M = (cpu->priv_mode == rv_mmode && rv_csr_mstatus_mie(cpu)) || (cpu->priv_mode < rv_mmode);

    if(can_trap_to_M) {
        uint32_t m_mode_active_interrupt_mask = mip & cpu->csr.mie & ~cpu->csr.mideleg;
        
        trap_if_set(cpu, m_mode_active_interrupt_mask, rv_exc_machine_external_interrupt, m_trap);
        trap_if_set(cpu, m_mode_active_interrupt_mask, rv_exc_machine_software_interrupt, m_trap);
        trap_if_set(cpu, m_mode_active_interrupt_mask, rv_exc_machine_timer_interrupt, m_trap);
        trap_if_set(cpu, m_mode_active_interrupt_mask, rv_exc_supervisor_external_interrupt, m_trap);
        trap_if_set(cpu, m_mode_active_interrupt_mask, rv_exc_supervisor_software_interrupt, m_trap);
        trap_if_set(cpu, m_mode_active_interrupt_mask, rv_exc_supervisor_timer_interrupt, m_trap);
    }

    // TRAP to S-mode
    // ((priv_mode == S && SIE) || (priv_mode < M)) && SIP[i] && SIE[i]

    bool can_trap_to_S = (cpu->priv_mode == rv_smode && rv_csr_sstatus_sie(cpu)) || (cpu->priv_mode < rv_smode);
    if(can_trap_to_S) {
        // mask to only account S mode interrupts
        uint32_t s_mode_active_interrupt_mask = mip & cpu->csr.mie & rv_csr_si_mask;

        // M-interrupts can be here theoretically by spec, but we don't allow the delegation of M interrupts in msim (which is allowed in spec)
        trap_if_set(cpu, s_mode_active_interrupt_mask, rv_exc_supervisor_external_interrupt, s_trap);
        trap_if_set(cpu, s_mode_active_interrupt_mask, rv_exc_supervisor_software_interrupt, s_trap);
        trap_if_set(cpu, s_mode_active_interrupt_mask, rv_exc_supervisor_timer_interrupt, s_trap);
    }

    #undef trap_if_set
}

static void account_hmp(rv_cpu_t* cpu, int i){
    ASSERT((i >= 0 && i < 29));
    
    uint32_t mask = (1 << (i + 3));
    bool inhibited = cpu->csr.mcountinhibit & mask;
    
    if(inhibited) return;

    csr_hpm_event_t event = cpu->csr.hpmevents[i];

    switch(event){
        case(hpm_u_cycles):{
            if(cpu->priv_mode == rv_umode){
                cpu->csr.hpmcounters[i]++;
            }
            break;
        }
        case(hpm_s_cycles):{
            if(cpu->priv_mode == rv_smode){
                cpu->csr.hpmcounters[i]++;
            }
            break;
        }
        case(hpm_m_cycles):{
            if(cpu->priv_mode == rv_mmode){
                cpu->csr.hpmcounters[i]++;
            }
            break;
        }
        case(hpm_w_cycles):{
            if(cpu->stdby){
                cpu->csr.hpmcounters[i]++;
            }
            break;
        }
        default:
            break;
    }
}

static void raise_timer_interrupts(rv_cpu_t* cpu){
    // raise or clear scyclecmp STIP
    if(((uint32_t)cpu->csr.cycle) >= cpu->csr.scyclecmp) {
        // Set supervisor timer interrupt pending
        cpu->csr.mip |= rv_csr_sti_mask;
    }
    else {
        // Clear STIP
        cpu->csr.mip &= ~rv_csr_sti_mask;
    }

    // raise or clear mtimecmp MTIP
    if(cpu->csr.mtime >= cpu->csr.mtimecmp){
        // Set MTIP
        cpu->csr.mip |= rv_csr_mti_mask;
    }
    else {
        // Clear MTIP
        cpu->csr.mip &= ~rv_csr_mti_mask;
    }
}


static void account(rv_cpu_t* cpu, bool exception_raised){
    if(!(cpu->csr.mcountinhibit & 0b001))
        cpu->csr.cycle++;

    uint64_t current_tick_time = current_timestamp();
    cpu->csr.mtime += (current_tick_time - cpu->csr.last_tick_time);
    cpu->csr.last_tick_time = current_tick_time;

    if(!(cpu->csr.mcountinhibit & 0b100) && !exception_raised && !cpu->stdby)
        cpu->csr.instret++;

    for(int i = 0; i < 29; ++i){
        account_hmp(cpu, i);
    }

    raise_timer_interrupts(cpu);
}

static rv_exc_t execute(rv_cpu_t *cpu) {
    ptr36_t phys;
    rv_exc_t ex = rv_convert_addr(cpu, cpu->pc, &phys, false, true, true);
 
    if(ex != rv_exc_none){
        alert("Fetching from unconvertable address!");;
        if(machine_trace) {
            rv_idump(cpu, cpu->pc, (rv_instr_t)0U);
        }
        return ex;
    }

    rv_instr_func_t instr_func = fetch_instr(cpu, phys);
    rv_instr_t instr_data = (rv_instr_t)physmem_read32(cpu->csr.mhartid, phys, true);
    
    if(machine_trace) {
        rv_idump(cpu, cpu->pc, instr_data);
    }

    ex = instr_func(cpu, instr_data);

    if(ex == rv_exc_illegal_instruction){
        cpu->csr.tval_next = instr_data.val;
    }

    return ex;
}

void rv_cpu_step(rv_cpu_t *cpu){
    ASSERT(cpu != NULL);

    rv_exc_t ex = rv_exc_none;

    if(!cpu->stdby) {
        ex = execute(cpu);
    }

    account(cpu, ex != rv_exc_none);
 
    if(ex != rv_exc_none){
        handle_exception(cpu, ex);
    }
    else {
        // If any interrupts are pending, handle them
        try_handle_interrupt(cpu);
    }

    if(!cpu->stdby){
        cpu->pc = cpu->pc_next;
        cpu->pc_next = cpu->pc + 4;
    }

    // x0 is always 0
    cpu->regs[0] = 0;
    cpu->csr.tval_next = 0;
}

bool rv_sc_access(rv_cpu_t *cpu, ptr36_t phys){
    ASSERT(cpu != NULL);
    // We align down because of writes that are shorter than 4 B
    // As long as all writes are aligned, and 32 bits at max, this works
    bool hit = cpu->reserved_addr == ALIGN_DOWN(phys, 4);
    if(hit) {
        cpu->reserved_valid = false;
    }
    return hit;
}

/* Interrupts
 * This is supposed to be used with devices and interprocessor communication,
 * devices should raise a Machine/Supervisor External Interrupt,
 * but interprocessor interrupts should be Machine/Supervisor Software Interrupts.
 * So we use the argument no to differentiate based on the exception code (see rv_exc_t definiton)
 */

void rv_interrupt_up(rv_cpu_t *cpu, unsigned int no){
    ASSERT(cpu != NULL);

    // Edge case, where we don't want to set SEIP, because SEIP is writable from M mode
    // Full explanation RISC-V Privileged spec section 3.1.9 Machine Interrupt Registers (mip and mie)
    if(no == RV_INTERRUPT_NO(rv_exc_supervisor_external_interrupt)){
        cpu->csr.external_SEIP = true;
        return;
    }

    // default to MEI if no is invalid
    if( no != RV_INTERRUPT_NO(rv_exc_machine_software_interrupt) &&
        no != RV_INTERRUPT_NO(rv_exc_supervisor_software_interrupt) &&
        no != RV_INTERRUPT_NO(rv_exc_machine_external_interrupt)
    ){
        no = RV_INTERRUPT_NO(rv_exc_machine_external_interrupt);
    }

    uint32_t mask = RV_EXCEPTION_MASK(no);

    cpu->csr.mip |= mask;
}

void rv_interrupt_down(rv_cpu_t *cpu, unsigned int no){
    ASSERT(cpu != NULL);
    //! for simplicity just clears the bit
    //! if this interrupt could be raised by different means,
    //! this would not work!

    // Edge case, where we don't want to clear SEIP, because SEIP is writable from M mode
    // Full explanation RISC-V Privileged spec section 3.1.9 Machine Interrupt Registers (mip and mie)
    if(no == RV_INTERRUPT_NO(rv_exc_supervisor_external_interrupt)){
        cpu->csr.external_SEIP = false;
        return;
    }
    
    // default to MEI if no is invalid
    if( no != RV_INTERRUPT_NO(rv_exc_machine_software_interrupt) &&
        no != RV_INTERRUPT_NO(rv_exc_supervisor_software_interrupt) &&
        no != RV_INTERRUPT_NO(rv_exc_machine_external_interrupt)
    ){
        no = RV_INTERRUPT_NO(rv_exc_machine_external_interrupt);
    }

    uint32_t mask = RV_EXCEPTION_MASK(no);

    cpu->csr.mip &= ~mask;
}