#ifdef KERNEL_TEST_ENABLED
#include "../tests/kerneltypes.h"
#endif
 
/*
NOTES:
Tracked the main bug to the fact that the subtask table was shared among all services.
That leads to push/pop conflicts!
*/

void report(__global uint*, uint);

#include "SharedMacros.h"
#include "SharedTypes.h"
#include "SystemConfiguration.h"

#define RETURN_REL_PTR 0

/* Base K_B symbol. */
#define SYMBOL_KB_ZERO 0x6040000000000000

/* Base K_P symbol. */
#define SYMBOL_KP_ZERO 0x8000000000000000

/* Used to access the information stored within a subtask record. */
#define SUBTREC_NARGS_ABSENT_MASK    0xF    // 00001111
#define SUBTREC_NARGS_ABSENT_SHIFT   0

#define SUBTREC_STATUS_MASK          0xF0   // 11110000
#define SUBTREC_STATUS_SHIFT         4

#define SUBTREC_RETURN_AS_ADDR_MASK  0xFF00 // 1111111100000000
#define SUBTREC_RETURN_AS_ADDR_SHIFT 8

#define SUBTREC_RETURN_AS_POS_MASK   0xFF   // 0000000011111111
#define SUBTREC_RETURN_AS_POS_SHIFT  0

/* Subtask record status. */
#define NEW        0
#define PROCESSING 1
#define PENDING    2

/* Subtask record arg status. */
#define ABSENT     0
#define REQUESTING 1
#define PRESENT    2

/* Used to access symbol information. */

/* :4  :3          :1     :2        :6        :16                      :32 (8|8|8|8)           */
/* K_S :(Datatype) :(Ext) :(Quoted) :Task     :Mode|Reg|2Bits|NArgs    :SNId|SCLId|SCId|Opcode */
/* K_R :(Datatype) :(Ext) :Quoted   :CodePage :6Bits|5Bits|CodeAddres  :SNId|SCLId|SCId|Opcode */
/* K_B :Datatype   :0     :Quoted   :Task     :16Bits                  :Value                  */

#define SYMBOL_KIND_MASK     0xF000000000000000UL // 11110000000000000000000000000000 00000000000000000000000000000000
#define SYMBOL_KIND_SHIFT    60

#define SYMBOL_QUOTED_MASK   0xc0000000000000UL   // 00000000110000000000000000000000 00000000000000000000000000000000
#define SYMBOL_QUOTED_SHIFT  54

#define SYMBOL_ADDRESS_MASK  0xFFFF00000000UL     // 00000000000000001111111111111111 00000000000000000000000000000000
#define SYMBOL_ADDRESS_SHIFT 32

#define SYMBOL_NARGS_MASK    0xF00000000UL        // 00000000000000000000000000001111 00000000000000000000000000000000
#define SYMBOL_NARGS_FW		 0xF // 15 args max
#define SYMBOL_NARGS_SHIFT   32

#define SYMBOL_SERVICE_MASK  0xFFFFFFFFUL         // 00000000000000000000000000000000 11111111111111111111111111111111
#define SYMBOL_SERVICE_SHIFT 0

#define SYMBOL_SNId_MASK     0xFF000000UL         // 00000000000000000000000000000000 11111111000000000000000000000000
#define SYMBOL_SNId_SHIFT    24

#define SYMBOL_SNLId_MASK    0xFF0000UL           // 00000000000000000000000000000000 00000000111111110000000000000000
#define SYMBOL_SNLId_SHIFT   16

#define SYMBOL_SNCId_MASK    0xFF00UL             // 00000000000000000000000000000000 00000000000000001111111100000000
#define SYMBOL_SNCId_SHIFT   8

#define SYMBOL_OPCODE_MASK   0xFFUL               // 00000000000000000000000000000000 00000000000000000000000011111111
#define SYMBOL_OPCODE_SHIFT  0

#define SYMBOL_VALUE_MASK    0xFFFFFFFFUL         // 00000000000000000000000000000000 11111111111111111111111111111111
#define SYMBOL_VALUE_SHIFT   0

/* Definition of symbol kinds. */
#define K_S 0 // Contains information needed to create subtask record.
#define K_R 4 // Reference symbol.
#define K_B 6 // Data symbol.
#define K_P 8 // Contains a pointer to data memory.

/***********************************/
/******* Function Prototypes *******/
/***********************************/

void parse_pkt(packet p, __global packet *q, int n, __global bytecode *cStore, __global SubtaskTable *subt, __global uint *data);
uint parse_subtask(uint source,
                   uint arg_pos,
                   uint subtask,
                   uint address,
                   __global packet *q,
                   int n,
                   __global bytecode *cStore,
                   __global SubtaskTable *subt,
                   __global uint *data);
ulong service_compute(__global SubtaskTable* subt, uint subtask,__global uint *data);
bool computation_complete(__global packet *q,  __global packet *rq, int n);

void subt_store_symbol(bytecode payload, uint arg_pos, uint i, __global SubtaskTable *subt);
bool subt_is_ready(uint i, __global SubtaskTable *subt);
bool subt_push(uint i, __global SubtaskTable *subt);
bool subt_pop(uint *result, __global SubtaskTable *subt);
bool subt_is_full(__global SubtaskTable *subt);
bool subt_is_empty(__global SubtaskTable *subt);
void subt_cleanup(uint i, __global SubtaskTable *subt);
uint subt_top(__global SubtaskTable *subt);
void subt_set_top(__global SubtaskTable *subt, uint i);

uint subt_rec_get_service_id(__global SubtaskRecord *r);
bytecode subt_rec_get_arg(__global SubtaskRecord *r, uint arg_pos);
uint subt_rec_get_arg_status(__global SubtaskRecord *r, uint arg_pos);
uint subt_rec_get_subt_status(__global SubtaskRecord *r);
uint subt_rec_get_nargs(__global SubtaskRecord *r);
uint subt_rec_get_nargs_absent(__global SubtaskRecord *r);
uint subt_rec_get_code_addr(__global SubtaskRecord *r);
uint subt_rec_get_return_to(__global SubtaskRecord *r);
uint subt_rec_get_return_as(__global SubtaskRecord *r);
uint subt_rec_get_return_as_addr(__global SubtaskRecord *r);
uint subt_rec_get_return_as_pos(__global SubtaskRecord *r);
void subt_rec_set_service_id(__global SubtaskRecord *r, uint service_id);
void subt_rec_set_arg(__global SubtaskRecord *r, uint arg_pos, bytecode arg);
void subt_rec_set_arg_status(__global SubtaskRecord *r, uint arg_pos, uint status);
void subt_rec_set_subt_status(__global SubtaskRecord *r, uint status);
void subt_rec_set_nargs(__global SubtaskRecord *r, uint n);
void subt_rec_set_nargs_absent(__global SubtaskRecord *r, uint n);
void subt_rec_set_code_addr(__global SubtaskRecord *r, uint return_to);
void subt_rec_set_return_to(__global SubtaskRecord *r, uint return_to);
void subt_rec_set_return_as(__global SubtaskRecord *r, uint return_as);
void subt_rec_set_return_as_addr(__global SubtaskRecord *r, uint return_as_addr);
void subt_rec_set_return_as_pos(__global SubtaskRecord *r, uint return_as_pos);

bytecode symbol_KS_create(uint nargs, uint SNId, uint SNLId, uint SNCId, uint opcode);
bytecode symbol_KR_create(uint subtask, uint SNId);
bytecode symbol_KB_create(uint value);
uint symbol_get_kind(bytecode s);
bool symbol_is_quoted(bytecode s);
uint symbol_get_service(bytecode s);
uint symbol_get_SNId(bytecode s);
uint symbol_get_SNLId(bytecode s);
uint symbol_get_SNCId(bytecode s);
uint symbol_get_opcode(bytecode s);
uint symbol_get_address(bytecode s);
uint symbol_get_nargs(bytecode s);
uint symbol_get_value(bytecode s);
void symbol_set_kind(bytecode *s, ulong kind);
void symbol_quote(bytecode *s);
void symbol_unquote(bytecode *s);
void symbol_set_service(bytecode *s, ulong service);
void symbol_set_SNId(bytecode *s, ulong SNId);
void symbol_set_SNLId(bytecode *s, ulong SNLId);
void symbol_set_SNCId(bytecode *s, ulong SNCId);
void symbol_set_opcode(bytecode *s, ulong opcode);
void symbol_set_address(bytecode *s, ulong address);
void symbol_set_nargs(bytecode *s, ulong nargs);
void symbol_set_value(bytecode *s, ulong value);

void q_transfer(__global packet *rq,  __global packet *q, int n);
uint q_get_head_index(size_t id, size_t gid, __global packet *q, int n);
uint q_get_tail_index(size_t id, size_t gid, __global packet *q, int n);
void q_set_head_index(uint index, size_t id, size_t gid, __global packet *q, int n);
void q_set_tail_index(uint index, size_t id, size_t gid, __global packet *q, int n);
void q_set_last_op(uint type, size_t id, size_t gid,__global packet *q, int n);
bool q_last_op_is_read(size_t id, size_t gid, __global packet *q, int n);
bool q_last_op_is_write(size_t id, size_t gid, __global packet *q, int n);
bool q_is_empty(size_t id, size_t gid, __global packet *q, int n);
bool q_is_full(size_t id, size_t gid,__global packet *q, int n);
uint q_size(size_t id, size_t gid, __global packet *q, int n);
bool q_read(packet *result, size_t n_id, size_t q_id, __global packet *q, int n);
bool q_write(packet value, size_t n_id, size_t q_id,__global packet *q, int n);

packet pkt_create(uint type, uint source, uint arg, uint sub, uint payload);
uint pkt_get_type(packet p);
uint pkt_get_source(packet p);
uint pkt_get_arg_pos(packet p);
uint pkt_get_sub(packet p);
uint pkt_get_payload(packet p);
void pkt_set_type(packet *p, uint type);
void pkt_set_source(packet *p, uint source);
void pkt_set_arg_pos(packet *p, uint arg);
void pkt_set_sub(packet *p, uint sub);
void pkt_set_payload_type(packet *p, uint ptype);
void pkt_set_payload(packet *p, uint payload);

/*
 * For reasons unknown the following function signatures do not match
 * those of the function implementation in some systems. It has something
 * to do with the __global return value. By moving the function implementions
 * above functions that call them, I avoid requiring a function signature. It is messy
 * but its the only solution that works - I really don't see the conflict.

__global subt_rec* subt_get_rec(uint i, __global SubtaskTable *subt);
__global uint* get_arg_value(uint arg_pos, __global SubtaskRecord *rec, __global uint *data);
*/

/* Return the subtask record at index i in the subtask table. */
__global SubtaskRecord *subt_get_rec(uint i, __global SubtaskTable *subt) {
  return &(subt[get_group_id(0)].recs[i]);
}

// ------------------------------- GPRM KERNEL API -------------------------------
// These functions are for use inside GPRM kernels
//-------------------------------------------------------------------------------- 
uint get_nargs( __global SubtaskRecord *rec);
uint is_quoted_ref(uint arg_pos, __global SubtaskRecord *rec);


/* Get the argument value stored at 'arg_pos' from a subtask record. */
// In the case of a (quoted) K_R symbol, we need to return the actual symbol
// Note that we return indices into the data[] array, not absolute pointers, so the ulong type is fine
ulong get_arg_value(uint arg_pos, __global SubtaskRecord *rec, __global uint *data) {
  bytecode symbol = subt_rec_get_arg(rec, arg_pos);
  uint kind = symbol_get_kind(symbol);
  uint value = symbol_get_value(symbol);
  
  if (kind == K_R) {
	  // clearly, this is a problem: symbols are 64-bit
	// We can only get the 32-bit Name in this way!
	  // So maybe get_arg_value should be 64-bit
    return symbol;   
  } else if (kind == K_B || kind == K_P) {
    return value;
  }
  // If we don't know the kind, return the entire symbol
  return symbol;

} // END of get_arg_value()


/**************************/
/******* The Kernel *******/
/**************************/

__kernel void vm(__global packet *q,            /* Compute unit queues. */
                 __global packet *rq,           /* Transfer queues for READ state. */
                 uint nservicenodes,             /* The number of service nodes. */
                 __global int *state,           /* Are we in the READ or WRITE state? */
                 __global bytecode *cStore,     /* The code store. */
                 __global SubtaskTable *subt,           /* The subtask table. */
                 __global uint *data            /* Data memory for temporary results. */
                 ) {
 size_t th_id = get_local_id(0);
 size_t sn_id = get_group_id(0);
  if (*state == WRITE) {
// printf("WRITE Workgroup id: %d\n",sn_id);
	  
//      if (th_id==0) 
        q_transfer(rq, q, nservicenodes);
    
    /* Synchronise the work items to ensure that all packets have transferred. */
      // WV: don't see that this is needed if there is only one local thread
    barrier(CLK_GLOBAL_MEM_FENCE);
// if (th_id==0) {

    if (computation_complete(q, rq, nservicenodes)) {
#ifdef OCLDBG		
		printf("COMPLETE in %d\n",sn_id);
#endif		
      *state = COMPLETE;
    }
// }
  } else if (*state == READ) {
// printf("READ Workgroup id: %d\n",sn_id);
    for (int i = 0; i < nservicenodes; i++) {
      packet p;
      while (q_read(&p, sn_id, i, q, nservicenodes)) {
//		  printf("Reading packet @WG %d from queue %d\n",sn_id,i);
        parse_pkt(p, rq, nservicenodes, cStore, subt, data);
      }
    }
  }
} // END of kernel code

/* Is the entire computation complete? When all compute units are inactive (no packets in their queues)
   the computation is complete. */
bool computation_complete(__global packet *q, __global packet *rq, int n) {
  bool complete = true;
  for (int i = 0; i < n; i++) {
    for (int j = 0; j < n; j++) {
      if (!q_is_empty(i, j, q, n)) {
        complete =  false;
      }
      if (!q_is_empty(i, j, rq, n)) {
        complete = false;
      }
    }
  }
  return complete;
}

/* Inspect a packet and perform some action depending on its contents. */
void parse_pkt(packet p, __global packet *q, int nservicenodes, __global bytecode *cStore, __global SubtaskTable *subt, __global uint *data) {
  uint type = pkt_get_type(p);
  uint source = pkt_get_source(p);
  uint arg_pos = pkt_get_arg_pos(p);
  uint subtask = pkt_get_sub(p);
  uint address = pkt_get_payload(p);
/*
	The GPRM uses one compute unit per service node and one thread per compute unit
	However, the kernels can use all threads in a compute unit

	Consequently, the number of service nodes is reflected by the number of work groups, not by the global_id
	I guess I should restrict the VM's operations to a single thread by testing if ( th_id == 0 )
	to save on global memory accesses.
	I guess I don't need global_id in the VM
	If a SNId is larger than the number of work groups, it should be modulo'ed; 
	nservicenodes can be larger or smaller than the number of compute units. If it is smaller, the device is under-utilised;
	if it is larger, the host should use nservicenodes as the NDRange instead of nunits.

 */
  size_t nunits = get_num_groups(0);	
  size_t sn_id = get_group_id(0);
  size_t th_id = get_local_id(0);
  size_t gl_id = get_global_id(0);

  switch (type) {
  case ERROR:
    break;

  case REFERENCE: {
    /* Create a new subtask record */
    uint ref_subtask = parse_subtask(source, arg_pos, subtask, address, q, nservicenodes, cStore, subt, data);
    
    if (subt_is_ready(ref_subtask, subt)) {
      /* Perform the computation. */
      ulong result = service_compute(subt, ref_subtask, data);
      // In principle, result is a 64-bit integer as it could be a symbol

	  // In practice, the only case when we need more than 32 bits is K_R
	  // 32 bits are used for the Name (SNId:SCLId:SCId:Opcode) and another 16 (?) for the subtask
	  
	  // But as far as I remember, we never actually _return_ a K_R
	  // So 32 bits should do (provided we limit data[] to 4G 32-bit words
      /* Create a new packet containing the computation results. */
      //WV: ORIG packet p = pkt_create(DATA, get_global_id(0), arg_pos, subtask, result);
      packet p = pkt_create(DATA, sn_id, arg_pos, subtask, (uint)result);

	  //WV: for dispatching a quoted K_R, we need
	  
	  // packet p = pkt_create(REFERENCE, sn_id, arg_idx, ref_subtask, code_addr);
	  // Also, we need to avoid cleanup.
	  // The code_addr size is 10 bits;
	  // say 4 bits for the core status
	  // 4 bits for the arg_idx
	  // also need to have the destination, 8 bits
	  // altogether 10 + 4 + 4 + 8 = 26 bits
      // So, I guess what I will do is return the K_R symbol, 
	  // so 10 + 8 bits are definitely in the right place
	  // Then I can replace the opcode by the arg_idx and the core_status.
      /* Send the result back to the compute unit that sent the reference request. */
#ifdef OCLDBG	  
	  printf("Writing RESULT @WG %d (REF) to queue %d\n",sn_id,source);
#endif	  
      q_write(p, sn_id,source, q, nservicenodes);

      /* Free up the subtask record so that it may be re-used. */
      subt_cleanup(ref_subtask, subt);
    }
    break;
  }
    
  case DATA: {
    /* Store the data in the subtask record. */
    subt_store_symbol(SYMBOL_KP_ZERO + address, arg_pos, subtask, subt);

    if (subt_is_ready(subtask, subt)) {
      /* Perform the computation. */
      ulong result = service_compute(subt, subtask, data);

      /* Figure out where to send the result to. */
      __global SubtaskRecord *rec = subt_get_rec(subtask, subt);
      uint return_to = subt_rec_get_return_to(rec);
      uint return_as_addr = subt_rec_get_return_as_addr(rec);
      uint return_as_pos = subt_rec_get_return_as_pos(rec);

      /* Initial reference packet doesn't need to send the result anyhere. It's in the data buffer. */
      if (return_to == RETURN_ADDRESS) { // WV: ORIG (n + 1)) 
#ifdef OCLDBG		  
		  printf("RETURN from @WG %d\n", sn_id);
#endif		  
//        break;
      } else {
      
      /* Create and send new packet containing the computation results. */
      packet p = pkt_create(DATA, sn_id, return_as_pos, return_as_addr, (uint)result);
#ifdef OCLDBG	  
	  printf("Writing RESULT @WG %d (DATA) to queue %d\n",sn_id,return_to);
#endif	  
      q_write(p, sn_id,return_to, q, nservicenodes);

      /* Free up the subtask record so that it may be re-used. */
      subt_cleanup(subtask, subt);
	  }
    }
    break;
  } 
  } // END of switch()
}
/* Create a subtask record from reference packet information. Return an identifier to the
   subtask record in the subtask table. */
uint parse_subtask(uint source,                  /* The compute unit who sent the request. */
                   uint arg_pos,                 /* The argument position the result should be stored at. */
                   uint subtask,                 /* The subtask record at which the result should be stored. */
                   uint address,                 /* The location of the bytecode in the code store. */
                   __global packet *q,
                   int nservicenodes,
                   __global bytecode *cStore,
                   __global SubtaskTable *subt,
                   __global uint *data
                   ) {
	//printf("parse_subtask() in WG %d\n",get_group_id(0));
  /* Get an available subtask record from the stack */
  uint av_index; // WV: av_index is the subtask address
  while (!subt_pop(&av_index, subt)) {} // WV: weird!
  __global SubtaskRecord *rec = subt_get_rec(av_index, subt);

  /* Get the K_S symbol from the code store. */
  bytecode symbol = cStore[address * MAX_BYTECODE_SZ];

  /* Create a new subtask record. */
  uint service = symbol_get_service(symbol);
  uint nargs = symbol_get_nargs(symbol);

  subt_rec_set_service_id(rec, service);
  subt_rec_set_code_addr(rec, address);
  subt_rec_set_subt_status(rec, NEW);
  subt_rec_set_nargs(rec, nargs);
  subt_rec_set_nargs_absent(rec, nargs);
  subt_rec_set_return_to(rec, source);
  subt_rec_set_return_as_addr(rec, subtask);
  subt_rec_set_return_as_pos(rec, arg_pos);

  /* Begin argument processing */
  subt_rec_set_subt_status(rec, PROCESSING);

  for (int arg_pos = 0; arg_pos < nargs; arg_pos++) {
    /* Mark argument as absent. */
    subt_rec_set_arg_status(rec, arg_pos, ABSENT);

    /* Get the next symbol (K_R or K_B) */
    symbol = cStore[(address * MAX_BYTECODE_SZ) + arg_pos + 1];

    switch (symbol_get_kind(symbol)) {
    case K_R:
      if (symbol_is_quoted(symbol)) {
		  /*
#ifdef OCLDBG          
          printf("Found quoted K_R: %d:%d:%d:%d, %d\n",                  
                   symbol_get_SNId(symbol),
 symbol_get_SNLId(symbol),
 symbol_get_SNCId(symbol),
 symbol_get_opcode(symbol),
 symbol_get_address(symbol)
 );
#endif      
*/
        subt_store_symbol(symbol, arg_pos, av_index, subt);
      } else {
		  /*
#ifdef OCLDBG          
          printf("Found unquoted K_R: %d:%d:%d:%d, %d\n",                  
                   symbol_get_SNId(symbol),
 symbol_get_SNLId(symbol),
 symbol_get_SNCId(symbol),
 symbol_get_opcode(symbol),
 symbol_get_address(symbol)
 );
#endif      
*/
        /* Create a packet to request a computation. */
        uint address = symbol_get_address(symbol);
        packet p = pkt_create(REFERENCE, get_group_id(0), arg_pos, av_index, address);

        /* Find out which service should perform the computation. */
        uint snid = symbol_get_SNId(symbol);
        
        uint destination = (snid - 1) % nservicenodes; // -1 as service 0 is not reserved for gateway.
#ifdef OCLDBG        
//         printf("DEST: (%d - 1) mod %d = %d\n",snid,nservicenodes,destination);
#endif         
        /* Send the packet and request the computation. */		
        q_write(p, get_group_id(0), destination, q, nservicenodes);

        /* Mark argument as requested. */
        subt_rec_set_arg_status(rec, arg_pos, REQUESTING);
      }
      break;

    case K_B: {
#ifdef OCLDBG      
//	printf("Found K_B: %u\n",symbol_get_value(symbol));
#endif      
      subt_store_symbol(symbol, arg_pos, av_index, subt);
      break;
			  }
    } // END of switch()
  } // for()

  /* Waiting for references to be computed. */
  subt_rec_set_subt_status(rec, PENDING);

  return av_index;
} // END of parse_subtask()

/* Perform the computation represented by a subtask record and return a relative index
   to the result in the data buffer. */
ulong service_compute(__global SubtaskTable* subt, uint subtask, __global uint *data) {
  __global SubtaskRecord *rec = subt_get_rec(subtask, subt);
  uint service_details = subt_rec_get_service_id(rec);

  uint library = symbol_get_SNLId(service_details);
  uint class = symbol_get_SNCId(service_details);
  uint method = symbol_get_opcode(service_details);
  uint service = (library << 16) + (class << 8) + method;
// ---------------- ---------------- GPRM KERNELS ---------------- ---------------- 
// TODO: put these in separate files/functions, mimic OpenCL kernels more closely

  switch (service) {
  case M_OclGPRM_MAT_mult: {
#ifdef OCLDBG          
          printf("Calling M_OclGPRM_MAT_mult, work group id = %d\n",get_group_id(0));
#endif            
		  
    __global uint *m1 = &data[get_arg_value(0, rec, data)]; 
    __global uint *m2 = &data[get_arg_value(1, rec, data)]; 
	ulong res_idx = get_arg_value(2, rec, data);
    __global uint *result = &data[res_idx]; 
     uint n = (uint)get_arg_value(3, rec, data);

#ifdef OCLDBG          
     printf("Got args for M_OclGPRM_MAT_mult, size = %d\n",n);
	 /*
	 
     printf("Mem addr m1 = 0x%X - 0x%X = 0x%X\n",m1,data,((uint)m1-(uint)data));
	      printf("Mem addr check = 0x%X\n",data[1]);

	printf("m1[0]=%d\n",data[data[1]]);
	 printf("m1[0]=%d\n",data[((uint)m1-(uint)data)>>2]); // i.e. data+((m1-data)>>2) or data + m1>>2 - data>>2
	
	 printf("m1=0x%X\n",m1);
     printf("Mem addr m2 = 0x%X\n",((uint)m2-(uint)data));
     printf("Mem addr result = 0x%X\n",((uint)result-(uint)data));
*/
#endif


    for (int i = 0; i < n; i++) {
      for (int r = 0; r < n; r++) {
        uint sum = 0;
        for (int c = 0; c < n; c++) {
          sum +=  m1[i * n + c] * m2[c * n + r];
        }
        result[i*n+r] = sum;
      }
    }


	/*
#ifdef OCLDBG          
    for (int i = 0; i < n; i++) {
      for (int r = 0; r < n; r++) {
		  printf("%d ",m1[i*n+r]);
      }
	  printf("\n");
    }
    for (int i = 0; i < n; i++) {
      for (int r = 0; r < n; r++) {
		  printf("%d ",m2[i*n+r]);
      }
	  printf("\n");
    }
    for (int i = 0; i < n; i++) {
      for (int r = 0; r < n; r++) {
		  printf("%d ",result[i*n+r]);
      }
	  printf("\n");
    }
          printf("Done M_OclGPRM_MAT_mult\n");
#endif                                    
    */
    return res_idx;
  }

  case M_OclGPRM_MAT_add: {
#ifdef OCLDBG          
          printf("Calling M_OclGPRM_MAT_add, work group id = %d\n",get_group_id(0));
#endif                                    
							  
    ulong m1_idx = get_arg_value(0, rec, data);
    ulong m2_idx = get_arg_value(1, rec, data);
    ulong result_idx = get_arg_value(2, rec, data);
    uint sz = (uint)get_arg_value(3, rec, data);
    uint n = sz;
	__global uint* m1 = &data[m1_idx];
	__global uint* m2 = &data[m2_idx];
	__global uint* result = &data[result_idx];
    
    for (int row = 0; row < n; row++) {
      for (int col = 0; col < n; col++) {
        uint sum = m1[row * n + col] + m2[row * n + col];
        result[row * n + col] = sum;
      }
    }

    return result_idx;
    }

  case M_OclGPRM_MAT_unit: {
#ifdef OCLDBG          
          printf("Calling M_OclGPRM_MAT_unit, work group id = %d\n",get_group_id(0));
#endif                                    
    ulong m_idx= get_arg_value(0, rec, data);
    uint sz = (uint)get_arg_value(1, rec, data);
    uint n = sz;
	__global uint* m = &data[m_idx];
    for (int i = 0; i < n; i++) {
      for (int j = 0; j < n; j++) {
        m[i * n + j] = (i == j) ? 1 : 0;
      }
    }
	/*
#ifdef OCLDBG          
	
    for (int i = 0; i < n; i++) {
      for (int r = 0; r < n; r++) {
		  printf("%d ",m[i*n+r]);
      }
	  printf("\n");
    }
          printf("Done M_OclGPRM_MAT_unit, work group id = %d\n",get_group_id(0));
#endif
*/
    return m_idx; // 
  }

  case M_OclGPRM_MEM_ptr: {
#ifdef OCLDBG          
          printf("Calling M_OclGPRM_MEM_ptr, work group id = %d\n",get_group_id(0));
#endif                                    
    ulong arg1 = (uint) get_arg_value(0, rec, data);
    return data[DATA_INFO_OFFSET + arg1];
	/*
#ifdef OCLDBG
    printf("Found pointer data[%d]=%d\n",DATA_INFO_OFFSET + arg1,data[DATA_INFO_OFFSET + arg1]);
    printf("Mem addr = 0x%X + 0x%X = 0x%X\n",data , data[DATA_INFO_OFFSET + arg1]<<2,data + data[DATA_INFO_OFFSET + arg1]);
	data[ data[DATA_INFO_OFFSET + arg1] ] =557188;
#endif    
    //return (__global uint*)((uint)data + (data[DATA_INFO_OFFSET + arg1]<<2)); // a pointer to the memory
    return (__global uint*)(&data[ data[DATA_INFO_OFFSET + arg1] ]); // a pointer to the memory
#endif
*/
  }
    
  case M_OclGPRM_MEM_const: {
#ifdef OCLDBG          
          printf("Calling M_OclGPRM_MEM_const, work group id = %d\n",get_group_id(0));
#endif                                    

    ulong arg1 = get_arg_value(0, rec, data);
    return data[arg1 + DATA_INFO_OFFSET]; // the actual constant
  }
    
  case M_OclGPRM_REG_read: {
    ulong arg1 = get_arg_value(0, rec, data);
    return data[arg1 + DATA_INFO_OFFSET + BUFFER_PTR_FILE_SZ]; // the value stored in the reg
  }
    
  case M_OclGPRM_REG_write: {
    ulong arg1 = get_arg_value(0, rec, data);
    ulong arg2 = get_arg_value(1, rec, data);
    data[arg1 + DATA_INFO_OFFSET + BUFFER_PTR_FILE_SZ]=arg2;
    return arg2; // the value just written
  }

// We assume every service returns a pointer
  case M_OclGPRM_CTRL_begin: {
#ifdef OCLDBG          
          printf("Calling M_OclGPRM_CTRL_begin, work group id = %d\n",get_group_id(0));
#endif                                    
    ulong nargs = get_nargs(rec);
    ulong last_arg = get_arg_value(nargs-1, rec, data);
    return last_arg;
  }

  case M_OclGPRM_TEST_report: {
#ifdef OCLDBG          
          printf("Calling M_OclGPRM_TEST_report, work group id = %d\n",get_group_id(0));
#endif                                    
	ulong res_array_idx = get_arg_value(1, rec, data); // idx into data[]
    ulong faulty_idx = get_arg_value(0, rec, data); 	
    ulong faulty_idx2 = get_arg_value(2, rec, data); 	

    ulong  idx = get_group_id(0);//get_arg_value(1, rec, data); 	
	// report takes an absolute pointer
    //report(&data[res_array_idx],idx);
    data[res_array_idx+4*idx+0]=get_global_id(0);
    data[res_array_idx+4*idx+1]=faulty_idx2;//get_local_id(0);
    data[res_array_idx+4*idx+2]=get_group_id(0);
    data[res_array_idx+4*idx+3]=faulty_idx;
    
	return res_array_idx;
  }

  case M_OclGPRM_CTRL_if: {
    uint pred = (uint)get_arg_value(0, rec, data);
    uint condval= pred & 0x1;
    uint argidx= 2-condval;    
    if (is_quoted_ref(argidx,rec)) {
        ulong ref_symbol=get_arg_value(argidx, rec, data);
		ref_symbol= (ref_symbol & 0xFFFFFFFFFFFFFF00) +(argidx<<4)+1;
		return ref_symbol;
    } else {
        return get_arg_value(argidx, rec, data);
    }                                
  }
  default:
	return 0;
  }; // END of switch() of 

  return 0;
} // END of service_compute()
// ---------------- ---------------- END OF GPRM KERNELS ---------------- ---------------- 

/*********************************/
/**** Subtask Table Functions ****/
/*********************************/

/* Store a symbol in argument 'arg_pos' at subtask record 'i'. */
void subt_store_symbol(bytecode symbol, uint arg_pos, uint i, __global SubtaskTable *subt) {
  __global SubtaskRecord *rec = subt_get_rec(i, subt);
  subt_rec_set_arg(rec, arg_pos, symbol);
  uint nargs_absent = subt_rec_get_nargs_absent(rec) - 1;
  subt_rec_set_arg_status(rec, arg_pos, PRESENT);
  subt_rec_set_nargs_absent(rec, nargs_absent);
}

/* Is the subtask record at index i ready for computation? */
bool subt_is_ready(uint i, __global SubtaskTable *subt) {
  __global SubtaskRecord *rec = subt_get_rec(i, subt);
  return subt_rec_get_nargs_absent(rec) == 0;
}

/* Remove the subtask record at index i from the subtask table and return
   it to the stack of available records. */
bool subt_push(uint i, __global SubtaskTable *subt) {
  if (subt_is_empty(subt)) {
    return false;
  }
  
  uint top = subt_top(subt);
  subt[get_group_id(0)].av_recs[top - 1] = i;
  subt_set_top(subt, top - 1);
  return true;
}

/* Return an available subtask record index from the subtask table. */
bool subt_pop(uint *av_index, __global SubtaskTable *subt) {
  if (subt_is_full(subt)) {
    return false;
  }

  uint top = subt_top(subt);
  *av_index = subt[get_group_id(0)].av_recs[top];
  subt_set_top(subt, top + 1);
  return true;
}

/* Remove and cleanup the subtask record at index i from the subtask table. */
void subt_cleanup(uint i, __global SubtaskTable *subt) {
  subt_push(i, subt);
}

/* Is the subtask table full? */
bool subt_is_full(__global SubtaskTable *subt) {
  return subt_top(subt) == SUBT_SIZE + 1;
}

/* Is the subtask table empty? */
bool subt_is_empty(__global SubtaskTable *subt) {
  return subt_top(subt) == 1;
}

/* Return the top of available records stack index. */
uint subt_top(__global SubtaskTable *subt) {
  return subt[get_group_id(0)].av_recs[0];
}

/* Set the top of available records stack index. */
void subt_set_top(__global SubtaskTable *subt, uint i) {
  subt[get_group_id(0)].av_recs[0] = i;
}

/**********************************/
/**** Subtask Record Functions ****/
/**********************************/

/* Get the subtask record service id. */
uint subt_rec_get_service_id(__global SubtaskRecord *r) {
  return r->service_id;
}

/* Get a subtask record argument. */
bytecode subt_rec_get_arg(__global SubtaskRecord *r, uint arg_pos) {
  return r->args[arg_pos];
}

/* Get the status of a subtask record argument. */
uint subt_rec_get_arg_status(__global SubtaskRecord *r, uint arg_pos) {
  return r->arg_status[arg_pos];
}

/* Get the subtask record status. */
uint subt_rec_get_subt_status(__global SubtaskRecord *r) {
  return (r->subt_status & SUBTREC_STATUS_MASK) >> SUBTREC_STATUS_SHIFT;
}

/* Get the number of arguments from the subtask record. */
uint subt_rec_get_nargs(__global SubtaskRecord *r) {
  return r->nargs;
}

/* Get the number of arguments absent in the subtask record. */
uint subt_rec_get_nargs_absent(__global SubtaskRecord *r) {
  return (r->subt_status & SUBTREC_NARGS_ABSENT_MASK);
}

/* Get the subtask record return to attribute. */
uint subt_rec_get_return_to(__global SubtaskRecord *r) {
  return r->return_to;
}

/* Get the subtask record return as attribute. */
uint subt_rec_get_return_as(__global SubtaskRecord *r) {
  return r->return_as;
}

/* Get the subtask record return as address. */
uint subt_rec_get_code_addr(__global SubtaskRecord *r) {
  return r->code_addr;
}

/* Get the subtask record return as address. */
uint subt_rec_get_return_as_addr(__global SubtaskRecord *r) {
  return r->return_as & SUBTREC_RETURN_AS_ADDR_MASK;
}

/* Get the subtask record return as position. */
uint subt_rec_get_return_as_pos(__global SubtaskRecord *r) {
  return r->return_as & SUBTREC_RETURN_AS_POS_MASK;
}

/* Set the subtask record service id. */
void subt_rec_set_service_id(__global SubtaskRecord *r, uint service_id) {
  r->service_id = service_id;
}

/* Set the value of the a subtask record argument. */
void subt_rec_set_arg(__global SubtaskRecord *r, uint arg_pos, bytecode arg) {
  r->args[arg_pos] = arg;
}

/* Set the status of a subtask record argument. */
void subt_rec_set_arg_status(__global SubtaskRecord *r, uint arg_pos, uint status) {
  r->arg_status[arg_pos] = status;
}

/* Set the subtask record status. */
void subt_rec_set_subt_status(__global SubtaskRecord *r, uint status) {
  r->subt_status = (r->subt_status & ~SUBTREC_STATUS_MASK)
    | ((status << SUBTREC_STATUS_SHIFT) & SUBTREC_STATUS_MASK);
}

/* Set the subtask record nargs attribute. */
void subt_rec_set_nargs(__global SubtaskRecord *r, uint nargs) {
  r->nargs = nargs;
}

/* Set the subtask record nargs_absent attribute. */
void subt_rec_set_nargs_absent(__global SubtaskRecord *r, uint n) {
  r->subt_status = (r->subt_status & ~SUBTREC_NARGS_ABSENT_MASK)
    | ((n << SUBTREC_NARGS_ABSENT_SHIFT) & SUBTREC_NARGS_ABSENT_MASK);
}

/* Set the subtask record return to attribute. */
void subt_rec_set_return_to(__global SubtaskRecord *r, uint return_to) {
  r->return_to = return_to;
}

/* Set the subtask record return_as attribute. */
void subt_rec_set_return_as(__global SubtaskRecord *r, uint return_as) {
  r->return_as = return_as;
}

/* Set the subtask record return_as address. */
void subt_rec_set_code_addr(__global SubtaskRecord *r, uint code_addr) {
  r->code_addr= code_addr;
}

/* Set the subtask record return_as address. */
void subt_rec_set_return_as_addr(__global SubtaskRecord *r, uint return_as_addr) {
  r->return_as = (r->return_as & ~SUBTREC_RETURN_AS_ADDR_MASK)
    | ((return_as_addr << SUBTREC_RETURN_AS_ADDR_SHIFT) & SUBTREC_RETURN_AS_ADDR_MASK);
}

/* Set the subtask record return_as position. */
void subt_rec_set_return_as_pos(__global SubtaskRecord *r, uint return_as_pos) {
  r->return_as = (r->return_as & ~SUBTREC_RETURN_AS_POS_MASK)
    | ((return_as_pos << SUBTREC_RETURN_AS_POS_SHIFT) & SUBTREC_RETURN_AS_POS_MASK);
}

/**************************/
/**** Symbol Functions ****/
/**************************/

/* Create a K_S symbol. */
bytecode symbol_KS_create(uint nargs, uint SNId, uint SNLId, uint SNCId, uint opcode) {
  bytecode s = 0;
  symbol_set_kind(&s, K_S);
  symbol_unquote(&s);
  symbol_set_nargs(&s, nargs);
  symbol_set_SNId(&s, SNId);
  symbol_set_SNLId(&s, SNLId);
  symbol_set_SNCId(&s, SNCId);
  symbol_set_opcode(&s, opcode);
  return s;
}

/* Create a K_R symbol. */
bytecode symbol_KR_create(uint subtask, uint name) {
  bytecode s = 0;
  symbol_set_kind(&s, K_R);
  symbol_unquote(&s);
  symbol_set_address(&s, subtask);
  symbol_set_value(&s, name);
  return s;
}

/* Create a K_B symbol. */
bytecode symbol_KB_create(uint value) {
  bytecode s = 0;
  symbol_set_kind(&s, K_B);
  symbol_quote(&s);
  symbol_set_value(&s, value);
  return s;
}

/* Return the symbol kind. */
uint symbol_get_kind(bytecode s) {
  return (s & SYMBOL_KIND_MASK) >> SYMBOL_KIND_SHIFT;
}

/* Is the symbol quoted? */
bool symbol_is_quoted(bytecode s) {
  return (s & SYMBOL_QUOTED_MASK) >> SYMBOL_QUOTED_SHIFT;
}

/* Return the symbol (K_S) service. */
uint symbol_get_service(bytecode s) {
  return (s & SYMBOL_SERVICE_MASK) >> SYMBOL_SERVICE_SHIFT;
}

/* Return the symbol (K_S) SNId. */
uint symbol_get_SNId(bytecode s) {
  return (s & SYMBOL_SNId_MASK) >> SYMBOL_SNId_SHIFT;
}

/* Return the symbol (K_S) SNLId. */
uint symbol_get_SNLId(bytecode s) {
  return (s & SYMBOL_SNLId_MASK) >> SYMBOL_SNLId_SHIFT;
}

/* Return the symbol (K_S) SNCId. */
uint symbol_get_SNCId(bytecode s) {
  return (s & SYMBOL_SNCId_MASK) >> SYMBOL_SNCId_SHIFT;
}

/* Return the symbol (K_S | K_R) opcode. */
uint symbol_get_opcode(bytecode s) {
  return (s & SYMBOL_OPCODE_MASK) >> SYMBOL_OPCODE_SHIFT;
}

/* Return the symbol (K_R) subtask. */
uint symbol_get_address(bytecode s) {
  return (s & SYMBOL_ADDRESS_MASK) >> SYMBOL_ADDRESS_SHIFT;
}

/* Return the symbol (K_S) nargs */
uint symbol_get_nargs(bytecode s) {
//  return (s & SYMBOL_NARGS_MASK) >> SYMBOL_NARGS_SHIFT;
  return (s >> SYMBOL_NARGS_SHIFT) & SYMBOL_NARGS_FW;
}

/* Return the symbol (K_B) value. */
uint symbol_get_value(bytecode s) {
  //return (s & SYMBOL_VALUE_MASK) >> SYMBOL_VALUE_SHIFT;
  return (uint)s;
}

/* Set the symbol kind */
void symbol_set_kind(bytecode *s, ulong kind) {
  *s = ((*s) & ~SYMBOL_KIND_MASK) | ((kind << SYMBOL_KIND_SHIFT) & SYMBOL_KIND_MASK);
}

/* Return true if the symbol is quoted, false otherwise. */
void symbol_quote(bytecode *s) {
  *s = ((*s) & ~SYMBOL_QUOTED_MASK) | ((1UL << SYMBOL_QUOTED_SHIFT) & SYMBOL_QUOTED_MASK);
}

/* Return true if the symbol is unquoted, false otherwise. */
void symbol_unquote(bytecode *s) {
  *s = ((*s) & ~SYMBOL_QUOTED_MASK) | ((0UL << SYMBOL_QUOTED_SHIFT) & SYMBOL_QUOTED_MASK);
}

/* Set the symbol (K_S | K_R) service. */
void symbol_set_service(bytecode *s, ulong service) {
  *s = ((*s) & ~SYMBOL_SERVICE_MASK) | ((service << SYMBOL_SERVICE_SHIFT) & SYMBOL_SERVICE_MASK);
}

/* Set the symbol (K_S | K_R) SNId. */
void symbol_set_SNId(bytecode *s, ulong SNId) {
  *s = ((*s) & ~SYMBOL_SNId_MASK) | ((SNId << SYMBOL_SNId_SHIFT) & SYMBOL_SNId_MASK);
}

/* Set the symbol (K_S | K_R) SNLId. */
void symbol_set_SNLId(bytecode *s, ulong SNLId) {
  *s = ((*s) & ~SYMBOL_SNLId_MASK) | ((SNLId << SYMBOL_SNLId_SHIFT) & SYMBOL_SNLId_MASK);
}

/* Set the symbol (K_S | K_R) SNCId. */
void symbol_set_SNCId(bytecode *s, ulong SNCId) {
  *s = ((*s) & ~SYMBOL_SNCId_MASK) | ((SNCId << SYMBOL_SNCId_SHIFT) & SYMBOL_SNCId_MASK);
}

/* Set the symbol (K_S | K_R) opcode. */
void symbol_set_opcode(bytecode *s, ulong opcode) {
  *s = ((*s) & ~SYMBOL_OPCODE_MASK) | ((opcode << SYMBOL_OPCODE_SHIFT) & SYMBOL_OPCODE_MASK);
}

/* Set the symbol (K_R) code address. */
void symbol_set_address(bytecode *s, ulong address) {
  *s = ((*s) & ~SYMBOL_ADDRESS_MASK) | ((address << SYMBOL_ADDRESS_SHIFT) & SYMBOL_ADDRESS_MASK);
}

/* Set the symbol (K_S) nargs. */
void symbol_set_nargs(bytecode *s, ulong nargs) {
  *s = ((*s) & ~SYMBOL_NARGS_MASK) | ((nargs << SYMBOL_NARGS_SHIFT) & SYMBOL_NARGS_MASK);
}

/* Set the symbol (K_B) value. */
void symbol_set_value(bytecode *s, ulong value) {
  *s = ((*s) & ~SYMBOL_VALUE_MASK) | ((value << SYMBOL_VALUE_SHIFT) & SYMBOL_VALUE_MASK);
}

/*************************/
/**** Queue Functions ****/
/*************************/

/* Copy all compute unit owned queue values from the readQueue into the real queues. */
void q_transfer(__global packet *rq, __global packet *q, int n) {
  packet packet;
  size_t n_id = get_group_id(0);
  for (int i = 0; i < n; i++) {
    while (q_read(&packet, n_id, i, rq, n)) {
//		printf("TRANSFER in %d for %d\n",n_id,i);
      q_write(packet, i, n_id, q, n);
    }
  }
}

/* Returns the array index of the head element of the queue. */
uint q_get_head_index(size_t n_id, size_t q_id, __global packet *q, int n) {
  ushort2 indices = as_ushort2(q[n_id * n + q_id].x);
  return indices.x;
}

/* Returns the array index of the tail element of the queue. */
uint q_get_tail_index(size_t n_id, size_t q_id,__global packet *q, int n) {
  ushort2 indices = as_ushort2(q[n_id * n + q_id].x);
  return indices.y;
}

/* Set the array index of the head element of the queue. */
void q_set_head_index(uint index, size_t n_id, size_t q_id, __global packet *q, int n) {
  ushort2 indices = as_ushort2(q[n_id * n + q_id].x);
  indices.x = index;
  q[n_id * n + q_id].x = as_uint(indices);
}

/* Set the array index of the tail element of the queue. */
void q_set_tail_index(uint index, size_t n_id, size_t q_id,__global packet *q, int n) {
  ushort2 indices = as_ushort2(q[n_id * n + q_id].x);
  indices.y = index;
  q[n_id * n + q_id].x = as_uint(indices);
}

/* Set the type of the operation last performed on the queue. */
void q_set_last_op(uint type, size_t n_id, size_t q_id, __global packet *q, int n) {
  q[n_id * n + q_id].y = type;
}

/* Returns true if the last operation performed on the queue is a read, false otherwise. */
bool q_last_op_is_read(size_t n_id, size_t q_id,__global packet *q, int n) {
  return q[n_id * n + q_id].y == READ;
}

/* Returns true if the last operation performed on the queue is a write, false otherwise. */
bool q_last_op_is_write(size_t n_id, size_t q_id,__global packet *q, int n) {
  return q[n_id * n + q_id].y == WRITE;
}

/* Returns true if the queue is empty, false otherwise. */
bool q_is_empty(size_t n_id, size_t q_id,  __global packet *q, int n) {
  return (q_get_head_index(n_id, q_id, q, n) == q_get_tail_index(n_id, q_id, q, n))
    && q_last_op_is_read(n_id, q_id, q, n);
}

/* Returns true if the queue is full, false otherwise. */
bool q_is_full(size_t n_id, size_t q_id, __global packet *q, int n) {
  return (q_get_head_index(n_id, q_id, q, n) == q_get_tail_index(n_id, q_id, q, n))
    && q_last_op_is_write(n_id, q_id, q, n);
}

/* Return the size of the queue. */
uint q_size(size_t n_id, size_t q_id, __global packet *q, int n) {
  if (q_is_full(n_id, q_id, q, n)) return MAX_BYTECODE_SZ;
  if (q_is_empty(n_id, q_id, q, n)) return 0;
  uint head = q_get_head_index(n_id, q_id, q, n);
  uint tail = q_get_tail_index(n_id, q_id, q, n);
  return (tail > head) ? (tail - head) : MAX_BYTECODE_SZ - head;
}

/* Read the value located at the head index of the queue into 'result'.
 * Returns true if succcessful (queue is not empty), false otherwise. */
bool q_read(packet *result, size_t n_id, size_t q_id, __global packet *q, int n) {
  if (q_is_empty(n_id, q_id, q, n)) {
    return false;
  }

  int index = q_get_head_index(n_id, q_id, q, n);
  *result = q[(n*n) + (n_id * n * MAX_BYTECODE_SZ) + (q_id * MAX_BYTECODE_SZ) + index];
  q_set_head_index((index + 1) % MAX_BYTECODE_SZ, n_id, q_id, q, n);
  q_set_last_op(READ, n_id, q_id, q, n);
  return true;
}

/* Write a value into the tail index of the queue.
 * Returns true if successful (queue is not full), false otherwise. */
bool q_write(packet value, size_t n_id, size_t q_id,__global packet *q, int n) {
//	printf("Writing @WG %d into queue for WG %d\n",n_id,q_id);
  if (q_is_full(n_id, q_id, q, n)) {
    return false;
  }

  int index = q_get_tail_index(n_id, q_id, q, n);
  q[(n*n) + (n_id * n * MAX_BYTECODE_SZ) + (q_id * MAX_BYTECODE_SZ) + index] = value;
  q_set_tail_index((index + 1) % MAX_BYTECODE_SZ, n_id, q_id, q, n);
  q_set_last_op(WRITE, n_id, q_id, q, n);
  return true;
}

/**************************/
/**** Packet Functions ****/
/**************************/

/* Return a newly created packet. */
packet pkt_create(uint type, uint source, uint arg, uint sub, uint payload) {
  packet p;
  pkt_set_type(&p, type);
  pkt_set_source(&p, source);
  pkt_set_arg_pos(&p, arg);
  pkt_set_sub(&p, sub);
  pkt_set_payload_type(&p, 0);
  pkt_set_payload(&p, payload);
  return p;
}

/* Return the packet type. */
uint pkt_get_type(packet p) {
  return (p.x & PKT_TYPE_MASK) >> PKT_TYPE_SHIFT;
}

/* Return the packet source address. */
uint pkt_get_source(packet p) {
  return (p.x & PKT_SRC_MASK) >> PKT_SRC_SHIFT;
}

/* Return the packet argument position. */
uint pkt_get_arg_pos(packet p) {
  return (p.x & PKT_ARG_MASK) >> PKT_ARG_SHIFT;
}

/* Return the packet subtask. */
uint pkt_get_sub(packet p) {
  return (p.x & PKT_SUB_MASK) >> PKT_SUB_SHIFT;
}

/* Return the packet payload. */
uint pkt_get_payload(packet p) {
  return p.y;
}

/* Set the packet type. */
void pkt_set_type(packet *p, uint type) {
  (*p).x = ((*p).x & ~PKT_TYPE_MASK) | ((type << PKT_TYPE_SHIFT) & PKT_TYPE_MASK);
}

/* Set the packet source address. */
void pkt_set_source(packet *p, uint source) {
  (*p).x = ((*p).x & ~PKT_SRC_MASK) | ((source << PKT_SRC_SHIFT) & PKT_SRC_MASK);
}

/* Set the packet argument position. */
void pkt_set_arg_pos(packet *p, uint arg) {
  (*p).x = ((*p).x & ~PKT_ARG_MASK) | ((arg << PKT_ARG_SHIFT) & PKT_ARG_MASK);
}

/* Set the packet subtask. */
void pkt_set_sub(packet *p, uint sub) {
  (*p).x = ((*p).x & ~PKT_SUB_MASK) | ((sub << PKT_SUB_SHIFT) & PKT_SUB_MASK);
}

/* Set the packet payload type. */
void pkt_set_payload_type(packet *p, uint ptype) {
  (*p).x = ((*p).x & ~PKT_PTYPE_MASK) | ((ptype << PKT_PTYPE_SHIFT) & PKT_PTYPE_MASK);
}

/* Set the packet payload. */
void pkt_set_payload(packet *p, uint payload) {
  (*p).y = payload;
}

// ------------------------------- GPRM KERNEL API -------------------------------
// These functions are for use inside GPRM kernels
//-------------------------------------------------------------------------------- 

uint get_nargs( __global SubtaskRecord *rec) {
    return rec->nargs;
}

uint is_quoted_ref(uint arg_pos, __global SubtaskRecord *rec ) {
  bytecode symbol = subt_rec_get_arg(rec, arg_pos);
  uint kind = symbol_get_kind(symbol);
  uint quoted = symbol_is_quoted(symbol);
  return ((kind == K_R) && quoted == 1) ? 1 : 0;
}

void report(__global uint* res_array, uint idx) {
    res_array[4*idx+0]=get_global_id(0);
    res_array[4*idx+1]=get_local_id(0);
    res_array[4*idx+2]=get_group_id(0);
    res_array[4*idx+3]=idx;
}
        
