import os
import OclBuilder 
from OclBuilder import initOcl

sources=Split("""
Packet.cpp
UserData.cpp
VM.cpp
timing.cc
""")

sources = map (lambda s: 'src/'+s, sources)

OclBuilder.useOclWrapper = False
OclBuilder.kopts= '-I'+os.environ['PWD']+'/include'
env = initOcl()
sources+=[OclBuilder.OPENCL_DIR+'/OpenCLIntegration/DeviceInfo.cc']
env.Append(CPPPATH=['./include',OclBuilder.OPENCL_DIR+'/OpenCLIntegration'])
env.Program('vm',sources)	

