import pylLoopManager as pylLM
from LoopGraph import LoopState, LoopSequence, Loop
from StateGraph import StateGraph
import itertools
from collections import namedtuple
import copy
import networkx as nx
import random

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
        if loopCount > 0:
            #print('loopcount changed by', loopCount)
            totalLoopCount += loopCount

            if 'lead2' in cData.setStartedLoops or 'lead4' in cData.setStartedLoops or (
                'lead7' in cData.setStartedLoops and totalLoopCount % 2):
                #print('Advancing graph state')
                # Advance the state graph
                sNext = SG.GetNextState()

                # If the state changed
                if sA is not sNext:
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
    # Create the states (three for now, very redundant)
    s1 = LoopState('One', {
    LoopSequence('chSustain',[
        Loop('chSustain1', 'chSustain1_head.wav', 5, 1., 'chSustain1_tail.wav')]),
    LoopSequence('bass',[
        Loop('bass', 'bass1_head.wav', 5, 1.,'bass1_tail.wav')]),
    LoopSequence('drums',[
        Loop('drum', 'drum1_head.wav', 5, 1.,'drum1_tail.wav')]),
    LoopSequence('lead',[
        Loop('lead1', 'lead1.wav'),
        Loop('lead2', 'lead2_head.wav', 5, 1.,'lead2_tail.wav')], itertools.cycle)
    })

    s2 = LoopState('Two', {
    LoopSequence('chSustain',[
        Loop('chSustain1', 'chSustain1_head.wav', 5, 1., 'chSustain1_tail.wav')]),
    LoopSequence('bass',[
        Loop('bass', 'bass1_head.wav', 5, 1.,'bass1_tail.wav')]),
    LoopSequence('drums',[
        Loop('drum', 'drum1_head.wav', 5, 1.,'drum1_tail.wav')]),
    LoopSequence('lead',[
        Loop('lead3', 'lead3.wav'),
        Loop('lead4', 'lead4.wav')], itertools.cycle)
    })


    s3 = LoopState('Three', {
    LoopSequence('chSustain',[
        Loop('chSustain1', 'chSustain1_head.wav', 5, 1., 'chSustain1_tail.wav')]),
    LoopSequence('bass',[
        Loop('bass', 'bass1_head.wav', 5, 1.,'bass1_tail.wav')]),
    LoopSequence('drums',[
        Loop('drum', 'drum1_head.wav', 5, 1.,'drum1_tail.wav')]),
    LoopSequence('lead',[
        Loop('lead5', 'lead5.wav'),
        Loop('lead6', 'lead6.wav'),
        Loop('lead7', 'lead7.wav'),
        Loop('lead7', 'lead7.wav')], itertools.cycle)
    })

    # Create the directed graph; s1, s2 connect and self connect
    G = nx.DiGraph()
    G.add_edges_from([(s1, s2), (s2, s3), (s3, s1)])
        #itertools.product((s1, s2, s3), (s1, s2, s3)))

    # The advance function just returns a random neighbor
    def fnAdvance(SG):
        if SG.activeState is None:
            return random.choice(SG.G.nodes())
        return random.choice(SG.G.neighbors(SG.activeState))

    # return the state graph
    return StateGraph(G, fnAdvance, s1)

# Globally declare state graph instance, init to None
g_SG = None

# globally declare coro as well, init to None
g_LoopCoro = None

def InitLoopManager(pLoopManager):
    # Globally capture stategraph, coroutine
    global g_SG
    global g_LoopCoro

    # Init the two, prime the coroutine
    g_SG = CreateStateGraph()
    g_LoopCoro = LoopCoroutine(g_SG)
    next(g_LoopCoro)

    # Create the py LM and get the samples per mS
    LM = pylLM.LoopManager(pLoopManager)
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