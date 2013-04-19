#ifndef _USER_CONFIG_H_
#define _USER_CONFIG_H_
#define _IN_HOST

#ifdef OSX
#include <cl.hpp>
#else
#include <CL/cl.hpp>
#endif
#include "SharedTypes.h"
unsigned int populateData(cl_uint *data, unsigned int nServices);

#endif /* _USER_CONFIG_H_ */
