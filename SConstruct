import os
import OclBuilder 
from OclBuilder import initOcl

sources=Split("""
DeviceInfo.cc        
Packet.cpp
UserData.cpp
VM.cpp
""")

sources = map (lambda s: 'src/'+s, sources)

OclBuilder.USE_OCL_WRAPPER= False
OclBuilder.kopts= '-I'+os.environ['PWD']+'/include'
env = initOcl()
env.Append(CPPPATH=['./include'])
env.Program('vm',sources)	

