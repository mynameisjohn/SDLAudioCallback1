# pyl modules
import pylLoopManager as pylLM
import pylLoop
import pylScene
import pylDrawable
import pylShader
import pylCamera

import sys, os
sys.path.append(os.path.dirname(os.path.realpath(__file__))+'/sdl2')
import events as SDLEvents
import keycode as SDLK

import math
import networkx as nx
import itertools

from SoundManager import *
from StateGraph import StateGraph
from InputManager import *

# Function to create state graph
def InitSoundManager(cScene):
    # Create LM wrapper
    LM = pylLM.LoopManager(cScene.GetLoopManagerPtr())

    # Create the states (these three sequences are common to all)
    lSeq_chSustain = LoopSequence('chSustain',[Loop('chSustain1', 'chSustain1_head.wav', 5, 1., 'chSustain1_tail.wav')])
    lSeq_bass = LoopSequence('bass',[Loop('bass', 'bass1_head.wav', 5, 1.,'bass1_tail.wav')])
    lSeq_drums = LoopSequence('drums',[Loop('drum1', 'drum1_head.wav', 5, 1.,'drum1_tail.wav')])

    # State 1 just plays lead1, lead2, lead1, lead2...
    s1 = DrawableLoopState('One', {lSeq_chSustain, lSeq_bass, lSeq_drums,
        LoopSequence('lead',[
            Loop('lead1', 'lead1.wav'),
            Loop('lead2', 'lead2_head.wav', 5, 1.,'lead2_tail.wav')], itertools.cycle)
    })

    # State 2 just plays lead3, lead4, lead3, lead4...
    s2 = DrawableLoopState('Two', {lSeq_chSustain, lSeq_bass, lSeq_drums,
        LoopSequence('lead',[
            Loop('lead3', 'lead3.wav'),
            Loop('lead4', 'lead4.wav')], itertools.cycle)
    })

    # State 3 plays lead5, lead6, lead7, lead7...
    s3 = DrawableLoopState('Three', {lSeq_chSustain, lSeq_bass, lSeq_drums, 
        LoopSequence('lead',[
            Loop('lead5', 'lead5.wav'),
            Loop('lead6', 'lead6.wav'),
            Loop('lead7', 'lead7.wav'),
            Loop('lead7', 'lead7.wav')], itertools.cycle)
    })

    s4 = DrawableLoopState('Four', { lSeq_bass, lSeq_drums})
    s5 = DrawableLoopState('Five', { lSeq_bass, LoopSequence('drums2', [Loop('drum2', 'drum2.wav')])})

    # Store all nodes in a list
    nodes = [s1, s2, s3, s4, s5]

    # Create the directed graph; all nodes connect and self connect
    G = nx.DiGraph()
    G.add_edges_from(itertools.product(nodes, nodes))

    # The advance function just returns a random neighbor
    def fnAdvance(SG):
        # Cartesian product
        def dot(A, B):
            return sum(a * b for a, b in itertools.zip_longest(A, B, fillvalue = 0))
        # Return the target of the edge out of activeState whose pathVec is most in line with stim
        return max(SG.G.out_edges_iter(SG.activeState, data = True), key = lambda edge : dot(SG.stim, edge[2]['pathVec']))[1]
    
    # Define the vectors between edges 
    # (used during dot product calculation, SG.stim is one of these)
    diEdges = {n : [1 if n == nn else 0 for nn in nodes] for n in nodes}
    for n in nodes:
        for nn in G.neighbors(n):
            G[n][nn]['pathVec'] = diEdges[nn]

    # Construct the StateGraph with an attribute 'stim' of s1's stimulus,
    # as well as a reference to the scene, so the states can use it
    SG = StateGraph(G, fnAdvance, s1, stim = diEdges[s1], cScene = cScene)

    # Init audio spec
    if LM.Configure({'freq' : 44100, 'channels' : 1, 'bufSize' : 4096}) == False:
        raise RuntimeError('Invalid aud config dict')

    # get the samples per mS
    sampPerMS = int(LM.GetSampleRate() / 1000)

    # For each loop in the state's loop sequences
    for loopState in nodes:
        # The state's trigger res is its longest loop
        loopState.triggerRes = 0
        for lSeq in loopState.diLoopSequences.values():
            for l in lSeq.loops:
                # Set tailfile to empty string if there is None
                if l.tailFile is None:
                    l.tailFile = ''
                # Add the loop
                if  LM.AddLoop(l.name, l.headFile, l.tailFile, int(sampPerMS * l.fadeMS), l.vol) == False:
                    raise IOError(l.name)

                # If successful, store handle to c loop
                l.cLoop = pylLoop.Loop(LM.GetLoop(l.name))

                # The state's trigger res is its longest loop
                if l.cLoop.GetNumSamples(False) > loopState.triggerRes:
                    loopState.triggerRes = l.cLoop.GetNumSamples(False)

    # This dict maps the number keys to edge vectors defined in diEdges
    # (provided there are less than 10 nodes...)
    # the edge vectors are used during state advancement for the graph
    diKeyToStim = {SDLK.SDLK_1 + i : diEdges[n] for i, n in zip(range(len(nodes)), nodes)}

    # The stimulus update function assigns the stim
    # member of the stategraph based on what the
    # button maps to in the dict
    def fnStimKey(btn, keyMgr):
        nonlocal diKeyToStim
        nonlocal SG
        if btn.code in diKeyToStim.keys():
            SG.stim = diKeyToStim[btn.code]
    liButtons = [Button(k, None, fnStimKey) for k in diKeyToStim.keys()]

    # The escape key callback tells the scene to quit
    def fnEscapeKey(btn, keyMgr):
        nonlocal cScene
        cScene.SetQuitFlag(True)
    liButtons.append(Button(SDLK.SDLK_ESCAPE, None, fnEscapeKey))

    # The space key callback tells the loop manager to play/pause
    def fnSpaceKey(btn, keyMgr):
        nonlocal LM
        LM.PlayPause()
    liButtons.append(Button(SDLK.SDLK_SPACE, None, fnSpaceKey))

    # Create the input manager (no mouse manager needed)
    inputManager = InputManager(cScene, KeyboardManager(liButtons), MouseManager([]))

    # Create the sound manager
    soundManager = SoundManager(cScene, SG, inputManager)

    # Start the active loop seq
    activeState = soundManager.GetStateGraph().GetActiveState()
    messageList = [(pylLM.CMDStartLoop, (l.name, 0)) for l in activeState.GetActiveLoopGen()]
    LM.SendMessages(messageList)

    # return the sound manager
    return soundManager

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
    soundManager = InitSoundManager(cScene)

    # Create the drawables from nodes in the loop graph
    # They're in a "circle" about the visible region
    activeState = soundManager.GetStateGraph().activeState
    nodes = soundManager.GetStateGraph().G.nodes()
    dTH = 2 * math.pi / len(nodes)
    for drIdx in range(len(nodes)):
        # Different colors for playing/stopped/pending loops
        if activeState is nodes[drIdx]:
            clr = DrawableLoopState.clrPlaying
        else:
            clr = DrawableLoopState.clrOff

        # Construct drawable
        th = drIdx * dTH - math.pi/2
        if cScene.AddDrawable('quad.iqm', [camDim[0]*math.cos(th)/2, camDim[0]*math.sin(th)/2], [.8, .8], clr):
            # Cache drawable index in state class
            nodes[drIdx].drIdx = drIdx

    # Unbind the shader
    cShader.Unbind()

    # Start SDL audio
    soundManager.PlayPause()

    # Return the sound manager
    return soundManager