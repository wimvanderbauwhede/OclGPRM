#define __CL_ENABLE_EXCEPTIONS

#include <CL/cl.hpp>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include "DeviceInfo.h"

const int QUEUE_SIZE = 16;
const std::string KERNEL_FILE("kernels/vm.cl");

int main() {
  std::vector<cl::Platform> platforms;
  std::vector<cl::Device> devices;
  cl::Device device;
  cl::Program program;

  DeviceInfo dInfo;

  try {
    /* Create a vector of available platforms. */
    cl::Platform::get(&platforms);
    
    /* Create a vector of available devices (GPU Priority). */
    try {
      platforms[0].getDevices(CL_DEVICE_TYPE_GPU, &devices);
    } catch (cl::Error error) {
      platforms[0].getDevices(CL_DEVICE_TYPE_DEFAULT, &devices);
    }

    /* Create a platform context for the available devices. */
    cl::Context context(devices);

    /* Use the first available device. */
    device = devices[0];
    
    /* Get the number of compute units for the device. */
    int computeUnits = dInfo.max_compute_units(device);

    /* Create a command queue for the device. */
    cl::CommandQueue commandQueue = cl::CommandQueue(context, device);

    /* Read the kernel program source. */
    std::ifstream kernelSourceFile(KERNEL_FILE.c_str());
    std::string kernelSource(std::istreambuf_iterator<char>(kernelSourceFile), (std::istreambuf_iterator<char>()));
    cl::Program::Sources source(1, std::make_pair(kernelSource.c_str(), kernelSource.length() + 1));
    
    /* Create a program in the context using the kernel source code. */
    program = cl::Program(context, source);
    
    /* Build the program for the available devices. */
    program.build(devices);

    /* Create the qtest kernel. */
    cl::Kernel kernel(program, "qtest");
    
    /* Allocate memory for the queues. */
    cl_uint2 *queues = new cl_uint2[computeUnits * QUEUE_SIZE];

    /* Initialise queue elements to zero. */
    for (int i = 0; i < computeUnits * QUEUE_SIZE; i++) {
      queues[i].x = 0;
      queues[i].y = 0;
    }

    /* Initialise the head/tail index values. */
    for (int i = 0; i < computeUnits; i++) {
      int rw_index = (i * QUEUE_SIZE) + QUEUE_SIZE - 1;
      queues[rw_index].x = 0;
      queues[rw_index].y = 0;
    }

    /* Create memory buffers on the device. */
    cl::Buffer queueBuffer = cl::Buffer(context, CL_MEM_READ_WRITE, computeUnits * QUEUE_SIZE * sizeof(cl_uint2));
    commandQueue.enqueueWriteBuffer(queueBuffer, CL_TRUE, 0, computeUnits * QUEUE_SIZE * sizeof(cl_uint2), queues);

    /* Set kernel arguments. */
    kernel.setArg(0, queueBuffer);
    kernel.setArg(1, QUEUE_SIZE);

    /* Run the kernel on NDRange. */
    cl::NDRange global(computeUnits), local(1);
    commandQueue.enqueueNDRangeKernel(kernel, cl::NullRange, global, local);
    
    /* Wait for completion. */
    commandQueue.finish();
    
    /* Read the modified queue buffer. */
    commandQueue.enqueueReadBuffer(queueBuffer, CL_TRUE, 0, computeUnits * QUEUE_SIZE * sizeof(cl_uint2), queues);
    for (int i = 0; i < computeUnits * QUEUE_SIZE; i++) {
      if ((i % QUEUE_SIZE) == 0) std::cout << std::endl;
      std::cout << "(" << queues[i].x << " " << queues[i].y << ")" << " ";
    }
    std::cout << std::endl;


    /* Cleanup */
    delete[] queues;
  } catch (cl::Error error) {
    std::cout << "EXCEPTION: " << error.what() << " [" << error.err() << "]" << std::endl;
    std::cout << program.getBuildInfo<CL_PROGRAM_BUILD_LOG>(device) << std::endl;
  }

  return 0;
}