import StateGraph
import random
import contextlib
import itertools

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