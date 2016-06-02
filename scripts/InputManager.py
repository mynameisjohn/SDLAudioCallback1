import sys, os
sys.path.append(os.path.dirname(os.path.realpath(__file__))+'/sdl2')
import events as SDLEvents
import keycode as SDLK

class Button:
    def __init__(self, code, *args, **kwargs):
        self.code = code

        self.fnUp = None
        if 'fnUp' in kwargs.keys():
            if hasattr(kwargs['fnUp'], '__call__'):
                self.fnUp = kwargs['fnUp']

        self.fnDown = None
        if 'fnDown' in kwargs.keys():
            if hasattr(kwargs['fnDown'], '__call__'):
                self.fnDown = fnDown

        self.state = False
        if 'state' in kwargs.keys():
            self.state = bool(kwargs['state'])

    def Press(self, mgr):
        self.state = True
        if self.fnDown is not None:
            self.fnDown(self, mgr)

    def Release(self, mgr):
        self.state = False
        if self.fnUp is not None:
            self.fnUp(self, mgr)

class KeyboardManager:
    def __init__(self, liKeys):
        self.diKeys = {k.code : k for k in liKeys}

    def HandleKey(self, sdlEvent):
        keyEvent = sdlEvent.key
        keyCode = keyEvent.keysym.sym
        if sdlEvent.type == SDLEvents.SDL_KEYDOWN:
            if keyEvent.repeat == False:
                if keyCode in self.diKeys.keys():
                    self.diKeys[keyCode].Press(self)
        elif sdlEvent.type == SDLEvents.SDL_KEYUP:
            if keyEvent.repeat == False:
                if keyCode in self.diKeys.keys():
                    self.diKeys[keyCode].Release(self)

    def GetButton(self, keyCode):
        if keyCode in self.diKeyStates.keys():
            return self.diKeyStates[keyCode]
        raise IndexError('Unregistered button queried')

class MouseManager:
    def __init__(self, liButtons, motionCallback = None):
        self.diButtons = {m.code : m for m in liButtons}
        self.mousePos = [0, 0]
        if hasattr(motionCallback, '__call__'):
            self.motionCallback = motionCallback
        else:
            self.motionCallback = None
        
    def HandleMouse(self, sdlEvent):
        btn = sdlEvent.button.button
        if sdlEvent.type == SDLEvents.SDL_MOUSEBUTTONDOWN:
            if btn in self.diButtons.keys():
                self.diButtons[btn].Press(self)
        elif sdlEvent.type == SDLEvents.SDL_MOUSEBUTTONUP:
            if btn in self.diButtons.keys():
                self.diButtons[btn].Release(self)
        elif sdlEvent.type == SDLEvents.SDL_MOUSEMOTION:
            m = sdlEvent.motion
            self.mousePos = [m.x, m.y]
            if self.motionCallback is not None:
                self.motionCallback(self)

class InputManager:
    def __init__(self, cScene, keyMgr, mouseMgr):
        self.cScene = cScene
        self.keyMgr = keyMgr
        self.mouseMgr = mouseMgr

    def HandleEvent(self, sdlEvent):
        if sdlEvent.type == SDLEvents.SDL_QUIT:
            self.cScene.SetQuitFlag(True)
        elif (sdlEvent.type == SDLEvents.SDL_KEYDOWN or
                sdlEvent.type == SDLEvents.SDL_KEYUP):
            self.keyMgr.HandleKey(sdlEvent)
        elif (sdlEvent.type == SDLEvents.SDL_MOUSEBUTTONUP or
                sdlEvent.type == SDLEvents.SDL_MOUSEBUTTONDOWN or
                sdlEvent.type == SDLEvents.SDL_MOUSEMOTION):
            self.mouseMgr.HandleMouse(sdlEvent)
