"""Synthetic art packet ABI for the exact native writer, not game artwork.

Four signed XY pairs are followed by UV origin/extent. The atlas selector
executes the original table path. Camera/GPU restoration remains separate.
"""
import struct
SELECTOR=0x3700
VERTICES=struct.pack('<12h',-8,-8,8,-8,-8,8,8,8,0,0,16,16)
COLOR=bytes((255,255,255,128))
MODULATION=bytes((128,128,128,128))

def install(write,set32,sprite,packet=None):
    set32(sprite+4,0x100)
    set32(sprite+0x10,SELECTOR)
    write(sprite+0x20,VERTICES)
    write(sprite+0x100,COLOR)
    if packet is not None:write(packet+4,MODULATION)
