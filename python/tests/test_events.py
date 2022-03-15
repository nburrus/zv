#!/usr/bin/env python3
 
import zv
import sys

def callback(image_id, x, y, user_data):
    if zv.imgui.IsMouseClicked(zv.imgui.MouseButton.Left, False):
        control_str = "[Control] " if zv.imgui.IsKeyDown(zv.imgui.Key.LeftCtrl) else ""
        print (f"{control_str}Left button clicked {x} {y}!")
        print ("")
        viewer.runAction (zv.ImageWindowAction.Zoom_Normal)
    elif zv.imgui.IsMouseClicked(zv.imgui.MouseButton.Right, False):
        print (f"Right button clicked {x} {y}!")
        viewer.runAction (zv.ImageWindowAction.Zoom_Inc10p)

app = zv.App()
app.initialize(sys.argv)

viewer = app.getViewer()
image_id = viewer.selectedImage

viewer.setEventCallback(image_id, callback, None)

viewer.setLayout(1,2)

while app.numViewers > 0:
    app.updateOnce(1.0 / 30.0)
