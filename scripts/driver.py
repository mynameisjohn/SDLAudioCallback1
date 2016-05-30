from LoopGraph import LoopState, LoopSequence, Loop
from StateGraph import StateGraph
import itertools
from collections import namedtuple
import copy
import networkx as nx
import random
import math

import pylLoopManager as pylLM
import pylSDLKeys as pylSDLK
import pylScene
import pylDrawable
import pylShader
import pylCamera

# Cartesian product
def dot(A, B):
    return sum(a * b for a, b in itertools.zip_longest(A, B, fillvalue = 0))

# Input to loop seq coro
CoroData = namedtuple('CoroData', ('loopCount, setStartedLoops'))

# output of loop seq coro
ChangeSet = namedtuple('ChangeSet', ('setToTurnOff', 'setToTurnOn'))

# The loop coro, which claims a ref to the global state graph
# and handles changes coming in and out of the loop graph
def LoopCoroutine(SG):
    # The changes posted to the caller, init to none
    changes = None
    # LoopCount here is the number of times the longest loop repeats...
    totalLoopCount = 0
    # "Prime" the state graph here by activating the initial state
    SG.GetNextState()
    # The coroutine loop
    while True:
        # Every iteration (but the first) yields a ChangeSet
        # containing the set of clips to turn on and off
        cData = yield changes
        changes = None

        # Get a ref to the current active state
        sA = SG.GetActiveState()
        
        # Make a local copy of the last set of loops sent to the LM
        prevSet = set(sA.GetActiveLoopGen())

        # If the loopcount has changed, inc the total loop count
        loopCount = int(cData.loopCount)

        # At every iteration, advance the graph state
        sNext = SG.GetNextState()

        # If the state changed, 
        if sNext is not sA:
            # Build up changeset from new state, post changes
            curSet = set(sNext.GetActiveLoopGen())
            changes = ChangeSet(prevSet - curSet, curSet - prevSet)
            continue

        # If we didn't continue above, and if no 
        # loops have just started, then continue
        # (changes is still None)
        if len(cData.setStartedLoops) == 0:
            continue

        # Otherwise, build up the change set based on the
        # advancement of the active state's loop sequences
        for loopName in cData.setStartedLoops:
            #print('We\'re being told', loopName, 'has stared')
            # Search the active state's loop sequences
            # for this loop (which we better find)
            for lSeq in sA.diLoopSequences.values():
                # If we found it
                if lSeq.activeLoop.name == loopName:
                    lSeq.AdvanceActiveLoop()
                    break
            # If we end up here, it means the loop wasn't matched as we'd like above
            # (which is really bad, so raise an error)
            else:
                raise RuntimeError('finished loop', loopName, 'no longer active!')

        # After advancing whatever needed advancing, find the difference between
        # prevSet and the current set of loops, and construct the changeset
        curSet = set(sA.GetActiveLoopGen())
        changes = ChangeSet(prevSet - curSet, curSet - prevSet)

# Function to create state graph
def CreateStateGraph():
    # Create the states (these three sequences are common to all)
    lSeq_chSustain = LoopSequence('chSustain',[Loop('chSustain1', 'chSustain1_head.wav', 5, 1., 'chSustain1_tail.wav')])
    lSeq_bass = LoopSequence('bass',[Loop('bass', 'bass1_head.wav', 5, 1.,'bass1_tail.wav')])
    lSeq_drums = LoopSequence('drums',[Loop('drum', 'drum1_head.wav', 5, 1.,'drum1_tail.wav')])

    # State 1 just plays lead1, lead2, lead1, lead2...
    s1 = LoopState('One', {lSeq_chSustain, lSeq_bass, lSeq_drums,
        LoopSequence('lead',[
            Loop('lead1', 'lead1.wav'),
            Loop('lead2', 'lead2_head.wav', 5, 1.,'lead2_tail.wav')], itertools.cycle)
    })

    # State 2 just plays lead3, lead4, lead3, lead4...
    s2 = LoopState('Two', {lSeq_chSustain, lSeq_bass, lSeq_drums,
        LoopSequence('lead',[
            Loop('lead3', 'lead3.wav'),
            Loop('lead4', 'lead4.wav')], itertools.cycle)
    })

    # State 3 plays lead5, lead6, lead7, lead7...
    s3 = LoopState('Three', {lSeq_chSustain, lSeq_bass, lSeq_drums, 
        LoopSequence('lead',[
            Loop('lead5', 'lead5.wav'),
            Loop('lead6', 'lead6.wav'),
            Loop('lead7', 'lead7.wav'),
            Loop('lead7', 'lead7.wav')], itertools.cycle)
    })

    # Create the directed graph; all nodes connect and self connect
    G = nx.DiGraph()
    G.add_edges_from(itertools.product((s1, s2, s3), (s1, s2, s3)))

    # Define the vectors between edges 
    # (used during dot product calculation, SG.stim is one of these)
    edgeDict = {
        s1 : [1, 0, 0],
        s2 : [0, 1, 0],
        s3 : [0, 0, 1]
        }
    for n in G.nodes():
        for nn in G.neighbors(n):
            G[n][nn]['pathVec'] = edgeDict[nn]

    # Set up the key handling dict
    keyDict = {
        pylSDLK.Num1 : edgeDict[s1],
        pylSDLK.Num2 : edgeDict[s2],
        pylSDLK.Num3 : edgeDict[s3]
    }

    # The advance function just returns a random neighbor
    def fnAdvance(SG):
        # Return the target of the edge out of activeState whose pathVec is most in line with stim
        return max(SG.G.out_edges_iter(SG.activeState, data = True), key = lambda edge : dot(SG.stim, edge[2]['pathVec']))[1]

    # return the state graph
    return StateGraph(G, fnAdvance, s1, {'keyDict' : keyDict, 'stim' : edgeDict[s1]})

# Globally declare state graph instance, init to None
g_SG = None

# globally declare coro as well, init to None
g_LoopCoro = None

def InitLoopManager(pLoopManager):
    # Create LM
    LM = pylLM.LoopManager(pLoopManager)

    # Init audio spec
    if LM.Configure({'freq' : 44100, 'channels' : 1, 'bufSize' : 4096}) == False:
        raise RuntimeError('Invalid aud config dict')

    # Globally capture stategraph, coroutine
    global g_SG
    global g_LoopCoro

    # Init the two, prime the coroutine
    g_SG = CreateStateGraph()
    g_LoopCoro = LoopCoroutine(g_SG)
    next(g_LoopCoro)

    # Create the py LM and get the samples per mS
    sampPerMS = int(LM.GetSampleRate() / 1000.)

    # For each loop in the state's loop sequences
    for loopState in g_SG.G.nodes_iter():
        for lSeq in loopState.diLoopSequences.values():
            for l in lSeq.loops:
                # Set tailfile to empty string if there is None
                if l.tailFile is None:
                    l.tailFile = ''
                # Add the loop
                if  LM.AddLoop(l.name, l.headFile, l.tailFile, sampPerMS * l.fadeMS, l.vol) == False:
                    raise IOError(l.name)

    # Start the active loop seq
    messageList = [(pylLM.CMDStartLoop, (l.name, 0)) for l in g_SG.GetActiveState().GetActiveLoopGen()]
    LM.SendMessages(messageList)
    
    if LM.Start() == False:
        raise RuntimeError('Error opening SDL audio device')

# Updates the coro, given the # of loops advanced since
# the last update and teh set of started loops (by name)
def Update(pLoopManager, loopCount, setStartedLoops):
    # Construct LM, make set unicode
    LM = pylLM.LoopManager(pLoopManager)
    setStartedLoops = set(l.decode('unicode_escape') for l in setStartedLoops)

    # Capture the coro, send the new data and get the changes
    global g_LoopCoro
    changeSets = g_LoopCoro.send(CoroData(loopCount, setStartedLoops))

    # If there are changes, start and stop the appropriate loops
    if changeSets is not None:
        print(changeSets)
        messageList = []
        for turnOff in changeSets.setToTurnOff:
            messageList.append((pylLM.CMDStopLoop, (turnOff.name, LM.GetMaxSampleCount())))
        for turnOn in changeSets.setToTurnOn:
            messageList.append((pylLM.CMDStartLoop, (turnOn.name, LM.GetMaxSampleCount())))
        LM.SendMessages(messageList)

# As rudimentary as it gets, if the keycode
# is in the state graph's key dict, use keyUps
# to affect a state transition
def HandleKey(keyCode, bIsKeyDown):
    # If it's not a keydown, it's a keyup
    if not bIsKeyDown:
        global g_SG
        if keyCode in g_SG.keyDict.keys():
            # Set the stim directly, it'll be used in the coro
            g_SG.stim = g_SG.keyDict[keyCode]

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

    pylCamera.SetProjHandle(cShader.GetHandle('u_PMV'))
    cCamera = pylCamera.Camera(cScene.GetCameraPtr())
    camDim = [-10., 10.]
    cCamera.InitOrtho(camDim, camDim, camDim)

    InitLoopManager(cScene.GetLoopManagerPtr())

    pylDrawable.SetPosHandle(cShader.GetHandle('a_Pos'))
    pylDrawable.SetColorHandle(cShader.GetHandle('u_Color'))

    global g_SG
    nodes = g_SG.G.nodes()
    dTH = 2 * math.pi / len(nodes)
    for i in range(len(nodes)):
        th = i * dTH - math.pi/2
        cScene.AddDrawable('quad.iqm', [camDim[0]*math.cos(th)/2, camDim[0]*math.sin(th)/2], [1., 1.], [1., 1., 1., 1.])

    cShader.Unbind()