from LoopGraph import LoopState, LoopSequence, Loop
from StateGraph import StateGraph
import pylLoopManager as pylLM
import pylLoop
import pylSDLKeys as pylSDLK
import pylDrawable
import contextlib
import random
import itertools
import networkx as nx

# Cartesian product
def dot(A, B):
    return sum(a * b for a, b in itertools.zip_longest(A, B, fillvalue = 0))

class LoopGraph(StateGraph):
    def __init__(self, LM, initialStim, keyDict, *args):
        StateGraph.__init__(self, *args)
        self.LM = LM
        self.stim = initialStim
        self.keyDict = keyDict
        self.prevSet = set()
        self.curSet = set()
        self.curSamplePos = 0
        self.curSamplePosInc = 0
        self.totalLoopCount = 0
        self.preTrigger = LM.GetBufferSize()
        self.nextState = self._fnAdvance(self)

    def UpdateSamplePos(self, numBufs):
        self.curSamplePosInc += numBufs * self.LM.GetBufferSize()

    def HandleInput(self, key):
        if key in self.keyDict.keys():
            self.stim = self.keyDict[key]
    # Presumably when this is called, the HandleInput
    # or SetSamplePos functions have had their input and
    # the update function is ready to determine where to go
    # That means the update function must be able to use either
    # the keyboard stim or the sample pos to advance loops
    def Update(self):
        nextState = self._fnAdvance(self)
        if nextState is not self.nextState:
            if self.nextState is not self.activeState:
                D = pylDrawable.Drawable(self.cScene.GetDrawable(self.nextState.drIdx))
                D.SetColor(MyLoopState.clrOff)
            if nextState is not self.activeState:
                D = pylDrawable.Drawable(self.cScene.GetDrawable(nextState.drIdx))
                D.SetColor(MyLoopState.clrPending)
            self.nextState = nextState
            
        bAnythingDone = False
        newSamplePos = self.curSamplePos + self.curSamplePosInc
        self.curSamplePosInc = 0

        self.prevSet = set(self.activeState.GetActiveLoopGen())
        
        if self.nextState is not self.activeState:
            trig = self.activeState.triggerRes - self.preTrigger
            if self.curSamplePos < trig and newSamplePos >= trig:
                self.GetNextState()
                bAnythingDone = True
        else:
            bAny = False
            for lSeq in self.activeState.diLoopSequences.values():
                loopTrig = lSeq.activeLoop.cLoop.GetNumSamples(False) - self.preTrigger
                if self.curSamplePos < loopTrig and newSamplePos >= loopTrig:
                    lSeq.AdvanceActiveLoop()
                    bAnythingDone = True

        self.curSamplePos = newSamplePos
                        
        if bAnythingDone == False:
            return
        else:
            self.curSamplePos = 0

        self.curSet = set(self.activeState.GetActiveLoopGen())
        setToTurnOn = self.curSet - self.prevSet
        setToTurnOff = self.prevSet - self.curSet
        print('on', setToTurnOn)
        print('off', setToTurnOff)
        
        messageList = []
        for turnOff in setToTurnOff:
            messageList.append((pylLM.CMDStopLoop, (turnOff.name, self.activeState.triggerRes)))
        for turnOn in setToTurnOn:
            messageList.append((pylLM.CMDStartLoop, (turnOn.name, self.activeState.triggerRes)))

        if len(messageList) > 0:
            self.LM.SendMessages(messageList)

class MyLoopState(LoopState):
    clrOff = [1., 0., 0., 1.]
    clrPending = [1., 1., 0., 1.]
    clrPlaying = [0., 1., 0., 1.]

    def __init__(self, *args):
        LoopState.__init__(self, *args)
        self.drIdx = None

    def SetDrawableIdx(self, drIdx):
        self.drIdx = drIdx

    @contextlib.contextmanager
    def Activate(self, SG, prevState):
        self.bActive = True
        if self.drIdx is not None:
            D = pylDrawable.Drawable(SG.cScene.GetDrawable(self.drIdx))
            D.SetColor(MyLoopState.clrPlaying)

        # add each seq's context to an exit stack
        with contextlib.ExitStack() as LoopSeqIterStack:
            # These will be exited when we exit
            for lsName in self.diLoopSequences.keys():
                LoopSeqIterStack.enter_context(self.diLoopSequences[lsName].Activate())
            yield

        if self.drIdx is not None:
            D = pylDrawable.Drawable(SG.cScene.GetDrawable(self.drIdx))
            D.SetColor(MyLoopState.clrOff)

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
    # Create LM
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
    
    if LM.Start() == False:
        raise RuntimeError('Error opening SDL audio device')

    return loopGraph