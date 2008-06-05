/*
 * Simple printer device
 * Copyright (c) 2002-2004 Viliam Holub
 */

#ifdef HAVE_CONFIG_H
#	include "../config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "dprinter.h"

#include "mtypes.h"
#include "device.h"
#include "fault.h"
#include "output.h"
#include "utils.h"

/* Registers */
#define REGISTER_CHAR	0	/* Outpu character */
#define REGISTER_LIMIT	4	/* Size of the register block */


/*
 * Device commands
 */

static bool dprinter_init( parm_link_s *parm, device_s *dev);
static bool dprinter_info( parm_link_s *parm, device_s *dev);
static bool dprinter_stat( parm_link_s *parm, device_s *dev);
static bool dprinter_redir( parm_link_s *parm, device_s *dev);
static bool dprinter_stdout( parm_link_s *parm, device_s *dev);

cmd_s printer_cmds[] =
{
	{ "init", (cmd_f)dprinter_init,
		DEFAULT,
		DEFAULT,
		"Inicialization",
		"Inicialization",
		REQ STR "name/printer name" NEXT
		REQ INT "addr/register address" END},
	{ "help", (cmd_f)dev_generic_help,
		DEFAULT,
		DEFAULT,
		"Displays this help text",
		"Displays this help text",
		OPT STR "cmd/command name" END},
	{ "info", (cmd_f)dprinter_info,
		DEFAULT,
		DEFAULT,
		"Displays printer state and configuration",
		"Displays printer state and configuration",
		NOCMD},
	{ "stat", (cmd_f)dprinter_stat,
		DEFAULT,
		DEFAULT,
		"Displays printer statistics",
		"Displays printer statistics",
		NOCMD},
	{ "redir", (cmd_f)dprinter_redir,
		DEFAULT,
		DEFAULT,
		"Redirect output to the specified file",
		"Redirect output to the specified file",
		REQ STR "filename/output file name" END},
	{ "stdout", (cmd_f)dprinter_stdout,
		DEFAULT,
		DEFAULT,
		"Redirect output to the standard output",
		"Redirect output to the standard output",
		NOCMD},
	LAST_CMD
};

const char id_printer[] = "dprinter";

static void printer_done( device_s *d);
static void printer_step4( device_s *d);
static void printer_write( device_s *d, uint32_t addr, uint32_t val);

device_type_s DPrinter =
{
	/* Type name and description */
	.name = id_printer,
	.brief = "Printer simulation",
	.full = "Printer device represents a simple character output device. Via "
		"memory-mapped register system can write character to the "
		"specified output like screen, file or another terminal.",
	
	/* Functions */
	.done = printer_done,
	.step4 = printer_step4,
	.write = printer_write,
	
	/* Commands */
	printer_cmds
};


struct printer_data_s
{
	uint32_t addr;		/* Printer register address */
	bool flush;		/* Flush-the-output flag (flush is necessary and it slow) */
	
	FILE *output_file;	/* Output file */
	
	uint64_t count;		/* Number of output characters */
};
typedef struct printer_data_s printer_data_s;



/** Init command implementation
 */
static bool
dprinter_init( parm_link_s *parm, device_s *dev)
{
	printer_data_s *pd;
	
	/* The printer structure allocation */
	pd = XXMALLOC( printer_data_s);
	dev->data = pd;
	
	/* Inicialization */
	parm_next( &parm);
	pd->addr = parm_next_int( &parm);
	pd->flush = false;
	pd->output_file = stdout;
	
	pd->count = 0;
	
	/* Check address alignment */
	if (!addr_word_aligned( pd->addr))
	{
		mprintf( "Printer address must be in the 4-byte boundary.\n");
		free( pd);
		return false;
	}
	
	return true;
}


/** Redir command implementation
 */
static bool
dprinter_redir( parm_link_s *parm, device_s *dev)
{
	printer_data_s *pd = dev->data;
	const char *const filename = parm_str( parm);
	FILE *new_file;
	
	/* Open the file */
	new_file = fopen( filename, "w");
	if (!new_file)
	{
		io_error( filename);
		mprintf( txt_file_open_err);
		
		return false;
	}
	
	/* Close old output file */
	if (pd->output_file != stdout)
	{
		if (!fclose( pd->output_file))
		{
			io_error( 0);
			error( txt_file_close_err);
			return false;
		}
	}
	
	/* Set new output file */
	pd->output_file = new_file;
	return true;
}


/** Stdout command implementation.
 */
static bool
dprinter_stdout( parm_link_s *parm, device_s *dev)
{
	printer_data_s *pd = dev->data;

	/* Close old ouput file if it is not stdout already */
	if (pd->output_file != stdout)
	{
		if (!fclose( pd->output_file))
		{
			io_error( 0);
			return txt_file_close_err;
		}

		pd->output_file = stdout;
	}
	
	return true;
}


/** Info command implementation.
 */
static bool
dprinter_info( parm_link_s *parm, device_s *dev)
{
	printer_data_s *pd = dev->data;
	mprintf_btag( INFO_SPC, "address:0x%08x\n", pd->addr);
	return true;
}


/** Stat command implementation.
 */
static bool
dprinter_stat( parm_link_s *parm, device_s *dev)
{
	printer_data_s *pd = dev->data;
	mprintf_btag( INFO_SPC, "count:%lld\n", pd->count);
	return true;
}




/*
 *
 * Implicit commands
 */

/** Cleans up the device.
 */
static void
printer_done( device_s *d)

{
	printer_data_s *pd = d->data;

	/* Close output file if ti is not stdout. */
	if (pd->output_file != stdout)
	{
		if (!fclose( pd->output_file))
		{
			io_error( 0);
			error( txt_file_close_err);
		}
	}

	XFREE( d->name);
	XFREE( d->data);
}


/** One step4 implementation.
 */
static void
printer_step4( device_s *d)
{
	printer_data_s *pd = d->data;
	
	/* Check if flush is necesary. */
	if (pd->flush)
	{
		pd->flush = false;
		fflush( pd->output_file);
	}
}


/** Write command implementation.
 */
static void
printer_write( device_s *d, uint32_t addr, uint32_t val)
{
	printer_data_s *pd = d->data;

	if (addr == pd->addr +REGISTER_CHAR) 

	{
		fprintf( pd->output_file, "%c", (char) val);
		pd->flush = true;
		pd->count++;
	}
}
