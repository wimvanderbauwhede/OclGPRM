#ifndef _USER_CONFIG_H_
#define _USER_CONFIG_H_

#ifdef OSX
#include <cl.hpp>
#else
#include <CL/cl.hpp>
#endif

void populateData(cl_uint *data);

#endif /* _USER_CONFIG_H_ */
