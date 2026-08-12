from pathlib import Path

import math

import sys
sys.path.insert(0, str(Path(__file__).parent.parent.parent / 'build' / 'python'))

import zv
import numpy as np

app = zv.App()
app.initialize(sys.argv)

viewer = app.getViewer()

blue_im = np.zeros((256,256,4), dtype=np.uint8)
blue_im[:,:,3] = 255
viewer.addImage ("All Black", blue_im)

red_im = np.zeros((256,256,3), dtype=np.float32)
red_im[:,:,0] = 1.0
viewer.addImage ("Red im", red_im)

gray_im = np.zeros((256,256), dtype=np.uint8)
gray_im[:,:] = 200
viewer.addImage ("Gray im", gray_im)

viewer.setLayout(2,2)

i = 0
while app.numViewers > 0:
    red_im[:,:,0] = abs(math.sin(i/20.0))*255
    blue_im[:,:,2] = abs(math.cos(i/20.0))*255
    i += 1
    viewer.addImage ("Red im", red_im, replace=True)
    viewer.addImage ("Blue im", blue_im, replace=True)
    app.updateOnce(1.0 / 30.0)
