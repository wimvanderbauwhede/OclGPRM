#include "SharedMacros.h"
#include "SharedTypes.h"
//#include "SystemConfiguration.h"
#define __global


enum Core_Status {
    CS_idle=0,
    CS_ready=1,
    CS_busy=2,
    CS_done=3,
    CS_managed=4,
    CS_done_eos=5,
    CS_skip=6,
    CS_eos=7
    };

enum Subtask_Status {
   STS_new=0, // 000 activated
   STS_pending=1, // 001 means being parsed or waiting for execution I guess
   STS_processing=2, // 010 being executed
   STS_processed=3, // 011 execution finished
   STS_cleanup=4, // 100 should be 'cleaned up'?
   STS_blocked=5, // 101
   STS_inactive=6, // 110 I'll use this for recursion where we reclaim addresses but don't remove the subtask
   STS_deleted=7, // 111 purely for HW mem management: indicates that the subtask at a particular address is deleted
   STS_skip=8,
   STS_eos=9
    };

// If error=4 and cleared=3, we need 2 bits for the actual Data_Status, another bit for error.
// As memory is at least byte-addressable and most likely 32-bit, we have plenty of bits left for
// other information regarding the storage. So we use a bitmask an have e.g. 2 bits for ACK support
// and 2 bits for streaming/fifo support
enum Data_Status {
   DS_absent=0, // 00
   DS_present=1, // 01
   DS_requested=2,  // 10
   DS_eos=3, // 11 EOS is a special case of present
   DS_cleared=4, // 100 does not fit into 2 bits! So cleared becomes absent, should be OK
   DS_error=5 // 101 does not fit into 2 bits!
    };


uint dispatch_reference(uint idx, uint st, bytecode ref_symbol, __global SubtaskRecord *rec, __global uint *data, __global packet *q, uint nservicenodes) {
	uint dest_snid=symbol_get_SNId(ref_symbol);

	Word nref_symbol=symbol_set_quoted(ref_symbol,0);
	// Change subtask status, args etc

	subt_rec_set_arg(rec,idx,nref_symbol);
	subt_rec_incr_nargs_absent(rec);
	subt_rec_set_subt_status(rec,STS_blocked);

	Word ncalled_as=subt_rec_get_called_as(rec);
	uint return_to=symbol_get_SNId(ncalled_as);

	// change subtask status, required for core_control
	subt_rec_set_subt_status(rec,STS_processing);

	// arg status is "requested" for the corresponding argument
	// NEEDED?
	subt_rec_set_arg_status(rec,idx,DS_requested);
	 // change Core status
	uint core_status=CS_managed;
	// "address" is the address of the current core
	Word var_label = symbol_set_subtask(ref_symbol,address);
	var_label = symbol_set_name(var_label,return_to);
	Word subtask_argpos=get_arg_value(idx, rec, data);
	subtask_argpos=symbol_set_subtask(subtask_argpos,st);
	subtask_argpos=symbol_set_task(subtask_argpos,idx);
	
	packet ref_packet = pkt_create(P_reference, return_to, idx, st, (uint)ref_symbol);
	q_write(ref_packet, dest_snid,return_to, q, nservicenodes);
	return core_status;

 } // END of dispatch_reference()
