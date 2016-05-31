import contextlib
import random
import itertools
import networkx as nx

# I know this is confusing... I have so many classes that
# have the word loop in their name. It's dumb
from LoopGraph import LoopState, LoopSequence, Loop
from StateGraph import StateGraph

# pyl modules
import pylLoopManager as pylLM
import pylLoop
import pylSDLKeys as pylSDLK
import pylDrawable

# Cartesian product
def dot(A, B):
    return sum(a * b for a, b in itertools.zip_longest(A, B, fillvalue = 0))

# StateGraph override (should I just have it own a StateGraph?)
class LoopGraph(StateGraph):
    # Init takes the C++ LoopManager instance, an initial stim,
    # a map of keycodes to stimuli, and the stategraph args
    def __init__(self, LM, initialStim, keyDict, *args):
        # Call the base constructor
        StateGraph.__init__(self, *args)

        # Lots of things to store
        self.LM = LM
        self.stim = initialStim
        self.keyDict = keyDict

        # These sets are used to determine what to turn
        # on and off during Update
        self.prevSet = set()
        self.curSet = set()

        # The current sample pos is incremented by
        # the curSamplePos inc, which is a multiple of
        # the loop manager's bufsize (every buf adds to inc)
        self.curSamplePos = 0
        self.curSamplePosInc = 0
        self.totalLoopCount = 0

        # the preTrigger is the number of samples before
        # the expected loop duration at which we send a state
        # change (we wait as long as possible.) 
        self.preTrigger = LM.GetBufferSize()

        # nextState is purely used for drawing pending states
        self.nextState = self._fnAdvance(self)

    # Confusing function name... the sample pos should only go
    # up by a multiple of the lm's buffer size, so pass in
    # the number of buffers that have been rendered since last time
    def UpdateSamplePos(self, numBufs):
        self.curSamplePosInc += numBufs * self.LM.GetBufferSize()

    # If the keycode is one of ours, set the stimulus
    # (but don't change state yet)
    def HandleInput(self, key):
        if key in self.keyDict.keys():
            self.stim = self.keyDict[key]

    # Presumably when this is called, the HandleInput
    # or SetSamplePos functions have had their input and
    # the update function is ready to determine where to go
    # That means the update function must be able to use either
    # the keyboard stim or the sample pos to advance loops

    # Called every frame, looks at the current stimulus and
    # sample position and determines if either the graph state
    # should advance or if the active loop sequences should advance
    def Update(self):
        # Determine the next state, but don't advance
        nextState = self._fnAdvance(self)

        # If the pending state is changing
        if nextState is not self.nextState:
            # Set the original pending state's color to off (if not active)
            if self.nextState is not self.activeState:
                self.nextState.SetDrColor(self.cScene, MyLoopState.clrOff)
            # Set the new pending state's color to pending (if not active)
            if nextState is not self.activeState:
                nextState.SetDrColor(self.cScene, MyLoopState.clrPending)
            # Update next state
            self.nextState = nextState
        
        # Compute the new sample pos, zero inc, don't update yet
        newSamplePos = self.curSamplePos + self.curSamplePosInc
        self.curSamplePosInc = 0

        # Store the previous set of loops sent to the LM
        self.prevSet = set(self.activeState.GetActiveLoopGen())

        # We get out early if there's nothing to do
        bAnythingDone = False
        
        # If the next state isn't the active state
        if self.nextState is not self.activeState:
            # Determine if we should advance the graph state
            trig = self.activeState.triggerRes - self.preTrigger
            if self.curSamplePos < trig and newSamplePos >= trig:
                # Advance graph
                self.GetNextState()
                bAnythingDone = True
        else:
            # Determine if we should advance the active loop seq
            for lSeq in self.activeState.diLoopSequences.values():
                loopTrig = lSeq.activeLoop.cLoop.GetNumSamples(False) - self.preTrigger
                if self.curSamplePos < loopTrig and newSamplePos >= loopTrig:
                    # Advance sequence
                    lSeq.AdvanceActiveLoop()
                    bAnythingDone = True

        # Update sample pos, maybe reset
        self.curSamplePos = newSamplePos
        if self.curSamplePos >= self.LM.GetMaxSampleCount():
            self.curSamplePos = 0
        
        # If no loop changes, get out
        if bAnythingDone == False:
            return

        # Compute the sets of loop changes (do I need to store curSet?)
        self.curSet = set(self.activeState.GetActiveLoopGen())
        setToTurnOn = self.curSet - self.prevSet
        setToTurnOff = self.prevSet - self.curSet
#        print('on', setToTurnOn)
#        print('off', setToTurnOff)

        # Send a message list to the LM        
        messageList = []
        for turnOff in setToTurnOff:
            messageList.append((pylLM.CMDStopLoop, (turnOff.name, self.activeState.triggerRes)))
        for turnOn in setToTurnOn:
            messageList.append((pylLM.CMDStartLoop, (turnOn.name, self.activeState.triggerRes)))

        if len(messageList) > 0:
            self.LM.SendMessages(messageList)

# LoopState override, just handles color changing
class MyLoopState(LoopState):
    clrOff = [1., 0., 0., 1.]
    clrPending = [1., 1., 0., 1.]
    clrPlaying = [0., 1., 0., 1.]

    # The only difference is we cache a drawable index
    def __init__(self, *args):
        LoopState.__init__(self, *args)
        self.drIdx = None

    def SetDrawableIdx(self, drIdx):
        self.drIdx = drIdx

    # Use the cached drawable index to update the color
    def UpdateDrColor(self, cScene):
        D = pylDrawable.Drawable(cScene.GetDrawable(self.drIdx))
        D.SetColor(MyLoopState.clrPlaying)

    # Activate override, sets color of drawable
    @contextlib.contextmanager
    def Activate(self, SG, prevState):
        self.bActive = True
        if self.drIdx is not None:
            self.UpdateDrColor(SG.cScene, MyLoopState.clrPlaying)

        # add each seq's context to an exit stack
        with contextlib.ExitStack() as LoopSeqIterStack:
            # These will be exited when we exit
            for lsName in self.diLoopSequences.keys():
                LoopSeqIterStack.enter_context(self.diLoopSequences[lsName].Activate())
            yield

        # Set color to off
        if self.drIdx is not None:
            self.UpdateDrColor(SG.cScene, MyLoopState.clrOff)

        self.bActive = False

# Function to create state graph
def CreateLoopGraph(LM):
    # Create the states (these three sequences are common to all)
    lSeq_chSustain = LoopSequence('chSustain',[Loop('chSustain1', 'chSustain1_head.wav', 5, 1., 'chSustain1_tail.wav')])
    lSeq_bass = LoopSequence('bass',[Loop('bass', 'bass1_head.wav', 5, 1.,'bass1_tail.wav')])
    lSeq_drums = LoopSequence('drums',[Loop('drum', 'drum1_head.wav', 5, 1.,'drum1_tail.wav')])

    # State 1 just plays lead1, lead2, lead1, lead2...
    s1 = MyLoopState('One', {lSeq_chSustain, lSeq_bass, lSeq_drums,
        LoopSequence('lead',[
            Loop('lead1', 'lead1.wav'),
            Loop('lead2', 'lead2_head.wav', 5, 1.,'lead2_tail.wav')], itertools.cycle)
    })

    # State 2 just plays lead3, lead4, lead3, lead4...
    s2 = MyLoopState('Two', {lSeq_chSustain, lSeq_bass, lSeq_drums,
        LoopSequence('lead',[
            Loop('lead3', 'lead3.wav'),
            Loop('lead4', 'lead4.wav')], itertools.cycle)
    })

    # State 3 plays lead5, lead6, lead7, lead7...
    s3 = MyLoopState('Three', {lSeq_chSustain, lSeq_bass, lSeq_drums, 
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

    # Create the loop graph
    return LoopGraph(LM, edgeDict[s1], keyDict, G, fnAdvance, s1)

def InitLoopManager(cScene):
    # Create LM wrapper
    LM = pylLM.LoopManager(cScene.GetLoopManagerPtr())

    # Init audio spec
    if LM.Configure({'freq' : 44100, 'channels' : 1, 'bufSize' : 4096}) == False:
        raise RuntimeError('Invalid aud config dict')

    # Create Loop Graph
    loopGraph = CreateLoopGraph(LM)
    loopGraph.GetNextState()
    loopGraph.cScene = cScene

    # Create the py LM and get the samples per mS
    sampPerMS = int(LM.GetSampleRate() / 1000.)

    # For each loop in the state's loop sequences
    for loopState in loopGraph.G.nodes_iter():
        # The state's trigger res is its longest loop
        loopState.triggerRes = 0
        for lSeq in loopState.diLoopSequences.values():
            for l in lSeq.loops:
                # Set tailfile to empty string if there is None
                if l.tailFile is None:
                    l.tailFile = ''
                # Add the loop
                if  LM.AddLoop(l.name, l.headFile, l.tailFile, sampPerMS * l.fadeMS, l.vol) == False:
                    raise IOError(l.name)

                # If successful, store handle to c loop
                l.cLoop = pylLoop.Loop(LM.GetLoop(l.name))

                # The state's trigger res is its longest loop
                if l.cLoop.GetNumSamples(False) > loopState.triggerRes:
                    loopState.triggerRes = l.cLoop.GetNumSamples(False)

    # Start the active loop seq
    messageList = [(pylLM.CMDStartLoop, (l.name, 0)) for l in loopGraph.GetActiveState().GetActiveLoopGen()]
    LM.SendMessages(messageList)
   
    # Start SDL Audio 
    if LM.Start() == False:
        raise RuntimeError('Error opening SDL audio device')

    # Return the graph instance
    return loopGraph
