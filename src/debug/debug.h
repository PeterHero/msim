/*
 * Copyright (c) 2002-2005 Viliam Holub
 * All rights reserved.
 *
 * Distributed under the terms of GPL.
 *
 *
 *  Useful debugging features
 *
 */

#ifndef DEBUG_H_
#define DEBUG_H_

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include "../cpu/instr.h"
#include "../cpu/r4000.h"
#include "../device/device.h"

extern bool dump_addr;
extern bool dump_instr;
extern bool dump_inum;

extern void reg_view(cpu_t *cpu);
extern void tlb_dump(cpu_t *cpu);
extern void cp0_dump(cpu_t *cpu, unsigned int reg);

extern void iview_phys(ptr36_t addr, instr_info_t *ii, char *regch);
extern void iview(cpu_t *cpu, ptr32_t addr, instr_info_t *ii, char *regch);

extern char *modified_regs_dump(cpu_t *cpu);

extern void dbg_print_devices(device_filter_t filter);
extern void dbg_print_devices_stat(device_filter_t filter);

extern void dbg_print_device_info(device_t *dev);
extern void dbg_print_device_stat(device_t *dev);

#endif
