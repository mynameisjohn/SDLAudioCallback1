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

# Global loopgraph instance
# I don't like having globals, but the buck
# stops somewhere I guess. Maybe I can add it
# to a pyl module somehow, but that won't help
g_LoopGraph = None

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
    global g_LoopGraph
    g_LoopGraph = InitLoopManager(cScene)

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
    g_LoopGraph.Update()

# Right now just send every keydown to the graph
# so it can determine if a state change is coming
def HandleKey(keyCode, bIsKeyDown):
    # If it's not a keydown, it's a keyup
    if not bIsKeyDown:
        global g_LoopGraph
        g_LoopGraph.HandleInput(keyCode)
