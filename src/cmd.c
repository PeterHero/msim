/*
 * Copyright (c) 2002-2007 Viliam Holub
 * All rights reserved.
 *
 * Distributed under the terms of GPL.
 *
 *
 *  Reading and executing commands
 *
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <inttypes.h>
#include "debug/debug.h"
#include "debug/breakpoint.h"
#include "device/device.h"
#include "device/machine.h"
#include "io/output.h"
#include "main.h"
#include "cmd.h"
#include "check.h"
#include "fault.h"
#include "utils.h"
#include "env.h"

static cmd_t system_cmds[];

/** Add command implementation
 *
 * Add memory, devices, etc.
 *
 * Device name should not be the same as
 * a command name and there should not be
 * another device with a same name.
 *
 */
static bool system_add(token_t *parm, void *data)
{
	const char *name = parm_str(parm);
	parm_next(&parm);
	const char *instance = parm_str(parm);
	
	/*
	 * Check for conflicts between
	 * the device name and a command name
	 */
	
	if (cmd_find(name, system_cmds, NULL) == CMP_HIT) {
		mprintf("Device name \"%s\" is in conflict with a command name.\n",
		    name);
		return false;
	}
	
	if (dev_by_name(name)) {
		mprintf("Device name \"%s\" already added.\n", name);
		return false;
	}
	
	/* Allocate device */
	device_t *dev = alloc_device(name, instance);
	if (!dev)
		return false;
	
	/* Call device inicialization */
	if (!cmd_run_by_name("init", parm, dev->type->cmds, dev)) {
		free_device(dev);
		return false;
	}
	
	/* Add into the device list */
	add_device(dev);
	return true;
}

/** Continue command implementation
 *
 * Continue simulation.
 *
 */
static bool system_continue(token_t *parm, void *data)
{
	interactive = false;
	return true;
}

/* Step command implementation
 *
 * Execute a given count of instructions.
 *
 */
static bool system_step(token_t *parm, void *data)
{
	switch (parm_type(parm)) {
	case tt_end:
		stepping = 1;
		interactive = false;
		break;
	case tt_uint:
		stepping = parm_uint(parm);
		interactive = false;
		break;
	default:
		return false;
	}
	
	return true;
}

/** Set command implementation
 *
 * Set configuration variable.
 *
 */
static bool system_set(token_t *parm, void *data)
{
	return env_cmd_set(parm);
}

/** Unset command implementation
 *
 * Unset configuration variable.
 *
 */
static bool system_unset(token_t *parm, void *data)
{
	return env_cmd_unset(parm);
}

/** Dump instructions command implementation
 *
 * Dump instructions.
 *
 */
static bool system_dumpins(token_t *parm, void *data)
{
	ptr_t addr = ALIGN_DOWN(parm_uint(parm), 4);
	uint32_t cnt = parm_next_uint(&parm);
	
	for (; cnt > 0; addr += 4, cnt--) {
		instr_info_t ii;
		ii.icode = mem_read(NULL, addr, BITS_32, false);
		decode_instr(&ii);
		iview(NULL, addr, &ii, 0);
	}
	
	return true;
}

/** Dump devices command implementation
 *
 * Dump configured devices.
 *
 */
static bool system_dumpdev(token_t *parm, void *data)
{
	dbg_print_devices(DEVICE_FILTER_ALL);
	return true;
}

/** Dump physical memory blocks command implementation
 *
 * Dump configured physical memory blocks.
 *
 */
static bool system_dumpphys(token_t *parm, void *data)
{
	dbg_print_devices(DEVICE_FILTER_MEMORY);
	return true;
}

/** Break command implementation
 *
 */
static bool system_break(token_t *parm, void *data)
{
	ptr_t addr = parm_uint(parm);
	parm_next(&parm);
	uint32_t size = parm_uint(parm);
	parm_next(&parm);
	const char *rw = parm_str(parm);
	
	access_filter_t access_flags = ACCESS_FILTER_NONE;
	
	if (strchr(rw, 'r') != NULL)
		access_flags |= ACCESS_READ;
	
	if (strchr(rw, 'w') != NULL)
		access_flags |= ACCESS_WRITE;
	
	if (access_flags == ACCESS_FILTER_NONE) {
		mprintf("Read or write access must be specified.\n");
		return false;
	}
	
	memory_breakpoint_add(addr, size, access_flags,
	    BREAKPOINT_KIND_SIMULATOR);
	return true;
}

/** Dump breakpoints command implementation
 *
 */
static bool system_dumpbreak(token_t *parm, void *data)
{	
	memory_breakpoint_print_list();
	return true;
}

/** Remove breakpoint command implementation
 *
 */
static bool system_rembreak(token_t *parm, void *data)
{
	ptr_t addr = parm_uint(parm);
	
	if (!memory_breakpoint_remove(addr)) {
		mprintf("Unknown breakpoint.\n");
		return false;
	}
	
	return true;
}

/** Stat command implementation
 *
 * Print simulator statistics.
 *
 */
static bool system_stat(token_t *parm, void *data)
{
	dbg_print_devices_stat(DEVICE_FILTER_ALL);
	return true;
}

/** Dump memory command implementation
 *
 * Dump physical memory.
 *
 */
static bool system_dumpmem(token_t *parm, void *data)
{
	ptr_t addr = ALIGN_DOWN(parm_uint(parm), 4);
	uint32_t cnt = parm_next_uint(&parm);
	uint32_t i;
	
	for (i = 0; i < cnt; addr += 4, i++) {
		if ((i & 0x3) == 0)
			mprintf("  %#010" PRIx32 "    ", addr);
		
		uint32_t val = mem_read(NULL, addr, BITS_32, false);
		mprintf("%08" PRIx32 " ", val);
		
		if ((i & 0x3) == 3)
			mprintf("\n");
	}
	
	if (i != 0)
		mprintf("\n");
	
	return true;
}

/** Quit command implementation
 *
 * Quit MSIM immediately.
 *
 */
static bool system_quit(token_t *parm, void *data)
{
	interactive = false;
	tohalt = true;
	
	return true;
}

/** Echo command implementation
 *
 * Print the user text on the screen.
 *
 */
static bool system_echo(token_t *parm, void *data)
{
	while (parm_type(parm) != tt_end) {
		switch (parm_type(parm)) {
		case tt_str:
			mprintf("%s", parm_str(parm));
			break;
		case tt_uint:
			mprintf("%" PRIu32, parm_uint(parm));
			break;
		default:
			return false;
		}
		
		parm_next(&parm);
		if (parm_type(parm) != tt_end)
			mprintf(" ");
	}
	
	mprintf("\n");
	return true;
}

/** Help command implementation
 *
 * Print the help.
 *
 */
static bool system_help(token_t *parm, void *data)
{
	cmd_print_extended_help(system_cmds, parm);
	return true;
}


/** Interprets the command line.
 *
 * Line is terminated by '\0' or '\n'.
 *
 */
bool interpret(const char *str)
{
	/* Parse input */
	token_t *parm = parm_parse(str);
	
	if (parm_type(parm) == tt_end)
		return true;
	
	if (parm_type(parm) != tt_str) {
		mprintf("Command name expected.\n");
		return true;
	}
	
	const char *name = parm_str(parm);
	bool ret;
	device_t *dev = dev_by_name(name);
	
	parm_next(&parm);
	
	if (dev)
		/* Device command */
		ret = cmd_run_by_parm(parm, dev->type->cmds, dev);
	else
		/* System command */
		ret = cmd_run_by_parm(parm, system_cmds, NULL);
	
	parm_delete(parm);
	return ret;
}

/** Run initial script uploaded in the memory
 *
 */
static bool setup_apply(const char *buf)
{
	size_t lineno = 1;
	
	while ((*buf) && (!tohalt)) {
		if (!interpret(buf))
			die(ERR_INIT, 0);
		
		lineno++;
		set_lineno(lineno);
		
		/* Move to the next line */
		while ((*buf) && (*buf != '\n'))
			buf++;
		
		if (*buf == '\n')
			buf++;
	}
	
	return true;
}

/** Interpret configuration file
 *
 */
void script(void)
{
	if (!config_file) {
		/* Check for variable MSIMCONF */
		config_file = getenv("MSIMCONF");
		if (!config_file)
			config_file = "msim.conf";
	}
	
	/* Open configuration file */
	FILE *file = fopen(config_file, "r");
	if (file == NULL) {
		if (errno == ENOENT)
			mprintf("Configuration file \"%s\" not found, skipping.\n",
			    config_file);
		else
			io_die(ERR_IO, config_file);
		
		interactive = true;
		return;
	}
	
	set_script(config_file);
	
	string_t str;
	string_init(&str);
	string_fread(&str, file);
	
	safe_fclose(file, config_file);
	setup_apply(str.str);
	unset_script();
	
	string_done(&str);
}

/** Generate a list of device types
 *
 */
static char *generator_devtype(token_t *parm, const void *data,
    unsigned int level)
{
	PRE(parm != NULL);
	PRE((parm_type(parm) == tt_str) || (parm_type(parm) == tt_end));
	
	const char *str;
	static uint32_t last_device_order = 0;
	
	if (level == 0)
		last_device_order = 0;
	
	if (parm_type(parm) == tt_str)
		str = dev_type_by_partial_name(parm_str(parm), &last_device_order);
	else
		str = dev_type_by_partial_name("", &last_device_order);
	
	return str ? safe_strdup(str) : NULL;
}

/** Generate a list of installed device names
 *
 */
static char *generator_devname(token_t *parm, const void *data,
    unsigned int level)
{
	PRE(parm != NULL);
	PRE((parm_type(parm) == tt_str) || (parm_type(parm) == tt_end));
	
	const char *str;
	static device_t *dev;
	
	if (level == 0)
		dev = NULL;
	
	if (parm_type(parm) == tt_str)
		str = dev_by_partial_name(parm_str(parm), &dev);
	else
		str = dev_by_partial_name("", &dev);
	
	return str ? safe_strdup(str) : NULL;
}

/** Generate a list of commands and device names
 *
 * @param unused_data Always NULL.
 *
 */
static char *generator_system_cmds_and_device_names(token_t *parm,
    const void *unused_data, unsigned int level)
{
	const char *str = NULL;
	static enum {
		command_name,
		device_name
	} gen_type;
	
	if (level == 0)
		gen_type = command_name;
	
	if (gen_type == command_name) {
		str = generator_cmd(parm, system_cmds + 1, level);
		if (!str) {
			gen_type = device_name;
			level = 0;
		}
	}
	
	if (gen_type == device_name)
		str = generator_devname(parm, NULL, level);
	
	return str ? safe_strdup(str) : NULL;
}

/** Add command find generator
 *
 */
static gen_t system_add_find_generator(token_t **parm, const cmd_t *cmd,
    const void **data)
{
	PRE(parm != NULL, data != NULL, generator != NULL, cmd != NULL);
	PRE(*parm != NULL, *data == NULL);
	
	uint32_t first_device_order = 0;
	if ((parm_type(*parm) == tt_str)
	    && (dev_type_by_partial_name(parm_str(*parm), &first_device_order))
	    && (parm_last(*parm)))
		return generator_devtype;
	
	return NULL;
}

/** Set command find generator
 *
 */
static gen_t system_set_find_generator(token_t **parm, const cmd_t *cmd,
    const void **data)
{
	PRE(parm != NULL, data != NULL, generator != NULL, cmd != NULL);
	PRE(*parm != NULL, *data == NULL);
	
	if (parm_type(*parm) == tt_str) {
		unsigned int res;
		
		/* Look up for a variable name */
		if (parm_last(*parm))
			/* There is a completion possible */
			res = env_cnt_partial_varname(parm_str(*parm));
		else
			/* Exactly one match is allowed */
			res = 1;
		
		if (res == 1) {
			/* Variable fit by partial name */
			if (parm_last(*parm))
				return generator_env_name;
			
			var_type_t type;
			
			if (env_check_varname(parm_str(*parm), &type)) {
				parm_next(parm);
				if (parm_type(*parm) == tt_str) {
					if (!strcmp(parm_str(*parm), "=")) {
						/* Search for value */
						parm_next(parm);
						
						if ((parm_type(*parm) == tt_str) && (type == vt_bool)) {
							if (parm_last(*parm))
								return generator_env_booltype;
						}
					} else if (!parm_str(*parm)[0])
						return generator_equal_char;
				}
			}
		}
	} else  {
		/* Multiple hit */
		if (parm_last(*parm))
			return generator_env_name;
	}
	
	return NULL;
}

/** Unset command find generator
 *
 */
static gen_t system_unset_find_generator(token_t **parm, const cmd_t *cmd,
    const void **data)
{
	PRE(parm != NULL, data != NULL, generator != NULL, cmd != NULL);
	PRE(*parm != NULL, *data == NULL);
	
	if (parm_type(*parm) == tt_str) {
		/* Look up for a variable name */
		unsigned int res = env_cnt_partial_varname(parm_str(*parm));
		
		if (res > 0) {
			/* Partially fit by partial name */
			if (parm_last(*parm))
				return generator_bool_envname;
		}
	}
	
	return NULL;
}

/** Look up for the completion generator
 *
 * The command is specified by the first parameter.
 *
 */
gen_t find_completion_generator(token_t **parm, const void **data)
{
	PRE(parm != NULL, generator != NULL, data != NULL);
	PRE(*parm != NULL, *generator == NULL, *data == NULL);
	
	if (parm_last(*parm))
		return generator_system_cmds_and_device_names;
	
	/* Check if the first token is a string */
	if (parm_type(*parm) != tt_str)
		return NULL;
	
	char* user_text = parm_str(*parm);
	
	/* Find a command */
	const cmd_t *cmd;
	cmd_find_res_t res = cmd_find(user_text, system_cmds + 1, &cmd);
	
	switch (res) {
	case CMP_NO_HIT:
	case CMP_PARTIAL_HIT:
		/*
		 * Unknown command.
		 *
		 * If the user has written only the first part of the command,
		 * then use device names completion. If there is also a second
		 * part written and the first part leads just to one device name,
		 * use commands for that device as completion.
		 */
		if (parm_last(*parm))
			return generator_system_cmds_and_device_names;
		
		device_t *last_device = NULL;
		size_t devices_count = dev_count_by_partial_name(user_text,
		    &last_device);
		
		if (devices_count == 1) {
			parm_next(parm);
			return dev_find_generator(parm, last_device, data);
		}
		
		break;
	case CMP_MULTIPLE_HIT:
	case CMP_HIT:
		/* Default system generator */
		if (parm_last(*parm))
			return generator_system_cmds_and_device_names;
		
		if (res == CMP_MULTIPLE_HIT)
			/* Input error */
			break;
		
		/* Continue to the next generator if possible */
		if (cmd->find_gen)
			return cmd->find_gen(parm, cmd, data);
		
		break;
	}
	
	return NULL;
}

/** Main command structure
 *
 * All system commands are defined here.
 *
 */
static cmd_t system_cmds[] = {
	{
		"init",
		NULL,    /* hardwired */
		DEFAULT,
		DEFAULT,
		"",
		"",
		NOCMD
	},
	{
		"add",
		system_add,
		system_add_find_generator,
		DEFAULT,
		"Add a new device into the system",
		"Add a new device into the system",
		REQ STR "type/Device type" NEXT
		REQ STR "name/Device name" CONT
	},
	{
		"quit",
		system_quit,
		DEFAULT,
		DEFAULT,
		"Exit MSIM",
		"Exit MSIM",
		NOCMD
	},
	{
		"dumpmem",
		system_dumpmem,
		DEFAULT,
		DEFAULT,
		"Dump words from physical memory",
		"Dump words from physical memory",
		REQ INT "addr/memory address" NEXT
		REQ INT "cnt/count" END
	},
	{
		"dumpins",
		system_dumpins,
		DEFAULT,
		DEFAULT,
		"Dump instructions from physical memory",
		"Dump instructions from physical memory",
		REQ INT "addr/memory address" NEXT
		REQ INT "cnt/count" END
	},
	{
		"dumpdev",
		system_dumpdev,
		DEFAULT,
		DEFAULT,
		"Dump installed devices",
		"Dump installed devices",
		NOCMD
	},
	{
		"dumpphys",
		system_dumpphys,
		DEFAULT,
		DEFAULT,
		"Dump installed physical memory blocks",
		"Dump installed physical memory blocks",
		NOCMD
	},
	{
		"break",
		system_break,
		DEFAULT,
		DEFAULT,
		"Add a new physical memory breakpoint",
		"Add a new physical memory breakpoint",
		REQ INT "addr/memory address" NEXT
		REQ INT "cnt/count" END
		REQ STR "type/Read or write breakpoint" END
	},
	{
		"dumpbreak",
		system_dumpbreak,
		DEFAULT,
		DEFAULT,
		"Dump physical memory breakpoints",
		"Dump physical memory breakpoints",
		NOCMD
	},
	{
		"rembreak",
		system_rembreak,
		DEFAULT,
		DEFAULT,
		"Remove a physical memory breakpoint",
		"Remove a physical memory breakpoint",
		REQ INT "addr/memory address" END
	},
	{
		"stat",
		system_stat,
		DEFAULT,
		DEFAULT,
		"Print system statistics",
		"Print system statistics",
		NOCMD
	},
	{
		"echo",
		system_echo,
		DEFAULT,
		DEFAULT,
		"Print user message",
		"Print user message",
		OPT STR "text" END
	},
	{
		"continue",
		system_continue,
		DEFAULT,
		DEFAULT,
		"Continue simulation",
		"Continue simulation",
		NOCMD
	},
	{
		"step",
		system_step,
		DEFAULT,
		DEFAULT,
		"Simulate one or a specified number of instructions",
		"Simulate one or a specified number of instructions",
		OPT INT "cnt/intruction count" END
	},
	{
		"set",
		system_set,
		system_set_find_generator,
		DEFAULT,
		"Set enviroment variable",
		"Set enviroment variable",
		OPT STR "name/variable name" NEXT
		OPT CON "=" NEXT
		REQ VAR "val/value" END
	},
	{
		"unset",
		system_unset,
		system_unset_find_generator,
		DEFAULT,
		"Unset environment variable",
		"Unset environment variable",
		REQ STR "name/variable name" END
	},
	{
		"help",
		system_help,
		DEFAULT,
		DEFAULT,
		"Display help",
		"Display help",
		OPT STR "cmd/command name" END
	},
	LAST_CMD
};
