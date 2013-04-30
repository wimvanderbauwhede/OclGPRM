#include <OclWrapper.h>
#include <string>
#include <deque>
#include "SharedMacros.h"
#include "SharedTypes.h"

class OclGPRM {
    public:
        OclGPRM(const char *bytecodeFile);
//        void loadBytecode(std::string tdc_file_name);
        unsigned int allocateBuffer(unsigned int pos, unsigned int size); // size in bytes to be like OpenCL
        unsigned int writeBuffer(unsigned int pos, void* buf, unsigned int size);
        void* readBuffer(unsigned int pos, unsigned int size);
        unsigned int run(unsigned int num_work_groups, unsigned int num_threads);
    private:
        int* state;
        packet *queues;
        packet *readQueues;
        bytecode *codeStore;
        SubtaskTable* subtaskTables;
        cl::Buffer dataBuffer;
        std::vector<unsigned int>* data_p;
        unsigned int dataSize;
        OclWrapper ocl
        std::deque<bytecode> bytecodeWords;
        std::deque< std::deque<bytecode> > bytecodePackets;

        void toggleState(OclWrapper& ocl, cl::Buffer& stateBuffer, int *state);
        void createSubtask(SubtaskTable*, uint);
        SubtaskTable *createSubt();
        std::deque<bytecode> readBytecode(char *bytecodeFile);
        std::deque< std::deque<bytecode> > words2Packets(std::deque<bytecode>& bytecodeWords);
        
};
