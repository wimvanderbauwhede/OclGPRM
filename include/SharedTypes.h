#ifndef _SHAREDTYPES_H_
#define _SHAREDTYPES_H_

#include "SharedMacros.h"

/* Contains type definitions that are used by both the host and kernel programs. */

/* Replace the types for use in host program source. */
#ifdef _IN_HOST
#define uchar cl_uchar
#define ushort cl_ushort
#define uint cl_uint
#define uint2 cl_uint2
#define ulong cl_ulong
#endif

/* A packet is 2x32bit words.
   Packet contents:   [subtask, arg_pos, source, type]:32bits [payload]:32bits
   type:2bits         00000000000000000000000000000011  The packet type.
   source:8bits       00000000000000000000001111111100  Who sent the packet?
   arg_pos:4bits      00000000000000000011110000000000  The argument position.
   subtask:10bits     00000000111111111100000000000000  The subtask record.
   payload_type:1bit  00000001000000000000000000000000  The payload type. */
#ifndef _IN_HOST
typedef uint2 packet;
#endif

/* Bytecode consists of 64bit words. */
typedef ulong bytecode;

/* A subtask table record. */
/* WV  
 added nargs and code_address 
 changed order a bit and added some more padding
 now it's aligned to 40 32-bit words or 80 16-bit words
 but we have 1 16-bit word per record for the address stack
 so we should actually use 39 words and have a 32-bit address stack
*/
typedef struct SubtaskRecord {
// 32
  uint service_id;		            // [32bits] Opcode.
  uint code_addr;					// [32bits]: address of the original bytecode
  bytecode args[MAX_BYTECODE_SZ-1]; // [64bits] * 15 Pointers to data or constants.
// 4
  uchar nargs;						// [8bits]: number of arguments
  uchar arg_status[MAX_BYTECODE_SZ-1]; // [8bits] * 15 The status of the arguments.
// 1
  uchar subt_status;				// [8bits]: [4bits]  Subtask status and [4bits] number of args absent.  
  uchar return_to;					// [8bits]  The service core to return to.
  ushort return_as;					// [16bits]: [8bits]  Subtask record address and [8bits] argument position.
// 2
  uint padding32[2];				// [32bits] * 3  what it says
//  ulong padding64[12];				// [64bits] * 12
} SubtaskRecord;

/* The subtask table with associated available record stack. */
typedef struct SubtaskTable {
  SubtaskRecord recs[SUBT_SIZE+1];      // The subtask table records. one too many, for padding
  uint av_recs[SUBT_SIZE+1]; // Stack of available records. // WV: ugly. The 1 is the stack pointer
} SubtaskTable;

#endif 
