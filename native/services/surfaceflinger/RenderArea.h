#pragma once

#include <ui/GraphicTypes.h>
#include <ui/Transform.h>

#include <functional>

#include "FrontEnd/LayerSnapshot.h"
#include "Layer.h"

namespace android {

class DisplayDevice;

// RenderArea describes a rectangular area that layers can be rendered to.
//
// There is a logical render area and a physical render area.  When a layer is
// rendered to the render area, it is first transformed and clipped to the logical
// render area.  The transformed and clipped layer is then projected onto the
// physical render area.
class RenderArea {
public:
    enum class CaptureFill {CLEAR, OPAQUE};
    enum class Options {
        // If not set, the secure layer would be blacked out or skipped
        // when rendered to an insecure render area
        CAPTURE_SECURE_LAYERS = 1 << 0,

        // If set, the render result may be used for system animations
        // that must preserve the exact colors of the display
        HINT_FOR_SEAMLESS_TRANSITION = 1 << 1,
    };
    static float getCaptureFillValue(CaptureFill captureFill);

    RenderArea(ui::Size reqSize, CaptureFill captureFill, ui::Dataspace reqDataSpace,
               ftl::Flags<Options> options)
          : mOptions(options),
            mReqSize(reqSize),
            mReqDataSpace(reqDataSpace),
            mCaptureFill(captureFill) {}

    static std::function<std::vector<std::pair<Layer*, sp<LayerFE>>>()> fromTraverseLayersLambda(
            std::function<void(const LayerVector::Visitor&)> traverseLayers) {
        return [traverseLayers = std::move(traverseLayers)]() {
            std::vector<std::pair<Layer*, sp<LayerFE>>> layers;
            traverseLayers([&](Layer* layer) {
                // Layer::prepareClientComposition uses the layer's snapshot to populate the
                // resulting LayerSettings. Calling Layer::updateSnapshot ensures that LayerSettings
                // are generated with the layer's current buffer and geometry.
                layer->updateSnapshot(true /* updateGeometry */);
                layers.emplace_back(layer, layer->copyCompositionEngineLayerFE());
            });
            return layers;
        };
    }

    virtual ~RenderArea() = default;

    // Returns true if the render area is secure.  A secure layer should be
    // blacked out / skipped when rendered to an insecure render area.
    virtual bool isSecure() const = 0;

    // Returns the transform to be applied on layers to transform them into
    // the logical render area.
    virtual const ui::Transform& getTransform() const = 0;

    // Returns the source crop of the render area.  The source crop defines
    // how layers are projected from the logical render area onto the physical
    // render area.  It can be larger than the logical render area.  It can
    // also be optionally rotated.
    //
    // The source crop is specified in layer space (when rendering a layer and
    // its children), or in layer-stack space (when rendering all layers visible
    // on the display).
    virtual Rect getSourceCrop() const = 0;

    // Returns the size of the physical render area.
    int getReqWidth() const { return mReqSize.width; }
    int getReqHeight() const { return mReqSize.height; }

    // Returns the composition data space of the render area.
    ui::Dataspace getReqDataSpace() const { return mReqDataSpace; }

    // Returns the fill color of the physical render area.  Regions not
    // covered by any rendered layer should be filled with this color.
    CaptureFill getCaptureFill() const { return mCaptureFill; }

    virtual sp<const DisplayDevice> getDisplayDevice() const = 0;

    // If this is a LayerRenderArea, return the root layer of the
    // capture operation.
    virtual sp<Layer> getParentLayer() const { return nullptr; }

    // If this is a LayerRenderArea, return the layer snapshot
    // of the root layer of the capture operation
    virtual const frontend::LayerSnapshot* getLayerSnapshot() const { return nullptr; }

    // Returns whether the render result may be used for system animations that
    // must preserve the exact colors of the display.
    bool getHintForSeamlessTransition() const {
        return mOptions.test(Options::HINT_FOR_SEAMLESS_TRANSITION);
    }

protected:
    ftl::Flags<Options> mOptions;

private:
    const ui::Size mReqSize;
    const ui::Dataspace mReqDataSpace;
    const CaptureFill mCaptureFill;
};

} // namespace android
