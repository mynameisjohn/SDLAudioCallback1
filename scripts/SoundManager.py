import StateGraph
import random
import contextlib
import itertools

import pylLoopManager as pylLM
import pylScene
import pylDrawable

# Stores the basic info about a loop
class Loop:
    def __init__(self, name, headFile, fadeMS = 5, vol = 1., tailFile = None):
        self.name = str(name)
        self.headFile = str(headFile)
        self.fadeMS = int(fadeMS)
        self.vol = float(vol)
        self.tailFile = str(tailFile)

    def __repr__(self):
        return self.name

    def __hash__(self):
        return hash(self.name)

    def __eq__(self, other):
        return self.name == other.name

# Stores a container of loops
# that are iterable via the provided
# generator expression
class LoopSequence:
    # A genexp that returns random.choice on the container
    def randomGen(container):
        while True:
            yield random.choice(container)

    # Init with name, loop container, and gen func
    def __init__(self, name, loops, genFunc = randomGen):
        if not(all(isinstance(l, Loop) for l in loops)):
            raise ValueError('Loops please')
        self.name = name
        self.loops = list(loops)

        # We need the constructor of the gen func
        # because it gets reset via the context
        self._genFuncConstructor = genFunc
        self._genFunc = None
        self.activeLoop = None

    # Context function that gets entered when the state
    # owning this loop seq becomes active. Constructs
    # the generator expression to restart it and 
    # initializes the active loop, then sets both to none 
    @contextlib.contextmanager
    def Activate(self):
        self._genFunc = self._genFuncConstructor(self.loops)
        self.activeLoop = next(self._genFunc)
        yield
        self.activeLoop = None
        self._genFunc = None

    # Calls next on the generator expression
    # (if one is active, will error otherwise)
    def AdvanceActiveLoop(self):
        if self.activeLoop is None:
            raise RuntimeError('Error: Advancing inactive loop sequence!')
        nextActiveLoop = next(self._genFunc)
        #if len(self.loops) > 1:
        #    print('active loop of', self.name, 'switching from', self.activeLoop.name, 'to', nextActiveLoop.name)
        self.activeLoop = nextActiveLoop
        return self.activeLoop 

    def __repr__(self):
        return self.name

    def __hash__(self):
        return hash(self.name)

    def __eq__(self, other):
        return self.name == other.name

# A loop state is a set of loop sequences
class LoopState(StateGraph.State):
    # Initialie with loop seq set and name
    def __init__(self, name, setLoopSequences):
        if not (all(isinstance(l, LoopSequence) for l in setLoopSequences)):
            raise ValueError('LS please')

        # Make it a set, to be sure
        setLoopSequences = set(setLoopSequences)
        self.name = name
        self.diLoopSequences = {l.name : l for l in setLoopSequences}
        # This bool lets us know if we're active... dumb I know
        self.bActive = False

    # The context entry function that sets the bool to active
    # and activates the generating loop sequences, which are
    # deactivated when the context exits (thanks contextlib)
    @contextlib.contextmanager
    def Activate(self, SG, prevState):
        if prevState is None:
            print('Entering State', self)
        else:
            print('Changing state from', prevState, 'to', self)
        self.bActive = True
        
        # add each seq's context to an exit stack
        with contextlib.ExitStack() as LoopSeqIterStack:
            # These will be exited when we exit
            for lsName in self.diLoopSequences.keys():
                LoopSeqIterStack.enter_context(self.diLoopSequences[lsName].Activate())
            yield

        self.bActive = False

    # Returns a generator expression that will yield the active loops from all seqs
    def GetActiveLoopGen(self):
        if self.bActive:
            return (lS.activeLoop for lS in self.diLoopSequences.values())
        raise RuntimeError('Attempting to return a loop seq from an inactive state')
        
    # Advance a specific loop sequence, return gen exp above
    # Advances all if the lsName arg is None
    def AdvanceLoopGen(self, lsName = None):
        if self.bActive:
            if lsName is None:
                for lsName in self.diLoopSequences.keys():
                    self.diLoopSequences[lsName].AdvanceActiveLoop()
            else:
                self.diLoopSequences[lsName].AdvanceActiveLoop()
            return self.GetActiveLoopGen()
        raise RuntimeError('Attempting to return a loop seq from an inactive state')

class DrawableLoopState(LoopState):
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
    def UpdateDrColor(self, cScene, C):
        D = pylDrawable.Drawable(cScene.GetDrawable(self.drIdx))
        D.SetColor(C)

    # Activate override, sets color of drawable
    @contextlib.contextmanager
    def Activate(self, SG, prevState):
        self.bActive = True
        if self.drIdx is not None:
            self.UpdateDrColor(SG.cScene, DrawableLoopState.clrPlaying)

        # add each seq's context to an exit stack
        with contextlib.ExitStack() as LoopSeqIterStack:
            # These will be exited when we exit
            for lsName in self.diLoopSequences.keys():
                LoopSeqIterStack.enter_context(self.diLoopSequences[lsName].Activate())
            yield

        # Set color to off
        if self.drIdx is not None:
            self.UpdateDrColor(SG.cScene, DrawableLoopState.clrOff)

        self.bActive = False

# StateGraph override (should I just have it own a StateGraph?)
class SoundManager:
    # Init takes the C++ LoopManager instance, an initial stim,
    # a map of keycodes to stimuli, and the stategraph args
    def __init__(self, cScene, SG, inputManager):
        # Lots of things to store
        self.cScene = cScene
        self.LM = pylLM.LoopManager(cScene.GetLoopManagerPtr())
        self.SG = SG
        self.inputManager = inputManager

        # The current sample pos is incremented by
        # the curSamplePos inc, which is a multiple of
        # the loop manager's bufsize (every buf adds to inc)
        self.curSamplePos = 0
        self.curSamplePosInc = 0
        self.totalLoopCount = 0

        # the preTrigger is the number of samples before
        # the expected loop duration at which we send a state
        # change (we wait as long as possible.) 
        self.preTrigger = self.LM.GetBufferSize()
        
        # Prime stategraph, nextState is purely used for drawing pending states
        self.nextState = self.SG.AdvanceState()

    # Toggles the play/pause state of the c loop manager
    def PlayPause(self):
        self.LM.PlayPause()

    def GetStateGraph(self):
        return self.SG

    def HandleEvent(self, pSdlEvent):
        self.inputManager.HandleEvent(pSdlEvent)

    # Called every frame, looks at the current stimulus and
    # sample position and determines if either the graph state
    # should advance or if the active loop sequences should advance
    def Update(self):
        # Determine how many buffers have advanced, calculate increment
        # (this involves update the C++ Loop Manager, which locks a mutex)
        uPrevNumBufs = self.LM.GetNumBufsCompleted()
        self.LM.Update()
        uPostNumBufs = self.LM.GetNumBufsCompleted()
        if uPostNumBufs > uPrevNumBufs:
            uNumBufs = uPostNumBufs - uPrevNumBufs
            self.curSamplePosInc += self.LM.GetBufferSize() * uNumBufs
        
        # Compute the new sample pos, zero inc, don't update yet
        newSamplePos = self.curSamplePos + self.curSamplePosInc
        self.curSamplePosInc = 0

        # Determine the next state, but don't advance
        nextState = self.SG.GetNextState()

        # Make a reference to the current active state
        curState = self.SG.GetActiveState()

        # If the pending state is changing
        if nextState is not self.nextState:
            # Set the original pending state's color to off (if not active)
            if self.nextState is not curState:
                self.nextState.UpdateDrColor(self.cScene, DrawableLoopState.clrOff)
            # Set the new pending state's color to pending (if not active)
            if nextState is not curState:
                nextState.UpdateDrColor(self.cScene, DrawableLoopState.clrPending)
            # Update next state
            self.nextState = nextState

        # Store the previous set of loops sent to the LM
        prevSet = set(curState.GetActiveLoopGen())

        # We get out early if there's nothing to do
        bAnythingDone = False
        
        # If the next state isn't the active state
        if self.nextState is not curState:
            # Determine if we should advance the graph state
            trig = curState.triggerRes - self.preTrigger
            if self.curSamplePos < trig and newSamplePos >= trig:
                # Advance graph, reassign state
                curState = self.SG.AdvanceState()
                bAnythingDone = True
        else:
            # Determine if we should advance the active loop seq
            for lSeq in curState.diLoopSequences.values():
                loopTrig = lSeq.activeLoop.cLoop.GetNumSamples(False) - self.preTrigger
                if self.curSamplePos < loopTrig and newSamplePos >= loopTrig:
                    # Advance sequence
                    lSeq.AdvanceActiveLoop()
                    bAnythingDone = True

        # Update sample pos, maybe inc totalLoopCount and reset
        self.curSamplePos = newSamplePos
        if self.curSamplePos >= self.LM.GetMaxSampleCount():
            self.totalLoopCount += 1
            self.curSamplePos = 0
        
        # If no loop changes, get out
        if bAnythingDone == False:
            return

        # Compute the sets of loop changes (do I need to store curSet?)
        curSet = set(curState.GetActiveLoopGen())
        setToTurnOn = curSet - prevSet
        setToTurnOff = prevSet - curSet
#        print('on', setToTurnOn)
#        print('off', setToTurnOff)

        # Send a message list to the LM        
        messageList = []
        for turnOff in setToTurnOff:
            messageList.append((pylLM.CMDStopLoop, (turnOff.name, curState.triggerRes)))
        for turnOn in setToTurnOn:
            messageList.append((pylLM.CMDStartLoop, (turnOn.name, curState.triggerRes)))

        if len(messageList) > 0:
            self.LM.SendMessages(messageList)

# Testing
if __name__ == '__main__':
    sA = LoopState('A', {
        LoopSequence('chSustain',[
            Loop('chSustain1', 'chSustain1_head.wav', 'chSustain1_tail.wav')]),
        LoopSequence('bass',[
            Loop('bass', 'bass_head.wav', 'bass_tail.wav')]),
        LoopSequence('drums',[
            Loop('drums', 'drums_head.wav', 'bass_tail.wav')]),
        LoopSequence('lead',[
            Loop('lead1', 'lead1_head.wav', 'lead1_tail.wav'),
            Loop('lead2', 'lead2_head.wav', 'lead2_tail.wav')], itertools.cycle)
        })

    for lSeq in sA.diLoopSequences.values():
        print(list((l.headFile, l.tailFile) for l in lSeq.loops))

    with sA.Activate(None, None):
        while True:
            prevSet = set(sA.GetActiveLoopGen())
            sA.AdvanceLoopGen('lead')
            curSet = set(sA.GetActiveLoopGen())
            print(prevSet - curSet, curSet - prevSet)