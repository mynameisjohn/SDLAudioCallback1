from LoopGraph import LoopState, LoopSequence, Loop
from StateGraph import StateGraph
import itertools
from collections import namedtuple
import copy
import networkx as nx
import random
import math
import contextlib

# pyl modules
import pylLoopManager as pylLM
import pylSDLKeys as pylSDLK
import pylScene
import pylDrawable
import pylShader
import pylCamera

from LoopManager import *

g_LoopGraph = None
def InitScene(pScene):
    cScene = pylScene.Scene(pScene)

    glVerMajor = 3
    glVerMinor = 0
    screenW = 800
    screenH = 800
    glBackgroundColor = [0.15, 0.15, 0.15, 1.]
    cScene.InitDisplay(glVerMajor, glVerMinor, screenW, screenH, glBackgroundColor)

    cShader = pylShader.Shader(cScene.GetShaderPtr())
    strVertSrc = 'simple.vert'
    strFragSrc = 'simple.frag'
    if cShader.SetSrcFiles(strVertSrc, strFragSrc):
        if cShader.CompileAndLink() != 0:
            raise RuntimeError('Shader failed to compile!')
    else:
        raise RuntimeError('Error: Shader source not loaded')

    cShader.Bind()

    pylDrawable.SetPosHandle(cShader.GetHandle('a_Pos'))
    pylDrawable.SetColorHandle(cShader.GetHandle('u_Color'))
    
    pylCamera.SetProjHandle(cShader.GetHandle('u_PMV'))
    cCamera = pylCamera.Camera(cScene.GetCameraPtr())
    camDim = [-10., 10.]
    cCamera.InitOrtho(camDim, camDim, camDim)
    
    global g_LoopGraph
    g_LoopGraph = InitLoopManager(cScene)

    nodes = g_LoopGraph.G.nodes()
    dTH = 2 * math.pi / len(nodes)
    for drIdx in range(len(nodes)):
        if g_LoopGraph.activeState is nodes[drIdx]:
            clr = MyLoopState.clrPlaying
        else:
            clr = MyLoopState.clrOff
        th = drIdx * dTH - math.pi/2
        if cScene.AddDrawable('quad.iqm', [camDim[0]*math.cos(th)/2, camDim[0]*math.sin(th)/2], [1., 1.], clr):
            nodes[drIdx].drIdx = drIdx

    cShader.Unbind()

# Updates the coro, given the # of loops advanced since
# the last update and teh set of started loops (by name)
def Update(pScene):
    cScene = pylScene.Scene(pScene)

    g_LoopGraph.Update()

# As rudimentary as it gets, if the keycode
# is in the state graph's key dict, use keyUps
# to affect a state transition
def HandleKey(keyCode, bIsKeyDown):
    # If it's not a keydown, it's a keyup
    if not bIsKeyDown:
        global g_LoopGraph
        g_LoopGraph.HandleInput(keyCode)