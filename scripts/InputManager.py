class Button:
    def __init__(self, code, **args, **kwargs):
        self.code = code

        self.fnUp = None
        if 'fnUp' in kwargs.keys():
            if hasattr(kwargs['fnUp'], '__call__'):
                self.fnUp = fnUp

        self.fnDown = None
        if 'fnDown' in kwargs.keys():
            if hasattr(kwargs['fnDown', '__call__'):
                self.fnDown = fnDown

        self.state = False
        if 'state' in kwargs.keys():
            self.state = bool(kwargs['state'])

    def Press(self, kbdMgr):
        self.state = True
        if self.fnDown is not None:
            self.fnDown(self, kbdMgr):

    def Release(self, kbdMgr):
        self.state = False
        if self.fnUp is not None:
            self.fnUp(self, kbdMgr)

class KeyboardManager:
    def __init__(self, diKeys):
        self.diKeys = dict(diKeys)

    def HandleKey(self, sdlEvent):
        keyEvent = sdlEvent.key
        keyCode = keyEvent.keysym.sym
        if sdlEvent.type == SDL_KEYDOWN:
            if keyEvent.repeat is False:
                if keyCode in self.diKeys.keys():
                    self.diKeys[keyCode].Press(self)
        elif sdlEvent.type == SDL_KEYUP:
            if keyEvent.repeat is False:
                if keyCode in self.diKeys.keys():
                    self.diKeys[keyCode].Release(self)

    def GetButton(self, keyCode):
        if keyCode in self.diKeyStates.keys():
            return self.diKeyStates[keyCode]
        raise IndexError('Unregistered button queried')

class MouseManager:
    def __init__(self, diButtons, motionCallback = None):
        self.diButtons = dict(diButtons)
        self.mousePos = [0, 0]
        if hasattr(motionCallback, '__call__'):
            self.motionCallback = motionCallback
        else:
            self.motionCallback = None
        
    def HandleMouse(sdlEvent):
        btn = sdlEvent.button.button
        if sdlEvent.type is SDL_MOUSEBUTTONDOWN:
            if btn in self.diButtons.keys():
                self.diButtons[btn].Press(self)
        elif sdlEvent.type is SDL_MOUSEBUTTONUP:
            if btn in self.diButtons.keys():
                self.diButtons[btn].Release(self)
        elif sdlEvent.type is SDL_MOUSEMOTION:
            m = sdlEvent.motion
            self.mousePos = [m.x, m.y]
            if self.motionCallback is not None:
                self.motionCallback(self)

class InputManager:
    def __init__(self, keyMgr, mouseMgr):
        self.keyMgr = kMgr
        self.mouseMgr = mouseMgr
        self.bQuit = False

    def HandleEvent(self, sdlEvent):
        if sdlEvent.type is SDL_QUIT:
            self.bQuit = True
        elif (sdlEvent.type is SDL_KEYDOWN or
                sdlEvent.type is SDL_KEYUP):
            self.keyMgr.HandleKey(sdlEvent)
        elif (sdlEvent.type is SDL_MOUSEBUTTONUP or
                sdlEvent.type is SDL_MOUSEBUTTONDOWN or
                sdlEvent.type is SDL_MOUSEMOTION):
            self.mouseMgr.HandleMouse(sdlEvent)
