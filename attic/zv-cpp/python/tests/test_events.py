#!/usr/bin/env python3
 
import zv
import sys

import random
import PyQt5.QtWidgets as QtWidgets

def callback(image_id, x, y, user_data):
    if zv.imgui.IsMouseClicked(zv.imgui.MouseButton.Left, False):
        control_str = "[Control] " if zv.imgui.IsKeyDown(zv.imgui.Key.LeftCtrl) else ""
        print (f"{control_str}Left button clicked {x} {y}!")
        print ("")
        viewer.runAction (zv.ImageWindowAction.Zoom_Normal)
    elif zv.imgui.IsMouseClicked(zv.imgui.MouseButton.Right, False):
        print (f"Right button clicked {x} {y}!")
        viewer.runAction (zv.ImageWindowAction.Zoom_Inc10p)    
    if zv.imgui.IsKeyPressed(zv.imgui.Key.K, False):
        print ("K was pressed!")
    elif zv.imgui.IsKeyPressed(zv.imgui.Key.D, False):
        print ("D was pressed!")

def create_qtapp():
    qtapp = QtWidgets.QApplication([])
    window = QtWidgets.QWidget()
    window.setWindowTitle("Test Events")
    layout = QtWidgets.QVBoxLayout()
    accept_button = QtWidgets.QPushButton('Accept')
    reject_button = QtWidgets.QPushButton('Reject')
    accept_button.clicked.connect(lambda: print ("Accepted"))
    reject_button.clicked.connect(lambda: print ("Rejected"))
    layout.addWidget(accept_button)
    layout.addWidget(reject_button)
    window.setLayout(layout)
    window.show()
    return qtapp, window


app = zv.App()
app.initialize(sys.argv)

viewer = app.getViewer()
image_id = viewer.selectedImage

viewer.setEventCallback(image_id, callback, None)

viewer.setLayout(1,2)

qtapp, window = create_qtapp()

while app.numViewers > 0:
    app.updateOnce(1.0 / 30.0)
    qtapp.processEvents()
