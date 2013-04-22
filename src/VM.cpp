#define __CL_ENABLE_EXCEPTIONS
#define _IN_HOST

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

const char *KERNEL_NAME = "vm";
const char *KERNEL_FILE = "kernels/VM.cl";
// WV: use KERNEL_OPTS from build system
//const char *KERNEL_BUILD_OPTIONS = "-I/Users/wim/Git/OclGPRM/include/";// -I./include";


const int NARGS = 3;
const int NPACKET_SHIFT = ((NBYTES * 8) - 16);
const int FS_Length = 32;
const long F_Length = 0x0000ffff00000000;

void toggleState(cl::CommandQueue& commandQueue, cl::Buffer& stateBuffer, int *state);
subt *createSubt();
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
      platforms[0].getDevices(CL_DEVICE_TYPE_CPU, &devices);
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
	unsigned int maxComputeUnits = deviceInfo.max_compute_units(device);
    
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
	std::cout << "INFO: Device is " << ((deviceInfo.is_little_endian(device))? "LITTLE" : "BIG") <<"-endian\n";
    if (nServices < maxComputeUnits) {
		std::cout << "INFO: Number of services (requested "<< nServices<<") is smaller than the number of compute units, "<<maxComputeUnits <<". The device will be under-utilised\n";
//		nServices = maxComputeUnits;
	} else if (nServices > maxComputeUnits) {
		std::cout << "INFO: Number of services (requested "<< nServices<<") is larger than the number of compute units, "<<maxComputeUnits <<". This will work fine but you could try and let GPRM schedule \n";
	}
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
    
    /* Populate the code store. */
    for (std::deque< std::deque<bytecode> >::iterator iterP = packets.begin(); iterP != packets.end(); iterP++) {
      std::deque<bytecode> packet = *iterP;
      for (std::deque<bytecode>::iterator iterW = packet.begin(); iterW != packet.end(); iterW++) {
        bytecode word = *iterW;
        int packetN = iterP - packets.begin(); // Which packet?
        int wordN = iterW - packet.begin(); // Which word?
        codeStore[((packetN + 1) * MAX_BYTECODE_SZ) + wordN] = word;
      }
    }
    
    /* Create initial packet. */
	// WV: note that RETURN_ADDRESS is used as the final destination
    packet p = pkt_create(REFERENCE, RETURN_ADDRESS, 0, 0, 1);
    queues[nQueues] = p;   // Initial packet.
    queues[0].x = 1 << 16; // Tail index is 1.
    queues[0].y = WRITE;   // Last operation is write.

    /* The subtask table. */
    subt *subtaskTable = createSubt();
    // WV: The data buffer covers the whole memory apart from the memory used by the VM's data structures
    unsigned long dataSize = (
            maxGlobalAlloc 
            - 2* qBufSize * sizeof(packet) 
            - sizeof(int)
            - CODE_STORE_SIZE * MAX_BYTECODE_SZ * sizeof(bytecode)
            - sizeof(subt)            
            ) / sizeof(cl_uint);// 4; // How many 32-bit integers?
    std::cout << "Size of data[]: "<< dataSize << " words\n";
    /* The data store */
    cl_uint *data = new cl_uint[dataSize];
    
    /* Users Write/allocate memory on the data buffer. */
    // WV: in many cases data depends on nServices
    unsigned int allocated = populateData(data,nServices);
    std::cout << "Buffers allocated in data[]: "<< allocated << " words\n";
    
    /* Create memory buffers on the device. */
    cl::Buffer qBuffer = cl::Buffer(context, CL_MEM_READ_WRITE, qBufSize * sizeof(packet));
    commandQueue.enqueueWriteBuffer(qBuffer, CL_TRUE, 0, qBufSize * sizeof(packet), queues);

    cl::Buffer rqBuffer = cl::Buffer(context, CL_MEM_READ_WRITE, qBufSize * sizeof(packet));
    commandQueue.enqueueWriteBuffer(rqBuffer, CL_TRUE, 0, qBufSize * sizeof(packet), readQueues);
    
    cl::Buffer stateBuffer = cl::Buffer(context, CL_MEM_READ_WRITE, sizeof(int));
    commandQueue.enqueueWriteBuffer(stateBuffer, CL_TRUE, 0, sizeof(int), state);
    
    cl::Buffer codeStoreBuffer = cl::Buffer(context, CL_MEM_READ_ONLY, CODE_STORE_SIZE * MAX_BYTECODE_SZ * sizeof(bytecode));
    commandQueue.enqueueWriteBuffer(codeStoreBuffer, CL_TRUE, 0, CODE_STORE_SIZE * MAX_BYTECODE_SZ * sizeof(bytecode), codeStore);
    
    cl::Buffer subtaskTableBuffer = cl::Buffer(context, CL_MEM_READ_WRITE, sizeof(subt));
    commandQueue.enqueueWriteBuffer(subtaskTableBuffer, CL_TRUE, 0, sizeof(subt), subtaskTable);

    cl::Buffer dataBuffer = cl::Buffer(context, CL_MEM_READ_WRITE, dataSize * sizeof(cl_uint));
    commandQueue.enqueueWriteBuffer(dataBuffer, CL_TRUE, 0, dataSize * sizeof(cl_uint), data);
    
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

    /* Run the kernel on NDRange until completion. */
	int iter=0;
    while (*state != COMPLETE ) {
#ifdef OCLDBG	  
	  std::cout << "\n *** CALL TO DEVICE " << iter << " *** \n";
#endif	  
      commandQueue.enqueueNDRangeKernel(kernel, cl::NullRange, global, local);
      commandQueue.finish();
#ifdef OCLDBG	  
//	  std::cout << "\n *** CONTROL BACK TO HOST *** \n";
#endif	  
      toggleState(commandQueue, stateBuffer, state);
	  iter++;
    }
    
    commandQueue.finish();
    
    /* Read the results. */
    commandQueue.enqueueReadBuffer(dataBuffer, CL_TRUE, 0, dataSize * sizeof(cl_uint), data);
    
    // Print resulting matrix from example 4. MODIFY ME!!
    std::cout << ((int) data[data[6]]) << " " << ((int) data[data[6] + 1]) << std::endl;
    std::cout << ((int) data[data[6] + 2]) << " " << ((int) data[data[6] + 3]) << std::endl;

    /* Cleanup */
    delete[] queues;
    delete[] readQueues;
    delete[] codeStore;
    delete[] data;
    delete subtaskTable;
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
subt *createSubt() {
  subt *table = new subt;

  if (table) {
    table->av_recs[0] = 1; // The first element is the top of stack index.

    /* Populate the stack with the available records in the subtask table. */
    for (int i = 1; i < SUBT_SIZE + 1; i++) {
      table->av_recs[i] = i - 1;
    }
  }

  return table;
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
