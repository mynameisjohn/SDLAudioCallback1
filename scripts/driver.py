# Local modules
import Init

import sys, os
sys.path.append(os.path.dirname(os.path.realpath(__file__))+'/sdl2')
import events as SDLEvents
import keycode as SDLK

# Used to cast sdl event capsule to a pointer to the struct
import ctypes
def convert_capsule_to_int(capsule):
    ctypes.pythonapi.PyCapsule_GetPointer.restype = ctypes.c_void_p
    ctypes.pythonapi.PyCapsule_GetPointer.argtypes = [ctypes.py_object, ctypes.c_char_p]
    return ctypes.pythonapi.PyCapsule_GetPointer(capsule, None)

# Global loopgraph instance
# I don't like having globals, but the buck
# stops somewhere I guess. Maybe I can add it
# to a pyl module somehow, but that won't help
g_SoundManager = None

def Initialize(pScene):
    global g_SoundManager
    g_SoundManager = Init.InitScene(pScene)

# Called every frame from C++
def Update(pScene):
    # All this does is update the loop graph for now
    global g_SoundManager
    g_SoundManager.Update()

# rudimentary input handling function
def HandleEvent(pEvent):
    # Delegate to the the loop graph's input manager
    g_SoundManager.HandleEvent(pEvent)
