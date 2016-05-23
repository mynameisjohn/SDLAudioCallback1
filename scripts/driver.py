import pylLoopManager as pylLM
from LoopGraph import LoopState, LoopSequence, Loop
import itertools
from collections import namedtuple

# We're only using one state for now, so declare it globally
g_sA = LoopState('A', {
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

# Input to loop seq coro
CoroData = namedtuple('CoroData', ('loopCount, setStartedLoops'))
# output of loop seq coro
ChangeSet = namedtuple('ChangeSet', ('setToTurnOff', 'setToTurnOn'))
# The loop seq coro
def LoopSeqCoro(sA):
    loopCount_old = -1
    #for lSeq in sA.diLoopSequences.values():
    #    for l in lSeq.loops:
    #        diPlayCounts_old[l.name] = -1;
    #print(diPlayCounts_old)
    with sA.Activate(None, None):
        changes = None
        while True:
            # Every iteration (but the first) yields a ChangeSet
            # containing the set of clips to turn on and off
            cData = yield changes
            changes = None

            if len(cData.setStartedLoops) == 0:
                continue

            # Here is where we'd check cData.loopCount against some cached value
            # to determine if we should advance the state graph, but hold off for now

            # Make a local copy of the last set of loops sent to the LM
            prevSet = set(sA.GetActiveLoopGen())
            
            # Walk the set of finished loops
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

# globally declare coro as well
g_LoopSeqCoro = LoopSeqCoro(g_sA)

def InitLoopManager(pLoopManager):
    # Global capture of state and coro
    global g_sA
    global g_LoopSeqCoro

    # Create the py LM and get the samples per mS
    LM = pylLM.LoopManager(pLoopManager)
    sampPerMS = int(LM.GetSampleRate() / 1000.)
 
    # For each loop in the state's loop sequences
    for lSeq in g_sA.diLoopSequences.values():
        for l in lSeq.loops:
            # Set tailfile to empty string if there is None
            if l.tailFile is None:
                l.tailFile = ''
            # Add the loop
            if  LM.AddLoop(l.name, l.headFile, l.tailFile, sampPerMS * l.fadeMS, l.vol) == False:
                raise IOError(l.name)
    
    # Prime the loop seq coro
    next(g_LoopSeqCoro)

    # Start the active loop seq
    messageList = [(pylLM.CMDStartLoop, (l.name, 0)) for l in g_sA.GetActiveLoopGen()]
    LM.SendMessages(messageList)

# Updates the coro, given the # of loops advanced since
# the last update and teh set of started loops (by name)
def Update(pLoopManager, loopCount, setStartedLoops):
    # We have nothing to do, get out
    if len(setStartedLoops) == 0:
        return

    # Construct LM, make set unicode
    LM = pylLM.LoopManager(pLoopManager)
    setStartedLoops = set(l.decode('unicode_escape') for l in setStartedLoops)

    # Capture the coro, send the new data and get the changes
    global g_LoopSeqCoro
    changeSets = g_LoopSeqCoro.send(CoroData(loopCount, setStartedLoops))

    # If there are changes, start and stop the appropriate loops
    if changeSets is not None:
        #print(changeSets)
        messageList = []
        for turnOff in changeSets.setToTurnOff:
            messageList.append((pylLM.CMDStopLoop, (turnOff.name, LM.GetMaxSampleCount())))
        for turnOn in changeSets.setToTurnOn:
            messageList.append((pylLM.CMDStartLoop, (turnOn.name, LM.GetMaxSampleCount())))
        LM.SendMessages(messageList)