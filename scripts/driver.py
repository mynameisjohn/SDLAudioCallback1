# Local modules
import Init

# Global SoundManager instance
# I don't like having globals, but the buck
# stops somewhere I guess. Maybe I can add it
# to a pyl module somehow, but that won't help
g_SoundManager = None

# Initialize the scene and the sound manager
def Initialize(pScene):
    # It is a bit strange that initScene returns a
    # sound manager...
    global g_SoundManager
    g_SoundManager = Init.InitScene(pScene)

# Called every frame from C++
def Update(pScene):
    # All this does is update the loop graph for now
    global g_SoundManager
    g_SoundManager.Update()

# rudimentary input handling function
def HandleEvent(pEvent):
    # Delegate to the the loop graph's input manager
    g_SoundManager.HandleEvent(pEvent)
