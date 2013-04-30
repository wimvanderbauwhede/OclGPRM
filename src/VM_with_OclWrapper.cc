#define __CL_ENABLE_EXCEPTIONS
#include <OclWrapper.h>
#define _IN_HOST

//#define OCLDBG
#define USE_SUBBUFFER


#include <unistd.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <deque>
#include <sstream>

//#include "DeviceInfo.h"
#include "SharedMacros.h"
#include "SharedTypes.h"
#include "Packet.h"
#include "timing.h"

#include "UserData.h"

const unsigned int mSize = WIDTH*WIDTH;

const char *KERNEL_NAME = "vm";
const char *KERNEL_FILE = "kernels/VM.cl";
// WV: use KERNEL_OPTS from build system
//const char *KERNEL_BUILD_OPTIONS = "-I/Users/wim/Git/OclGPRM/include/";// -I./include";

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



void toggleState(OclWrapper& ocl, cl::Buffer& stateBuffer, int *state);
void createSubtask(SubtaskTable*, uint);
SubtaskTable *createSubt();
void validateArguments(int argc);
std::deque<bytecode> readBytecode(char *bytecodeFile);
std::deque< std::deque<bytecode> > words2Packets(std::deque<bytecode>& bytecodeWords);

int main(int argc, char **argv) {
  validateArguments(argc);//the host should truncate it to nunits!

#ifdef VERBOSE
    std::cout << "Creating OclWrapper\n";
#endif
    OclWrapper ocl;
//     try {

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
    packet *queues = new packet[qBufSize];
    packet *readQueues = new packet[qBufSize];
    
    /* Initialise queue elements to zero. */
    for (int i = 0; i < qBufSize; i++) {
      queues[i].x = 0;
      queues[i].y = 0;
      readQueues[i].x = 0;
      readQueues[i].y = 0;
    }

    /* Which stage of the READ/WRITE cycle are we in? */
    int *state = new int;
    *state = READ;

    /* The code store stores bytecode in MAX_BYTECODE_SZ chunks. */
    bytecode *codeStore = new bytecode[CODE_STORE_SIZE * MAX_BYTECODE_SZ];
    
    /* Read the bytecode from file. */
    std::deque<bytecode> bytecodeWords = readBytecode(argv[1]);
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
		//		std::cout << "root address: "<<root_address<<"\n";
			}
		}
		if (packet_type == P_code && word_count>2) {					
			codeStore[ code_address * MAX_BYTECODE_SZ + word_count - 3] = word;
     	//	std::cout << "codeStore["<<code_address * MAX_BYTECODE_SZ + word_count - 3<<"] = "<<word<<";\n";
			
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
    SubtaskTable* subtaskTables = new SubtaskTable[nServices];
	for (uint ii=0;ii<nServices;ii++) {
		createSubtask(subtaskTables,ii);//]= createSubt();
	}

    unsigned int dataSize = 0;
    cl_uint* data = populateData(&dataSize,nServices);
// allocating data for the result
#ifdef USE_SUBBUFFER
    cl_uint* mC = new cl_uint[mSize];
#endif
   
#ifdef OCLDBG
    std::cout << "Buffers allocated in data[]: "<< dataSize << " words\n";
#endif    
    /* Create memory buffers on the device. */
    cl::Buffer qBuffer = ocl.makeReadBuffer( qBufSize * sizeof(packet));
    cl::Buffer rqBuffer = ocl.makeReadBuffer( qBufSize * sizeof(packet));
    ocl.writeBuffer(rqBuffer, qBufSize * sizeof(packet), readQueues);
    
    cl::Buffer stateBuffer = ocl.makeReadWriteBuffer( sizeof(int));
    
    cl::Buffer codeStoreBuffer = ocl.makeReadBuffer(CODE_STORE_SIZE * MAX_BYTECODE_SZ * sizeof(bytecode));
    ocl.writeBuffer(codeStoreBuffer, CODE_STORE_SIZE * MAX_BYTECODE_SZ * sizeof(bytecode), codeStore);
    
    cl::Buffer subtaskTableBuffer = ocl.makeReadBuffer(sizeof(SubtaskTable)*nServices);
    ocl.writeBuffer(subtaskTableBuffer, sizeof(SubtaskTable)*nServices, subtaskTables);

    cl::Buffer dataBuffer = ocl.makeReadWriteBuffer(dataSize * sizeof(cl_uint));
    
       
    /* Set the NDRange. */
     const int& nServices_r=nServices;

    cl::NDRange global(nServices*NTH), local(NTH); // means nServices compute units, one thread per unit
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
    ocl.readBuffer(dataBuffer, true, data[3]*sizeof(cl_uint), mSize * sizeof(cl_uint), mC);
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
#if SELECT==1
#elif SELECT==2
#if REF != 0
    const unsigned int mWidth = WIDTH;

    cl_int* mCref=new cl_int[mSize];
    cl_int mArow[mWidth];
unsigned int A0=data[1];
unsigned int B0=data[2];
#ifndef USE_SUBBUFFER
unsigned int C0=data[3];
#endif
/*
std::cout << data[0] <<"\n";
std::cout << data[1] <<"\n";
std::cout << data[2] <<"\n";
std::cout << data[3] <<"\n";
std::cout << data[4] <<"\n";
*/  
    for (uint i = 0; i<mWidth; i++) {
    	// This is an attempt to put a row in the cache.
    	// It sometimes works, giving a speed-up of 5x
    	for (uint j = 0; j<mWidth; j++) {
    		mArow[j]=data[A0+i*mWidth+j];
    	}
        for (uint j = 0; j<mWidth; j++) {
            cl_int elt=0.0;
            for (uint k = 0; k<mWidth; k++) {
            	elt+=mArow[k]*data[B0+k*mWidth+j];
            }
            mCref[i*mWidth+j]=elt;
        }
    }
// now compare data[C0+...] to mCref
  unsigned int correct=0;               // number of correct results returned
    int nerrors=0;
    int max_nerrors=mSize;
    for (unsigned int i = 0; i < mSize; i++) {
#ifdef USE_SUBBUFFER
		int diff = mC[i] - mCref[i];
#else
		int diff = data[C0+i] - mCref[i];
#endif
        if(diff==0) { // 2**-20
            correct++;
        } else {
        	nerrors++;
        	if (nerrors>max_nerrors) break;
        }
    }
#ifdef VERBOSE
    if (nerrors==0) {
	    std::cout << "All "<< correct <<" correct!\n";
    }  else {
	    std::cout << "#errors: "<<nerrors<<"\n";
	    std::cout << "Computed '"<<correct<<"/"<<mSize<<"' correct values!\n";
    }
#else
	    std::cout << "\t"<< correct <<"\t";
#endif
    delete[] mCref;
#endif
#elif SELECT==4    
    // Print resulting matrix from example 4. MODIFY ME!!
    std::cout << ((int) data[data[6]]) << " " << ((int) data[data[6] + 1]) << std::endl;
    std::cout << ((int) data[data[6] + 2]) << " " << ((int) data[data[6] + 3]) << std::endl;


#elif SELECT==5
#if VERBOSE==1    
		std::cout << "node\tglobal\tlocal\tgroup\tidx\n";

	for (unsigned int ii=0;ii<4*nServices*NTH;ii+=4) {
		std::cout << ii/4 << "\t";
		std::cout << data[data[1]+ii+0] << "\t";
        std::cout << data[data[1]+ii+1] << "\t";
		std::cout << data[data[1]+ii+2] << "\t";
		std::cout << data[data[1]+ii+3] << "\n";
	}
#endif    
#endif	

    /* Cleanup */
    delete[] queues;
    delete[] readQueues;
    delete[] codeStore;
    delete[] data;
    delete[] subtaskTables;
    delete state;
/*    
  } catch (cl::Error error) {
    std::cout << "EXCEPTION: " << error.what() << " [" << error.err() << "]" << std::endl;
    std::cout << ocl.program_p->getBuildInfo<CL_PROGRAM_BUILD_LOG>(device) << std::endl;
  }
  */
  return 0;
} // END of main()

/* Toggles the state between read and write until it is set to complete by the virtual machine. */
void toggleState(OclWrapper& ocl, cl::Buffer& stateBuffer, int *state) {
  ocl.readBuffer(stateBuffer, sizeof(int), state);
  if (*state == COMPLETE) return;
  *state = (*state == WRITE) ? READ : WRITE;
  ocl.writeBuffer(stateBuffer, sizeof(int), state);
}

/* Create and initialise a subtask table. */
SubtaskTable *createSubt() {
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
void createSubtask(SubtaskTable * table, uint idx) {

    table[idx].av_recs[0] = 1; // The first element is the top of stack index.

    /* Populate the stack with the available records in the subtask table. */
    for (int i = 1; i < SUBT_SIZE + 1; i++) {
      table[idx].av_recs[i] = i - 1;
    }
}



/* Validate the command line arguments. */
void validateArguments(int argc) {
  if (argc < NARGS) {
    std::cout << "Usage: ./vm [bytecode-file] [n-services]" << std::endl;
    exit(EXIT_FAILURE);
  }
}

/* Read the bytecode from file and place it in a queue of words. */
std::deque<bytecode> readBytecode(char *bytecodeFile) {
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
std::deque< std::deque<bytecode> > words2Packets(std::deque<bytecode>& bytecodeWords) {
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
#ifndef OLD
	  packet.push_back(headerWord); // WV
#endif	  
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
