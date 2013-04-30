#include "../include/OclGPRM.h"

#define __CL_ENABLE_EXCEPTIONS

#define _IN_HOST

//#define OCLDBG
//#define USE_SUBBUFFER

#include <unistd.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <deque>
#include <sstream>

#include "SharedMacros.h"
#include "SharedTypes.h"
#include "Packet.h"
#include "timing.h"

OclGPRM::OclGPRM (const char *bytecodeFile) : dataSize(1 + BUFFER_PTR_FILE_SZ + REGISTER_FILE_SZ) {

const char *KERNEL_NAME = "vm";
const char *KERNEL_FILE = "kernels/VM.cl";

const int NARGS = 3;
const int NPACKET_SHIFT = ((NBYTES * 8) - 16);

// From GPRM ServiceConfiguration.h
const unsigned int P_code = 2;
const unsigned int P_reference = 3;
const unsigned int FS_Length = 32;
const uint64_t F_Length = 0x0000ffff00000000;
const unsigned int FS_Packet_type=56;
const uint64_t FW_Packet_type=0xFFULL;
const unsigned int FS_CodeAddress = 32 ;
const uint64_t FW_CodeAddress=0x3FFULL;

#ifdef VERBOSE
    std::cout << "Creating OclWrapper\n";
#endif
    unsigned int nunits= NGROUPS;
        if (nunits==0) {
                nunits = ocl.getMaxComputeUnits();
        }
#ifdef OCLDBG
	unsigned int maxComputeUnits = ocl.getMaxComputeUnits();
#endif           

#ifdef VERBOSE
    std::cout << "Number of work groups = "<< nunits <<"\n";
    std::cout << "Compiling Kernel\n";
#endif
    ocl.loadKernel(KERNEL_FILE,KERNEL_NAME, KERNEL_OPTS);

#ifdef VERBOSE
    std::cout << "Creating Buffers\n";
#endif
 
      
    /* How many services are to be used? */
    unsigned int nServices = nunits;
    /* Calculate the number of queues we need. */
    int nQueues = nServices * nServices;
    
    /* Calculate the memory required to store the queues. The first nQueue packets are used to store
       information regarding the queues themselves (head index, tail index and last operation performed). */
    int qBufSize = (nQueues * MAX_BYTECODE_SZ) + nQueues;
    
    /* Allocate memory for the queues. */
    queues = new packet[qBufSize];
    readQueues = new packet[qBufSize];
  
    /* Initialise queue elements to zero. */
    for (int i = 0; i < qBufSize; i++) {
      queues[i].x = 0;
      queues[i].y = 0;
      readQueues[i].x = 0;
      readQueues[i].y = 0;
    }

    /* Which stage of the READ/WRITE cycle are we in? */
    state = new int;
    *state = READ;

    /* The code store stores bytecode in MAX_BYTECODE_SZ chunks. */
    codeStore = new bytecode[CODE_STORE_SIZE * MAX_BYTECODE_SZ];
    
    /* Read the bytecode from file. */
    std::deque<bytecode> bytecodeWords = readBytecode(bytecodeFile);
    std::deque< std::deque<bytecode> > packets = words2Packets(bytecodeWords);
 /* Populate the code store -- WV */
	unsigned int root_address = 1;
    for (std::deque< std::deque<bytecode> >::iterator iterP = packets.begin(); iterP != packets.end(); iterP++) {
      std::deque<bytecode> packet = *iterP;
	  unsigned int word_count=0;
	  unsigned int packet_type=0;
	  unsigned int code_address=0;
      for (std::deque<bytecode>::iterator iterW = packet.begin(); iterW != packet.end(); iterW++) {
        bytecode word = *iterW;
		if (word_count==0) {
			packet_type = (word>>FS_Packet_type) & FW_Packet_type;
		}
		if (word_count==2) {
			code_address = (word >> FS_CodeAddress) & FW_CodeAddress;
			if (packet_type == P_reference && word_count==2) {
				root_address=code_address;
			}
		}
		if (packet_type == P_code && word_count>2) {					
			codeStore[ code_address * MAX_BYTECODE_SZ + word_count - 3] = word;
		}
		word_count++;
      }
    }
    
     /* Create initial packet. */
	// WV: note that RETURN_ADDRESS is used as the final destination
	// This assumes the arg_pos and subtask are both 0 and the payload is 1
	// i.e. the code at address 1 will be activated, 
	// the result is destined for subtask 0, arg_pos 0, which of course do not exists
	// This is a bit weak, because the code address might not be 1
	packet p = pkt_create(REFERENCE, RETURN_ADDRESS, 0, 0, root_address);
    queues[nQueues] = p;   // Initial packet.
    queues[0].x = 1 << 16; // Tail index is 1.
    queues[0].y = WRITE;   // Last operation is write.

    /* The subtask table. */
    subtaskTables = new SubtaskTable[nServices];
	for (uint ii=0;ii<nServices;ii++) {
		createSubtask(subtaskTables,ii);
	}

    data_p = new std::vector<unsigned int>(0,1+BUFFER_PTR_FILE_SZ + REGISTER_FILE_SZ);
}
//-------------------------------------------------------------------------------- 
OclGPRM::run(unsigned int nServices, unsigned int num_threads) {
    /* Create memory buffers on the device. */
       
    cl::Buffer rqBuffer = ocl.makeReadBuffer( qBufSize * sizeof(packet));
    ocl.writeBuffer(rqBuffer, qBufSize * sizeof(packet), readQueues);
           
    cl::Buffer codeStoreBuffer = ocl.makeReadBuffer(CODE_STORE_SIZE * MAX_BYTECODE_SZ * sizeof(bytecode));
    ocl.writeBuffer(codeStoreBuffer, CODE_STORE_SIZE * MAX_BYTECODE_SZ * sizeof(bytecode), codeStore);
    
    cl::Buffer subtaskTableBuffer = ocl.makeReadBuffer(sizeof(SubtaskTable)*nServices);
    ocl.writeBuffer(subtaskTableBuffer, sizeof(SubtaskTable)*nServices, subtaskTables);

    dataBuffer = ocl.makeReadWriteBuffer(dataSize * sizeof(cl_uint));
    cl::Buffer qBuffer = ocl.makeReadBuffer( qBufSize * sizeof(packet));
    cl::Buffer stateBuffer = ocl.makeReadWriteBuffer( sizeof(int));
 
  /* Set the NDRange. */
     const int& nServices_r=nServices;

    cl::NDRange global(nServices*num_threads), local(num_threads); // means nServices compute units, one thread per unit

#if NRUNS > 1    
    for (unsigned int nruns=1;nruns<=NRUNS; nruns++) {
#endif        
    double t_start=wsecond();

    ocl.writeBuffer(dataBuffer, dataSize * sizeof(cl_uint), data);
    ocl.writeBuffer(qBuffer, qBufSize * sizeof(packet), queues);
    ocl.writeBuffer(stateBuffer, sizeof(int), state);
    
    /* Run the kernel on NDRange until completion. */
	unsigned int iter=1;
    while (*state != COMPLETE ) {
#ifdef OCLDBG	  
	  std::cout << "\n *** CALL #"<< iter <<" TO DEVICE ("<<  (*state==READ?"READ & PARSE":"WRITE")  <<") *** \n";
#endif	  
      ocl.enqueueNDRange(global, local);

      ocl.runKernel(qBuffer, rqBuffer,nServices_r, stateBuffer, codeStoreBuffer, subtaskTableBuffer,dataBuffer   ).wait();
      toggleState(ocl, stateBuffer, state);
      iter++;
    }
    /* Read the results. */
      // TODO: The smarter thing is to read a sub-buffer of data[]

#ifdef USE_SUBBUFFER
    ocl.readBuffer(dataBuffer, data[3]*sizeof(cl_uint), mSize * sizeof(cl_uint), mC);
#else
    ocl.readBuffer(dataBuffer, dataSize * sizeof(cl_uint), data);
#endif	
    double t_stop=wsecond();
    double t_elapsed = t_stop - t_start;
#if VERBOSE==1    
    std::cout << "Finished in "<<t_elapsed/1000<<" s\n";
#else
    std::cout << "\t"<<t_elapsed;

#endif 
#if NRUNS > 1     
    *state = READ;
  } // NRUNS  
#endif

} // END of run()

//-------------------------------------------------------------------------------- 

//  void OclGPRM::loadBytecode(std::string tdc_file_name) { }
//-------------------------------------------------------------------------------- 

unsigned int OclGPRM::allocateBuffer(unsigned int pos, unsigned int bufsize) {
    unsigned int wsize = bufsize/sizeof(unsigned int);
    regs[pos]=wsize;
    data_p->resize(data_p->size()+wsize);
    dataSize = data_p->size(); // in words
}
//-------------------------------------------------------------------------------- 

unsigned int OclGPRM::writeBuffer(unsigned int pos, void* buf, unsigned int bufsize) {
    // Essentially a memcpy into data_p
    unsigned int wsize = bufsize/sizeof(unsigned int);
    std::vector<unsigned int> bufv;
    bufv.reserve(wsize);
    bufv.assign(buf,buf+wsize);

    std::vector<unsigned int>::iterator it = data_p->begin();
    data_p->insert(it+ data_p->at(pos),bufv.begin(), bufv.end());
    return data_p->size();
}

//-------------------------------------------------------------------------------- 

void* OclGPRM::readBuffer(unsigned int pos, unsigned int bufsize) {
    // we read the subbuffer, so we'll need the data buffer as an attribute
    unsigned int * rbuf = new unsigned int[bufsize];
    ocl.readBuffer(dataBuffer, true, data[pos+1]*sizeof(cl_uint), bufsize, rbuf);
    return rbuf;
}



// ----------------------- Private methods ---------------------
/* Toggles the state between read and write until it is set to complete by the virtual machine. */
void OclGPRM::toggleState(OclWrapper& ocl, cl::Buffer& stateBuffer, int *state) {
  ocl.readBuffer(stateBuffer, sizeof(int), state);
  if (*state == COMPLETE) return;
  *state = (*state == WRITE) ? READ : WRITE;
  ocl.writeBuffer(stateBuffer, sizeof(int), state);
}

/* Create and initialise a subtask table. */
SubtaskTable *OclGPRM::createSubt() {
  SubtaskTable* table = new SubtaskTable;

  if (table) {
    table->av_recs[0] = 1; // The first element is the top of stack index.

    /* Populate the stack with the available records in the subtask table. */
    for (int i = 1; i < SUBT_SIZE + 1; i++) {
      table->av_recs[i] = i - 1;
    }
  }

  return table;
}

/* Create and initialise a subtask table. */
void OclGPRM::createSubtask(SubtaskTable * table, uint idx) {

    table[idx].av_recs[0] = 1; // The first element is the top of stack index.

    /* Populate the stack with the available records in the subtask table. */
    for (int i = 1; i < SUBT_SIZE + 1; i++) {
      table[idx].av_recs[i] = i - 1;
    }
}

/* Read the bytecode from file and place it in a queue of words. */
std::deque<bytecode> OclGPRM::readBytecode(char *bytecodeFile) {
  std::ifstream f(bytecodeFile);
  std::deque<bytecode> bytecodeWords;
  
  if (f.is_open()) {
    while (f.good()) {
      bytecode word = 0;
      for (int i = 0; i < NBYTES; i++) {
        unsigned char c = f.get();
        word = (word << NBYTES) + (bytecode)c;
      }
//	  std::cout << "WORD: "<< word << "\t";
//	  std::cout << "VAL: "<< (word & 0xFFFFULL) << "\n";

      bytecodeWords.push_back(word);
    }
  }
  
  return bytecodeWords;
}

/* Group the bytecode words into packets of service calls. */
std::deque< std::deque<bytecode> > OclGPRM::words2Packets(std::deque<bytecode>& bytecodeWords) {
  int nPackets = bytecodeWords.front() >> NPACKET_SHIFT; bytecodeWords.pop_front();
  std::deque< std::deque<bytecode> > packets;
  for (int p = 0; p < nPackets; p++) {
//	  std::cout << "PACKET: "<<p<<"\n";
    std::deque<bytecode> packet;

    int length = 0;
    for (int i = 0; i < 3; i++) {
      bytecode headerWord = bytecodeWords.front();
      bytecodeWords.pop_front();
      if (i == 0) {
        length = (headerWord & F_Length) >> FS_Length;
      }
	  packet.push_back(headerWord); // WV  
    }

    for (int i = 0; i < length; i++) {
      bytecode payloadWord = bytecodeWords.front();
//	  std::cout << "SYM: "<< payloadWord << "\t";
//	  std::cout << "VAL: "<< (payloadWord & 0xFFFFFFFFULL) << "\n";

      bytecodeWords.pop_front();	  
      packet.push_back(payloadWord);
    }

    packets.push_back(packet);
  }
  
  return packets;
}

