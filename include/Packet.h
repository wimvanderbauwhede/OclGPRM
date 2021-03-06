#ifndef _PACKET_H_
#define _PACKET_H_

/* Contains the definitions and functions necessary for creating the initial
   reference packet in the host program. Replicates the same functionality available
   in the kernel program. */
#ifdef OSX
#include <cl.hpp>
#else
#include <CL/cl.hpp>
#endif

#define _IN_HOST

#include "SharedTypes.h"
struct packet {
    int x;
    int y;
};
packet pkt_base_init();
packet pkt_create(uint type, uint source, uint arg, uint sub, uint payload);
void pkt_set_type(packet *p, uint type);
void pkt_set_source(packet *p, uint source);
void pkt_set_arg_pos(packet *p, uint arg);
void pkt_set_sub(packet *p, uint sub);
void pkt_set_payload_type(packet *p, uint ptype);
void pkt_set_payload(packet *p, uint payload);

#endif /* _PACKET_H_ */

