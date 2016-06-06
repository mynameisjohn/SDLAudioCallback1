# Local modules
import Init

# Global LoopManager instance
# I don't like having globals, but the buck
# stops somewhere I guess. Maybe I can add it
# to a pyl module somehow, but that won't help
g_LoopManager = None

# Initialize the scene and the sound manager
def Initialize(pScene):
    # It is a bit strange that initScene returns a
    # sound manager...
    global g_LoopManager
    g_LoopManager = Init.InitScene(pScene)

# Called every frame from C++
def Update(pScene):
    # All this does is update the loop graph for now
    global g_LoopManager
    g_LoopManager.Update()

# rudimentary input handling function
def HandleEvent(pEvent):
    # Delegate to the the loop graph's input manager
    global g_LoopManager
    g_LoopManager.HandleEvent(pEvent)