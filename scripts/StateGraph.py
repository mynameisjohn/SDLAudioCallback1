import networkx as nx
import abc
import random
import contextlib

# Generic state class, must have a
# context management function and a name
class State(abc.ABC):
    def __init__(self, name):
        self.name = name

    def __hash__(self):
        return hash(self.name)

    def __eq__(self, other):
        return __eq__(self.name, other.name)

    def __repr__(self):
        return str(self.name)

    @abc.abstractmethod
    @contextlib.contextmanager
    def Activate(self, SG, prevState):
        yield

# A graph of states, edges denote possible transitions
class StateGraph:
    def __init__(self, graph, fnAdvance, initialState, **kwargs):
        # The graph, the initial state, and the advancement function
        self.G = graph
        self.activeState = initialState
        self._fnAdvance = fnAdvance
        
        # A coroutine that manages active state contexts
        def stateCoro(self):
            prevState = None
            if self.activeState is None:
                nextState = self._fnAdvance(self)
            else:
                nextState = self.activeState

            while True:
                self.activeState = nextState
                with self.activeState.Activate(self, prevState):
                    while nextState is self.activeState:
                        yield self.activeState
                        nextState = self._fnAdvance(self)
                prevState = self.activeState

        # Declare coro, do not prime (?)
        self._stateCoro = stateCoro(self)

        # Optional attrdict argument
        if kwargs is not None:
            for k, v in kwargs.items():
                setattr(self, k, v)

    # Returns the current active state
    def GetActiveState(self):
        return self.activeState

    # Returns next state without advancing
    def GetNextState(self):
        return self._fnAdvance(self)

    # Actually advance the state coro and return next state
    def AdvanceState(self):
        return next(self._stateCoro)

    # Just returns states in a container
    def GetAllStates(self):
        return self.G.nodes()