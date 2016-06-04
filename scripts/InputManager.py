import sys, os
sys.path.append(os.path.dirname(os.path.realpath(__file__))+'/sdl2')
import events as SDLEvents
import keycode as SDLK

# Used to cast sdl event capsule to a pointer to the struct
import ctypes
def convert_capsule_to_int(capsule):
    ctypes.pythonapi.PyCapsule_GetPointer.restype = ctypes.c_void_p
    ctypes.pythonapi.PyCapsule_GetPointer.argtypes = [ctypes.py_object, ctypes.c_char_p]
    return ctypes.pythonapi.PyCapsule_GetPointer(capsule, None)

class Button:
    def __init__(self, code, fnDown, fnUp):
        # code should be hashable... should I have __hash__?
        self.code = code
        self.state = False

        self.fnDown = None
        if hasattr(fnDown, '__call__'):
            self.fnDown = fnDown

        self.fnUp = None
        if hasattr(fnUp, '__call__'):
            self.fnUp = fnUp

    def Toggle(self, mgr):
        oldState = self.state
        self.state = not(self.state)
        if oldState == False and self.fnDown is not None:
            self.fnDown(self, mgr)
        elif oldState and self.fnUp is not None:
            self.fnUp(self, mgr)

class KeyboardManager:
    def __init__(self, liKeys):
        if not(all(isinstance(k, Button) for k in liKeys)):
            raise TypeError('Error: All keys registered must be Buttons')

        # diKeys is initialized with the registered buttons
        # but will store bools for unregistered keys
        self.diKeys = {k.code : k for k in liKeys}

    def HandleKey(self, sdlEvent):
        # Get the SDL Keyboard Event
        keyEvent = sdlEvent.key
        keyCode = keyEvent.keysym.sym

        # I only care about non-repeated
        if keyEvent.repeat == False:
            # If we have this in our dict
            if keyCode in self.diKeys.keys():
                # If it's a registered button
                if isinstance(self.diKeys[keyCode], Button):
                    # Delegate to the button
                    self.diKeys[keyCode].Toggle(self)
                # Otherwise it should be a bool, so flip it
                else:
                    self.diKeys[keyCode] = not(self.diKeys[keyCode])
            # If it's new and it's a keydown event, add a bool to the dict
            elif sdlEvent.type == SDLEvents.SDL_KEYDOWN:
                self.diKeys[keyCode] = True

    # If a button was registered, return it, otherwise return None
    def GetButton(self, keyCode):
        if keyCode in self.diKeyStates.keys():
            if isinstance(self.diKeys[keyCode], Button):
                return self.diKeyStates[keyCode]
        return None

    # If a button was registered return it's state
    # Otherwise if we have it return the bool, else False
    def IsKeyDown(self, keyCode):
        if self.GetButton(keyCode) is not None:
            return self.GetButton(keyCode).state
        elif keyCode in self.diKeyStates.keys():
            return self.diKeyStates[keyCode]
        return False

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

    def HandleEvent(self, pSdlEvent):
        evtAddr = convert_capsule_to_int(pSdlEvent)
        sdlEvent = SDLEvents.SDL_Event.from_address(evtAddr)
        if sdlEvent.type == SDLEvents.SDL_QUIT:
            self.cScene.SetQuitFlag(True)
        elif (sdlEvent.type == SDLEvents.SDL_KEYDOWN or
                sdlEvent.type == SDLEvents.SDL_KEYUP):
            self.keyMgr.HandleKey(sdlEvent)
        elif (sdlEvent.type == SDLEvents.SDL_MOUSEBUTTONUP or
                sdlEvent.type == SDLEvents.SDL_MOUSEBUTTONDOWN or
                sdlEvent.type == SDLEvents.SDL_MOUSEMOTION):
            self.mouseMgr.HandleMouse(sdlEvent)
