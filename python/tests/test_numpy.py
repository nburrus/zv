from pathlib import Path

import sys
sys.path.insert(0, str(Path(__file__).parent.parent.parent / 'build' / 'python'))

from zv import Viewer
import numpy as np

viewer = Viewer()
viewer.initialize()

black_im = np.zeros((256,256,4), dtype=np.uint8)
black_im[:,:,3] = 255
viewer.addImage ("All Black", black_im)

red_im = np.zeros((256,256,4), dtype=np.uint8)
red_im[:,:,0] = 255
red_im[:,:,3] = 255
viewer.addImage ("Red im", red_im)
viewer.addImage ("Red im", black_im)

while not viewer.exitRequested():
    viewer.renderFrame(1.0 / 30.0)
