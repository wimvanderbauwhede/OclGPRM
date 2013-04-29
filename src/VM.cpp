#define __CL_ENABLE_EXCEPTIONS
#define _IN_HOST
//#define OCLDBG
//#define OLD

#ifdef OSX
#include <cl.hpp>
#else
#include <CL/cl.hpp>
#endif

#include <unistd.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <deque>
#include <sstream>

#include "DeviceInfo.h"
#include "SharedMacros.h"
#include "SharedTypes.h"
#include "Packet.h"
#include "UserData.h"
#include "timing.h"

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



void toggleState(cl::CommandQueue& commandQueue, cl::Buffer& stateBuffer, int *state);
void createSubtask(SubtaskTable*, uint);
SubtaskTable *createSubt();
void validateArguments(int argc);
std::deque<bytecode> readBytecode(char *bytecodeFile);
std::deque< std::deque<bytecode> > words2Packets(std::deque<bytecode>& bytecodeWords);

int main(int argc, char **argv) {
  validateArguments(argc);//the host should truncate it to nunits!

  std::vector<cl::Platform> platforms;
  std::vector<cl::Device> devices;
  cl::Device device;
  cl::Program program;
  
  DeviceInfo deviceInfo;
  
  try {
    /* Create a vector of available platforms. */
    cl::Platform::get(&platforms);
    
    /* Create a vector of available devices */
    try {
#ifndef GPU              
      /* Use CPU */
      platforms[1].getDevices(CL_DEVICE_TYPE_CPU, &devices);
#else      
      /* Use GPU */
      platforms[0].getDevices(CL_DEVICE_TYPE_GPU, &devices);
#endif      
    } catch (cl::Error error) {
      platforms[0].getDevices(CL_DEVICE_TYPE_CPU, &devices);
    }
    
    /* Create a platform context for the available devices. */
    cl::Context context(devices);
    
    /* Use the first available device. */
    device = devices[0];
    
    /* Get the global memory size (in bytes) of the device. */
    // long globalMemSize = deviceInfo.global_mem_size(device);

    /* Get the max memory allocation size (in bytes) of the device */
    long maxGlobalAlloc = deviceInfo.global_mem_max_alloc_size(device);
#ifdef OCLDBG
	unsigned int maxComputeUnits = deviceInfo.max_compute_units(device);
#endif    
    /* Create a command queue for the device. */
    cl::CommandQueue commandQueue = cl::CommandQueue(context, device);

    /* Read the kernel program source. */
    std::ifstream kernelSourceFile(KERNEL_FILE);
    std::string kernelSource(std::istreambuf_iterator<char>(kernelSourceFile), (std::istreambuf_iterator<char>()));
    cl::Program::Sources source(1, std::make_pair(kernelSource.c_str(), kernelSource.length() + 1));

    /* Create a program in the context using the kernel source code. */
    program = cl::Program(context, source);
    
    /* Build the program for the available devices. */
    // WV: ORIG: program.build(devices, KERNEL_BUILD_OPTIONS);
    program.build(devices, KERNEL_OPTS);
    
    /* Create the kernel. */
    cl::Kernel kernel(program, KERNEL_NAME);
    
    /* How many services are to be used? */
    unsigned int nServices = 1;
    std::stringstream(argv[2]) >> nServices;
#ifdef OCLDBG
	std::cout << "INFO: Device is " << ((deviceInfo.is_little_endian(device))? "LITTLE" : "BIG") <<"-endian\n";	
    if (nServices < maxComputeUnits) {
		std::cout << "INFO: Number of services (requested "<< nServices<<") is smaller than the number of compute units, "<<maxComputeUnits <<". The device will be under-utilised\n";
//		nServices = maxComputeUnits;
	} else if (nServices > maxComputeUnits) {
		std::cout << "INFO: Number of services (requested "<< nServices<<") is larger than the number of compute units, "<<maxComputeUnits <<". This will work fine but you could try and let GPRM schedule \n";
	}
#endif	
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
#ifdef OLD    
    /* Populate the code store. */
	
    for (std::deque< std::deque<bytecode> >::iterator iterP = packets.begin(); iterP != packets.end(); iterP++) {
      std::deque<bytecode> packet = *iterP;
      for (std::deque<bytecode>::iterator iterW = packet.begin(); iterW != packet.end(); iterW++) {
        bytecode word = *iterW;
        int packetN = iterP - packets.begin(); // Which packet?
        int wordN = iterW - packet.begin(); // Which word?

        codeStore[((packetN + 1) * MAX_BYTECODE_SZ) + wordN] = word;
		//std::cout << "codeStore["<<((packetN + 1) * MAX_BYTECODE_SZ) + wordN<<"] = "<<word<<";\n";
      }
    }
#else	
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
#endif

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

    /* The data store */
    //cl_uint *data;// = new cl_uint[dataSize];
    
    /* Users Write/allocate memory on the data buffer. */
    // WV: in many cases data depends on nServices
    //unsigned int dataSize = populateData(data,nServices);
    unsigned int dataSize = 0;
    cl_uint* data = populateData(&dataSize,nServices);
#ifdef OCLDBG
    std::cout << "Size of data[]: "<< dataSize << " words\n";
    std::cout << "Size of data[]: "<< data[data[0]+1] << " words\n";
	std::cout << data[0] << "\n";
	std::cout << data[1] << "\n";
	std::cout << data[2] << "\n";
	std::cout << data[3] << "\n";
	std::cout << data[4] << "\n";
#endif
    if (
            maxGlobalAlloc 
            - 2* qBufSize * sizeof(packet) 
            - sizeof(int)
            - CODE_STORE_SIZE * MAX_BYTECODE_SZ * sizeof(bytecode)
            - sizeof(SubtaskTable)*nServices            
			-dataSize
			< 0
            ) {
		std::cout << "ERROR: NOT ENOUGH MEMORY\n";
		exit(1);
	}

#ifdef OCLDBG
    std::cout << "Buffers allocated in data[]: "<< dataSize << " words\n";
#endif    
    /* Create memory buffers on the device. */
    cl::Buffer qBuffer = cl::Buffer(context, CL_MEM_READ_WRITE, qBufSize * sizeof(packet));
//    commandQueue.enqueueWriteBuffer(qBuffer, CL_TRUE, 0, qBufSize * sizeof(packet), queues);

    cl::Buffer rqBuffer = cl::Buffer(context, CL_MEM_READ_WRITE, qBufSize * sizeof(packet));
    commandQueue.enqueueWriteBuffer(rqBuffer, CL_TRUE, 0, qBufSize * sizeof(packet), readQueues);
    
    cl::Buffer stateBuffer = cl::Buffer(context, CL_MEM_READ_WRITE, sizeof(int));
//    commandQueue.enqueueWriteBuffer(stateBuffer, CL_TRUE, 0, sizeof(int), state);
    
    cl::Buffer codeStoreBuffer = cl::Buffer(context, CL_MEM_READ_ONLY, CODE_STORE_SIZE * MAX_BYTECODE_SZ * sizeof(bytecode));
    commandQueue.enqueueWriteBuffer(codeStoreBuffer, CL_TRUE, 0, CODE_STORE_SIZE * MAX_BYTECODE_SZ * sizeof(bytecode), codeStore);
    
    cl::Buffer subtaskTableBuffer = cl::Buffer(context, CL_MEM_READ_WRITE, sizeof(SubtaskTable)*nServices);
    commandQueue.enqueueWriteBuffer(subtaskTableBuffer, CL_TRUE, 0, sizeof(SubtaskTable)*nServices, subtaskTables);

    cl::Buffer dataBuffer = cl::Buffer(context, CL_MEM_READ_WRITE, dataSize * sizeof(cl_uint));
    
    /* Set kernel arguments. */
    kernel.setArg(0, qBuffer);
    kernel.setArg(1, rqBuffer);
    kernel.setArg(2, nServices);
    kernel.setArg(3, stateBuffer);
    kernel.setArg(4, codeStoreBuffer);
    kernel.setArg(5, subtaskTableBuffer);
    kernel.setArg(6, dataBuffer);
    
    /* Set the NDRange. */
    //ORIG cl::NDRange global(nServices), local(nServices);
    cl::NDRange global(nServices*NTH), local(NTH); // means nServices compute units, one thread per unit
#if NRUNS > 1    
    for (unsigned int nruns=1;nruns<=NRUNS; nruns++) {
#endif        
    double t_start=wsecond();
    commandQueue.enqueueWriteBuffer(dataBuffer, CL_TRUE, 0, dataSize * sizeof(cl_uint), data);
    commandQueue.enqueueWriteBuffer(qBuffer, CL_TRUE, 0, qBufSize * sizeof(packet), queues);
    commandQueue.enqueueWriteBuffer(stateBuffer, CL_TRUE, 0, sizeof(int), state);
    
    /* Run the kernel on NDRange until completion. */
	unsigned int iter=1;
    while (*state != COMPLETE ) {
#ifdef OCLDBG	  
	  std::cout << "\n *** CALL #"<< iter <<" TO DEVICE ("<<  (*state==READ?"READ & PARSE":"WRITE")  <<") *** \n";
#endif	  
      commandQueue.enqueueNDRangeKernel(kernel, cl::NullRange, global, local);
      commandQueue.finish();
   
    /* Read the results. */
      // TODO: The smarter thing is to read a sub-buffer of data[]
    commandQueue.enqueueReadBuffer(dataBuffer, CL_TRUE, 0, dataSize * sizeof(cl_uint), data);

#ifdef OCLDBG	  
	// Read the subtask table for debugging
	commandQueue.enqueueReadBuffer(subtaskTableBuffer, CL_TRUE, 0, sizeof(SubtaskTable)*nServices, subtaskTables);
	for (uint kk=0;kk<nServices;kk++) {
	SubtaskTable subtaskTable =  subtaskTables[kk];	
	uint stackpointer = subtaskTable.av_recs[0];
	std::cout << "SUBTASK STACK: " << stackpointer << "\n";
	for (uint jj = 0;jj<stackpointer;jj++) {
	std::cout << "SUBTASK REC "<<jj<<":\n";
	SubtaskRecord c_subt_rec= subtaskTable.recs[jj];
	std::cout << "Opcode: "<< (uint)(c_subt_rec.service_id & 0xFF) << "\n";
	std::cout << "Code addr: "<<(uint)c_subt_rec.code_addr << "\n";
	uint nargs =  (uint)c_subt_rec.nargs ;
	std::cout << "NArgs: "<<nargs << "\n";
	for (uint ii=0;ii<nargs;ii++) {
	std::cout << (uint)(c_subt_rec.args[ii])<< "\n";
	}
	}
	}

	// To debug, we need to read the queues and display their content
	commandQueue.enqueueReadBuffer(qBuffer, CL_TRUE, 0, qBufSize * sizeof(packet), queues);
	commandQueue.enqueueReadBuffer(rqBuffer, CL_TRUE, 0, qBufSize * sizeof(packet), readQueues);
	// loop over all services
   // int nQueues = nServices * nServices;
    //int qBufSize = (nQueues * MAX_BYTECODE_SZ) + nQueues;			
    
	for (unsigned int ii=0;ii<nServices;ii++) {
	// for every service, loop over all queues
        std::cout << "\nBuffers for work group " << ii << "\n";

		for (unsigned int jj=0;jj<nServices;jj++) {
			for (unsigned int kk=0;kk< MAX_BYTECODE_SZ ;kk++) {

            packet ppr=readQueues[nQueues+ii*nServices * MAX_BYTECODE_SZ + jj * MAX_BYTECODE_SZ + kk] ;
            packet ppw=queues[nQueues+ii*nServices * MAX_BYTECODE_SZ + jj * MAX_BYTECODE_SZ + kk] ;
            if ( (ppr.x!=0 && ppr.y !=0) || (ppw.x!=0 && ppw.y!=0)) {
                    unsigned int rsubtask = (ppr.x >> 14) & 0x3FF;
                    unsigned int rtype = ppr.x & 0x3;
                    unsigned int rsource = (ppr.x >> 2) & 0xFF;
                    unsigned int rarg_pos = (ppr.x >>10) & 0xF;
                    unsigned int rpayload= ppr.y; 
                    unsigned int wsubtask = (ppw.x >> 14) & 0x3FF;
                    unsigned int wtype = ppw.x & 0x3;
                    unsigned int wsource = (ppw.x >> 2) & 0xFF;
                    unsigned int warg_pos = (ppw.x >>10) & 0xF;
                    unsigned int wpayload= ppw.y ;
            std::cout <<jj <<"\t"<<kk<<": " << rsubtask <<":"<< rarg_pos <<":"<<rsource<<":"<<rtype  <<";"<<rpayload<<"\t";
            std::cout  << wsubtask <<":"<< warg_pos<<":"<<wsource<<":"<<wtype  <<";"<<wpayload<<"\n";
                        }
			}		
            
		}
	}
#endif

        toggleState(commandQueue, stateBuffer, state);
        iter++;
    }
    commandQueue.finish();
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
    const unsigned int mSize = WIDTH*WIDTH;
    const unsigned int mWidth = WIDTH;

    cl_int* mCref=new cl_int[mSize];
    cl_int mArow[mWidth];
unsigned int A0=data[1];
unsigned int B0=data[2];
unsigned int C0=data[3];
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
		int diff = data[C0+i] - mCref[i];
        if(diff==0) { // 2**-20
            correct++;
        } else {
        	nerrors++;
        	if (nerrors>max_nerrors) break;
        }
    }

    if (nerrors==0) {
	    std::cout << "All "<< correct <<" correct!\n";
    }  else {
	    std::cout << "#errors: "<<nerrors<<"\n";
	    std::cout << "Computed '"<<correct<<"/"<<mSize<<"' correct values!\n";
    }

    delete[] mCref;

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
  } catch (cl::Error error) {
    std::cout << "EXCEPTION: " << error.what() << " [" << error.err() << "]" << std::endl;
    std::cout << program.getBuildInfo<CL_PROGRAM_BUILD_LOG>(device) << std::endl;
  }
  
  return 0;
}

/* Toggles the state between read and write until it is set to complete by the virtual machine. */
void toggleState(cl::CommandQueue& commandQueue, cl::Buffer& stateBuffer, int *state) {
  commandQueue.enqueueReadBuffer(stateBuffer, CL_TRUE, 0, sizeof(int), state);
  if (*state == COMPLETE) return;
  *state = (*state == WRITE) ? READ : WRITE;
  commandQueue.enqueueWriteBuffer(stateBuffer, CL_TRUE, 0, sizeof(int), state);
  commandQueue.finish();
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
