import math

# pyl modules
import pylLoopManager as pylLM
import pylSDLKeys as pylSDLK
import pylScene
import pylDrawable
import pylShader
import pylCamera

# Local modules
from LoopGraph import LoopState, LoopSequence, Loop
from StateGraph import StateGraph
from LoopManager import *
import InputManager

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
g_LoopGraph = None

def InitInputManager(pScene):
    cScene = pylScene.Scene(pScene)

    # The escape key callback tells the scene to quit
    def fnEscapeKey(btn, keyMgr):
        nonlocal cScene
        cScene.SetQuitFlag(True)
    btnEscapeKey = InputManager.Button(SDLK.SDLK_ESCAPE, fnUp = fnEscapeKey)

    # The space key callback tells the loop manager to play/pause
    def fnSpaceKey(btn, keyMgr):
        nonlocal cScene
        LM = pylLM.LoopManager(cScene.GetLoopManagerPtr())
        LM.PlayPause()
    btnSpaceKey = InputManager.Button(SDLK.SDLK_SPACE, fnUp = fnSpaceKey)

    # Create Key Manager
    keyMgr = InputManager.KeyboardManager((btnEscapeKey, btnSpaceKey))

    # I don't know what to do with the mouse yet...
    def fnMouseClick(btn, mouseMgr):
        global cScene
        if btn.code is SDLEvents.SDL_BUTTON_RIGHT:
            print('Right Button up at', mouseMgr.mousePos)
        if btn.code is SDLEvents.SDL_BUTTON_LEFT:
            print('Left Button up at', mouseMgr.mousePos)
    btnRMouseClick = InputManager.Button(SDLEvents.SDL_BUTTON_RIGHT, fnUp = fnMouseClick)
    btnLMouseClick = InputManager.Button(SDLEvents.SDL_BUTTON_LEFT, fnUp = fnMouseClick)

    mouseMgr = InputManager.MouseManager((btnRMouseClick, btnLMouseClick))

    return InputManager.InputManager(cScene, keyMgr, mouseMgr)

# Called by the C++ Scene class's
# constructor, inits Scene and
# the loop manager, adds drawables
def InitScene(pScene):
    # Create the pyl wrapper for Scene
    cScene = pylScene.Scene(pScene)

    # Init the display 
    # TODO Window name, maybe do camera here as well
    glVerMajor = 3
    glVerMinor = 0
    screenW = 800
    screenH = 800
    glBackgroundColor = [0.15, 0.15, 0.15, 1.]
    cScene.InitDisplay(glVerMajor, glVerMinor, screenW, screenH, glBackgroundColor)

    # After GL context has started, create the Shader
    cShader = pylShader.Shader(cScene.GetShaderPtr())
    strVertSrc = 'simple.vert'
    strFragSrc = 'simple.frag'
    if cShader.SetSrcFiles(strVertSrc, strFragSrc):
        if cShader.CompileAndLink() != 0:
            raise RuntimeError('Shader failed to compile!')
    else:
        raise RuntimeError('Error: Shader source not loaded')

    # The shader must be bound while creating drawables
    # This isn't strictly true, it's just that I need the handles
    # to some shader variables. What I should do is cache every shader
    # variable in a std::map<GLint, string> on compilation. 
    cShader.Bind()

    # Get the position and color handles, set static drawable vars
    pylDrawable.SetPosHandle(cShader.GetHandle('a_Pos'))
    pylDrawable.SetColorHandle(cShader.GetHandle('u_Color'))
    
    # Set static PMV handle for camera
    pylCamera.SetProjHandle(cShader.GetHandle('u_PMV'))

    # Init camera (TODO give this screen dims or aspect ratio)
    cCamera = pylCamera.Camera(cScene.GetCameraPtr())
    camDim = [-10., 10.]
    cCamera.InitOrtho(camDim, camDim, camDim)
    
    # Create the loop graph (in LoopManager.py)
    # and give it an input manager
    global g_LoopGraph
    g_LoopGraph = InitLoopManager(cScene)
    g_LoopGraph.inputManager = InitInputManager(pScene)

    # Create the drawables from nodes in the loop graph
    # They're in a "circle" about the visible region
    nodes = g_LoopGraph.G.nodes()
    dTH = 2 * math.pi / len(nodes)
    for drIdx in range(len(nodes)):
        # Different colors for playing/stopped/pending loops
        if g_LoopGraph.activeState is nodes[drIdx]:
            clr = MyLoopState.clrPlaying
        else:
            clr = MyLoopState.clrOff

        # Construct drawable
        th = drIdx * dTH - math.pi/2
        if cScene.AddDrawable('quad.iqm', [camDim[0]*math.cos(th)/2, camDim[0]*math.sin(th)/2], [1., 1.], clr):
            # Cache drawable index in state class
            nodes[drIdx].drIdx = drIdx

    # Unbind the shader
    cShader.Unbind()

# Called every frame from C++
def Update(pScene):
    # Right now this just updates the loop graph
    global g_LoopGraph
    g_LoopGraph.Update()

# rudimentary input handling function
def HandleEvent(pEvent):
    # cast pointer to SDL event as ctype struct
    h = convert_capsule_to_int(pEvent)
    e = SDLEvents.SDL_Event.from_address(h)

    # Delegate to the the loop graph's input manager
    g_LoopGraph.inputManager.HandleEvent(e)