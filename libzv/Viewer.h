//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#pragma once

#include <libzv/Image.h>
#include <libzv/ImageWindowActions.h>

#include <memory>
#include <functional>
#include <vector>
#include <string>

namespace zv
{

class ImageWindow;
class ControlsWindow;
class ImageList;
using ImageId = int64_t;

struct ImageItem;
using ImageItemPtr = std::shared_ptr<ImageItem>;
using ImageItemUniquePtr = std::unique_ptr<ImageItem>;

struct ViewerState
{
    bool helpRequested = false;
    bool toggleControlsRequested = false;
    bool dismissRequested = false;
    bool openImageRequested = false;
    bool controlsRequestedForConfirmation = false;
    
    bool pendingChangesConfirmationRequested = false;
    std::function<void(void)> funcIfChangesConfirmed;
};

class Viewer
{
public:
    Viewer(const std::string& name, int index);
    ~Viewer();
    
    // Call it once, creates the context, etc.
    bool initialize ();
    
    void shutdown ();
    
    bool exitRequested () const;

    void renderFrame ();

public:
    ImageId addImageFromFile (const std::string& imagePath, bool replaceExisting = true);
    ImageId addImageData (const ImageSRGBA& image, const std::string& imageName, int insertPos = -1, bool replaceExisting = true);
    ImageId addPastedImage ();
    ImageId selectedImage () const;
    void selectImageIndex (int index);

    ImageItemPtr getImageItem (ImageId imageId) const;

    ImageId addImageItem (ImageItemUniquePtr imageItem, int insertPos, bool replaceExisting = true);

    using EventCallbackType = std::function<void(ImageId, float, float, void* userData)>;
    void setEventCallback (ImageId imageId, EventCallbackType callback, void* userData);
    
    using GlobalEventCallbackType = std::function<void(void*)>;
    void setGlobalEventCallback (const GlobalEventCallbackType& callback, void* userData);

    void setLayout (int nrows, int ncols);
    void runAction (ImageWindowAction action);

    void refreshPrettyFileNames ();    
        
protected:
    // Controller-like global methods that member windows can call.
    void onDismissRequested ();
    void onHelpRequested ();
    void onToggleControls ();
    void onImageWindowGeometryUpdated (const Rect& geometry);
    void onOpenImage ();
    void onControlsRequestedForConfirmation();

    void onSavePendingChangesConfirmed(Confirmation result, bool forcePathSelectionOnSave);

    void onAllChangesSaved (bool cancelled);

    void runAfterConfirmingPendingChanges (std::function<void(void)>&& func);

    ImageWindow* imageWindow();
    ControlsWindow* controlsWindow();
    ImageList& imageList();

    int globalIndex () const;
    const std::string& name() const;

    friend class ImageWindow;
    friend class ControlsWindow;

private:
    struct Impl;
    friend struct Impl;
    std::unique_ptr<Impl> impl;
};

} // zv
