/*
 * Copyright (c) 2000-2003 Viliam Holub
 * All rights reserved.
 *
 * Distributed under the terms of GPL.
 *
 */

#ifndef MACHINE_H_
#define MACHINE_H_


#include "../mtypes.h"
#include "../list.h"
#include "../main.h"
#include "../cpu/processor.h"
#include "../parser.h"
#include "../endi.h"
#include "device.h"

#define DEFAULT_MEMORY_VALUE32 0xffffffff

enum TMemoryType_enum {
	mtRWM,
	mtROM,
	mtEXC
};

typedef struct mem_element_s {
	/* Memory region type */
	bool writable;
	
	/* Basic specification - position and size */
	uint32_t start;
	uint32_t size;
	
	/* Block of memory */
	unsigned char *mem;
	
	/* Next element in the list */
	struct mem_element_s *next;
} mem_element_s;

typedef struct llist {
	processor_t *p;
	struct llist *next;
} llist_t;

/**< Common variables */
extern bool totrace;
extern bool tohalt;
extern int procno;

extern char *config_file;

/**< Debug features */
extern char **cp0name;
extern char **cp1name;
extern char **cp2name;
extern char **cp3name;
extern bool change;
extern bool interactive;
extern bool errors;
extern bool script_stat;

extern bool remote_gdb;
extern int remote_gdb_port;
extern bool remote_gdb_conn;
extern bool remote_gdb_listen;
extern bool remote_gdb_step;

extern bool version;

extern uint32_t stepping;
extern mem_element_s *memlist;
extern llist_t *ll_list;

extern bool tobreak;
extern bool reenter;

extern void input_back(void);


/*
 * Basic machine functions
 */

extern void init_machine(void);
extern void done_machine(void);
extern void go_machine(void);
extern void machine_step(void);

/** ll and sc control */
extern void register_ll(processor_t *pr);
extern void unregister_ll(processor_t *pr);

/** Memory access */
extern bool mem_write(processor_t *pr, uint32_t addr, uint32_t val,
    size_t size, bool protected_write);
extern uint32_t mem_read(processor_t *pr, uint32_t addr, size_t size, 
    bool protected_read);

/** Memory control */
extern void mem_link(mem_element_s *e);
extern void mem_unlink(mem_element_s *e);

#endif /* MACHINE_H_ */
