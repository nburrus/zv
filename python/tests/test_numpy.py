from pathlib import Path

import math

import sys
sys.path.insert(0, str(Path(__file__).parent.parent.parent / 'build' / 'python'))

from zv import Viewer
import numpy as np

viewer = Viewer()
viewer.initialize()

blue_im = np.zeros((256,256,4), dtype=np.uint8)
blue_im[:,:,3] = 255
viewer.addImage ("All Black", blue_im)

red_im = np.zeros((256,256,4), dtype=np.uint8)
red_im[:,:,0] = 255
red_im[:,:,3] = 255
viewer.addImage ("Red im", red_im)

i = 0
while not viewer.exitRequested():
    red_im[:,:,0] = abs(math.sin(i/20.0))*255
    blue_im[:,:,2] = abs(math.cos(i/20.0))*255
    i += 1
    viewer.addImage ("Red im", red_im, replace=True)
    viewer.addImage ("Blue im", blue_im, replace=True)
    viewer.renderFrame(1.0 / 30.0)
