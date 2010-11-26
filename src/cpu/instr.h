/*
 * Copyright (c) 2001-2003 Viliam Holub
 * All rights reserved.
 *
 * Distributed under the terms of GPL.
 *
 *
 *  Instruction decoding
 *
 */

#ifndef INSTR_H_
#define INSTR_H_

#include <stdint.h>
#include <inttypes.h>
#include "../main.h"

/** Opcode numbers
 *
 * Warning: Do NOT change the order
 *
 */
typedef enum {
	/* Special names for blocks of instructions */
	opcSPECIAL,
	opcBCOND,
	opcSPECIAL2,
	
	/* Real instructions */
	opcADD,
	opcADDI,
	opcADDIU,
	opcADDU,
	opcAND,
	opcANDI,
	
	opcBC0F,
	opcBC1F,
	opcBC2F,
	opcBC3F,
	opcBC0FL,
	opcBC1FL,
	opcBC2FL,
	opcBC3FL,
	opcBC0T,
	opcBC1T,
	opcBC2T,
	opcBC3T,
	opcBC0TL,
	opcBC1TL,
	opcBC2TL,
	opcBC3TL,
	
	opcBEQ,
	opcBEQL,
	opcBGEZ,
	opcBGEZAL,
	opcBGEZALL,
	opcBGEZL,
	opcBGTZ,
	opcBGTZL,
	opcBLEZ,
	opcBLEZL,
	opcBLTZ,
	opcBLTZAL,
	opcBLTZALL,
	opcBLTZL,
	opcBNE,
	opcBNEL,
	opcBREAK,
	
	opcCACHE,
	opcCFC0,
	opcCFC1,
	opcCFC2,
	opcCFC3,
	opcCLO,
	opcCLZ,
	opcCOP0,
	opcCOP1,
	opcCOP2,
	opcCOP3,
	opcCTC0,
	opcCTC1,
	opcCTC2,
	opcCTC3,
	
	opcDADD,
	opcDADDI,
	opcDADDIU,
	opcDADDU,
	opcDDIV,
	opcDDIVU,
	opcDIV,
	opcDIVU,
	opcDMFC0,
	opcDMFC1,
	opcDMFC2,
	opcDMFC3,
	opcDMTC0,
	opcDMTC1,
	opcDMTC2,
	opcDMTC3,
	opcDMULT,
	opcDMULTU,
	opcDSLL,
	opcDSLLV,
	opcDSLL32,
	opcDSRA,
	opcDSRAV,
	opcDSRA32,
	opcDSRL,
	opcDSRLV,
	opcDSRL32,
	opcDSUB,
	opcDSUBU,
	
	opcERET,
	
	opcJ,
	opcJAL,
	opcJALR,
	opcJR,
	
	opcLB,
	opcLBU,
	opcLD,
	opcLDC1,
	opcLDC2,
	opcLDL,
	opcLDR,
	opcLH,
	opcLHU,
	opcLL,
	opcLLD,
	opcLUI,
	opcLW,
	opcLWC1,
	opcLWC2,
	opcLWL,
	opcLWR,
	opcLWU,
	
	opcMADD,
	opcMADDU,
	opcMFC0,
	opcMFC1,
	opcMFC2,
	opcMFC3,
	opcMFHI,
	opcMFLO,
	opcMOVN,
	opcMOVZ,
	opcMSUB,
	opcMSUBU,
	opcMTC0,
	opcMTC1,
	opcMTC2,
	opcMTC3,
	opcMTHI,
	opcMTLO,
	opcMUL,
	opcMULT,
	opcMULTU,
	
	opcNOR,
	
	opcOR,
	opcORI,
	
	opcSB,
	opcSC,
	opcSCD,
	opcSD,
	opcSDC1,
	opcSDC2,
	opcSDL,
	opcSDR,
	opcSH,
	opcSLL,
	opcSLLV,
	opcSLT,
	opcSLTI,
	opcSLTIU,
	opcSLTU,
	opcSRA,
	opcSRAV,
	opcSRL,
	opcSRLV,
	opcSUB,
	opcSUBU,
	opcSW,
	opcSWC1,
	opcSWC2,
	opcSWL,
	opcSWR,
	opcSYNC,
	opcSYSCALL,
	
	opcTEQ,
	opcTEQI,
	opcTGE,
	opcTGEI,
	opcTGEIU,
	opcTGEU,
	opcTLBP,
	opcTLBR,
	opcTLBWI,
	opcTLBWR,
	opcTLT,
	opcTLTI,
	opcTLTIU,
	opcTLTU,
	opcTNE,
	opcTNEI,
	
	opcWAIT,
	
	opcXOR,
	opcXORI,
	
	opcNOP,
	
	opcUNIMP,
	
	opcRES,
	opcQRES,
	
	/* Debugging features */
	opcDVAL,
	opcDTRC,
	opcDTRO,
	opcDRV,
	opcDHLT,
	opcDINT,
	
	opcIllegal,
	
	/* For decoding */
	opcBC,
	opcC0
} instr_opcode_t;

/** Instruction formats */
typedef enum {
	ifX,       /**< undefined */
	ifNONE,    /**< no parameters */
	ifERR,     /**< invalid */
	ifR4,      /**< not implemented */
	ifIMM,     /**< immediate */
	ifIMMS,    /**< immediate signed */
	ifIMMU,    /**< immediate unsigned */
	ifIMMUX,   /**< immediate unsigned, hex 4 */
	
	ifJ,       /**< jump */
	ifREG,     /**< register */
	ifOFF,     /**< offset */
	ifCND,     /**< cond */
	ifRO,      /**< reg + off */
	ifTD,      /**< rt, rd */
	ifTDX0,    /**< rt, rd as number cp0 */
	ifTDX1,    /**< rt, rd as number cp1 */
	ifTDX2,    /**< rt, rd as number cp2 */
	ifTDX3,    /**< rt, rd as number cp3 */
	ifOP,
	ifST,
	ifDS,
	ifS,
	ifTOB,
	ifRIW,
	ifD,
	ifSI,
	ifSIW,
	ifDTS,
	ifSYSCALL  /**< syscall */
} instr_form_basic_t;

typedef struct {
	instr_opcode_t opcode;
	instr_form_basic_t format;
} instr_form_t;

/** Various mask and shift settings */
#define TARGET_MASK   UINT32_C(0x03ffffff)
#define TARGET_SHIFT  2
#define TARGET_COMB   UINT64_C(0xfffffffff0000000)

#define FUNCTION_MASK  UINT32_C(0x0000003f)
#define IMM_MASK       UINT32_C(0x0000ffff)

#define SA_MASK   UINT32_C(0x000007c0)
#define SA_SHIFT  6
#define RD_MASK   UINT32_C(0x0000f800)
#define RD_SHIFT  11
#define RT_MASK   UINT32_C(0x001f0000)
#define RT_SHIFT  16
#define RS_MASK   UINT32_C(0x03e00000)
#define RS_SHIFT  21
#define OP_MASK   UINT32_C(0xfc000000)
#define OP_SHIFT  26

#define CO_MASK      UINT32_C(0x0000003f)
#define BCOND_SHIFT  16
#define BCOND_MASK   UINT32_C(0x0000001f)

typedef struct {
	/* Instruction */
	uint32_t icode;
	instr_opcode_t opcode;
	
	/* Function */
	uint8_t function;
	
	/* Registers */
	uint8_t rs;
	uint8_t rt;
	uint8_t rd;
	uint8_t sa;
	
	/* Others */
	uint32_t target;
	uint16_t imm;
} instr_info_t;

typedef struct {
	char *acronym;
	instr_form_basic_t itype;
} instr_text_t;

extern instr_text_t instr_names_acronym[];

/** Register and coprocessor names */
extern char *reg_name[][32];
extern char *cp0_name[][32];
extern char *cp1_name[][32];
extern char *cp2_name[][32];
extern char *cp3_name[][32];

/** Convert opcode to instruction description */
extern void decode_instr(instr_info_t *ii);

#endif
