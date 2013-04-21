#ifndef _SHAREDMACROS_H_
#define _SHAREDMACROS_H_

/* Contains MACRO definitions that are used by both the host and kernel programs. */
#define RETURN_ADDRESS 255

// States
#define COMPLETE -1
#define READ 0
#define WRITE 1
// Sizes
#define MAX_BYTECODE_SZ 16
#define CODE_STORE_SIZE (256 * MAX_BYTECODE_SZ)
#define SUBT_SIZE 1024
// Various
#define DATA_INFO_OFFSET 1
#define NBYTES 8 // Number of bytes in a bytecode word.

/* Used to create, manipulate and access packet information. */
#define PKT_TYPE_MASK   0x3       // 00000000000000000000000000000011
#define PKT_TYPE_SHIFT  0

#define PKT_SRC_MASK    0x3FC     // 00000000000000000000001111111100
#define PKT_SRC_SHIFT   2

#define PKT_ARG_MASK    0x3C00    // 00000000000000000011110000000000
#define PKT_ARG_SHIFT   10

#define PKT_SUB_MASK    0xFFC000  // 00000000111111111100000000000000
#define PKT_SUB_SHIFT   14

#define PKT_PTYPE_MASK  0x1000000 // 00000001000000000000000000000000
#define PKT_PTYPE_SHIFT 24

/* Packet Types. */
#define ERROR     0
#define REFERENCE 1
#define DATA      2

/* WV size of I/O buffer pointer file and register file */
#define BUFFER_PTR_FILE_SZ 256
#define REGISTER_FILE_SZ 256

#endif
