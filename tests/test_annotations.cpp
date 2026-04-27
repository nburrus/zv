#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <libzv/Annotations.h>
#include <libzv/AnnotationTool.h>
#include <libzv/ImageList.h>
#include <libzv/ImguiUtils.h>
#include <libzv/MathUtils.h>
#include <libzv/Modifiers.h>

using namespace zv;

namespace {

// A widget transform that maps texture coords directly to widget coords with
// a fixed image-pixel-to-widget-pixel scale, with no zoom (uvRoi = full).
WidgetToImageTransform makeIdentityWidgetTransform(int imageWidth, int imageHeight)
{
    ImageWidgetRoi uvRoi{ImVec2(0, 0), ImVec2(1, 1)};
    Rect widgetRect = Rect::from_x_y_w_h(0, 0, imageWidth, imageHeight);
    return WidgetToImageTransform(uvRoi, widgetRect);
}

}

TEST_CASE("AnnotationElement moveBy applies normalized delta to lines and texts")
{
    LineAnnotationData ld;
    ld.textureLine = Line(Point(0.1, 0.2), Point(0.3, 0.4));
    AnnotationElement line(AnnotationId::nextId(), ld);

    line.moveBy(Point(0.05, -0.10));

    CHECK(line.asLine().textureLine.p1.x == doctest::Approx(0.15));
    CHECK(line.asLine().textureLine.p1.y == doctest::Approx(0.10));
    CHECK(line.asLine().textureLine.p2.x == doctest::Approx(0.35));
    CHECK(line.asLine().textureLine.p2.y == doctest::Approx(0.30));

    TextAnnotationData td;
    td.textureBox = Rect::from_x_y_w_h(0.4, 0.4, 0.2, 0.1);
    AnnotationElement text(AnnotationId::nextId(), td);

    text.moveBy(Point(0.1, 0.05));

    CHECK(text.asText().textureBox.origin.x == doctest::Approx(0.5));
    CHECK(text.asText().textureBox.origin.y == doctest::Approx(0.45));
    // Size unchanged.
    CHECK(text.asText().textureBox.size.x == doctest::Approx(0.2));
    CHECK(text.asText().textureBox.size.y == doctest::Approx(0.1));

    RectangleAnnotationData rd;
    rd.textureBox = Rect::from_x_y_w_h(0.2, 0.3, 0.4, 0.2);
    AnnotationElement rectangle(AnnotationId::nextId(), rd);

    rectangle.moveBy(Point(-0.1, 0.05));

    CHECK(rectangle.asRectangle().textureBox.origin.x == doctest::Approx(0.1));
    CHECK(rectangle.asRectangle().textureBox.origin.y == doctest::Approx(0.35));
    CHECK(rectangle.asRectangle().textureBox.size.x == doctest::Approx(0.4));
    CHECK(rectangle.asRectangle().textureBox.size.y == doctest::Approx(0.2));

    EllipseAnnotationData ed;
    ed.textureBox = Rect::from_x_y_w_h(0.1, 0.2, 0.3, 0.4);
    AnnotationElement ellipse(AnnotationId::nextId(), ed);

    ellipse.moveBy(Point(0.2, -0.1));

    CHECK(ellipse.asEllipse().textureBox.origin.x == doctest::Approx(0.3));
    CHECK(ellipse.asEllipse().textureBox.origin.y == doctest::Approx(0.1));
    CHECK(ellipse.asEllipse().textureBox.size.x == doctest::Approx(0.3));
    CHECK(ellipse.asEllipse().textureBox.size.y == doctest::Approx(0.4));
}

TEST_CASE("AnnotationElement moveHandleTo updates line endpoints")
{
    LineAnnotationData ld;
    ld.textureLine = Line(Point(0.1, 0.1), Point(0.5, 0.5));
    AnnotationElement line(AnnotationId::nextId(), ld);

    CHECK(line.numHandles() == 2);

    line.moveHandleTo(0, Point(0.2, 0.3));
    CHECK(line.asLine().textureLine.p1.x == doctest::Approx(0.2));
    CHECK(line.asLine().textureLine.p1.y == doctest::Approx(0.3));
    // p2 unchanged.
    CHECK(line.asLine().textureLine.p2.x == doctest::Approx(0.5));

    line.moveHandleTo(1, Point(0.7, 0.8));
    CHECK(line.asLine().textureLine.p2.x == doctest::Approx(0.7));
    CHECK(line.asLine().textureLine.p2.y == doctest::Approx(0.8));
}

TEST_CASE("AnnotationElement moveHandleTo resizes rectangle and ellipse corners")
{
    RectangleAnnotationData rd;
    rd.textureBox = Rect::from_x_y_w_h(0.2, 0.2, 0.4, 0.3);
    AnnotationElement rectangle(AnnotationId::nextId(), rd);

    REQUIRE(rectangle.numHandles() == 4);
    rectangle.moveHandleTo(2, Point(0.8, 0.7));
    CHECK(rectangle.asRectangle().textureBox.origin.x == doctest::Approx(0.2));
    CHECK(rectangle.asRectangle().textureBox.origin.y == doctest::Approx(0.2));
    CHECK(rectangle.asRectangle().textureBox.size.x == doctest::Approx(0.6));
    CHECK(rectangle.asRectangle().textureBox.size.y == doctest::Approx(0.5));

    rectangle.moveHandleTo(0, Point(0.1, 0.1));
    CHECK(rectangle.asRectangle().textureBox.origin.x == doctest::Approx(0.1));
    CHECK(rectangle.asRectangle().textureBox.origin.y == doctest::Approx(0.1));
    CHECK(rectangle.asRectangle().textureBox.size.x == doctest::Approx(0.7));
    CHECK(rectangle.asRectangle().textureBox.size.y == doctest::Approx(0.6));

    EllipseAnnotationData ed;
    ed.textureBox = Rect::from_x_y_w_h(0.3, 0.3, 0.3, 0.2);
    AnnotationElement ellipse(AnnotationId::nextId(), ed);

    REQUIRE(ellipse.numHandles() == 4);
    ellipse.moveHandleTo(3, Point(0.2, 0.8));
    CHECK(ellipse.asEllipse().textureBox.origin.x == doctest::Approx(0.2));
    CHECK(ellipse.asEllipse().textureBox.origin.y == doctest::Approx(0.3));
    CHECK(ellipse.asEllipse().textureBox.size.x == doctest::Approx(0.4));
    CHECK(ellipse.asEllipse().textureBox.size.y == doctest::Approx(0.5));
}

TEST_CASE("AnnotationElement moveHandleTo resizes text corners")
{
    TextAnnotationData td;
    td.textureBox = Rect::from_x_y_w_h(0.2, 0.2, 0.4, 0.3);
    td.fontSize = 24.f;
    AnnotationElement text(AnnotationId::nextId(), td);

    REQUIRE(text.numHandles() == 4);
    CHECK(text.handleTexturePos(0).x == doctest::Approx(0.2));
    CHECK(text.handleTexturePos(2).y == doctest::Approx(0.5));

    text.moveHandleTo(2, Point(0.8, 0.7));
    CHECK(text.asText().textureBox.origin.x == doctest::Approx(0.2));
    CHECK(text.asText().textureBox.origin.y == doctest::Approx(0.2));
    CHECK(text.asText().textureBox.size.x == doctest::Approx(0.6));
    CHECK(text.asText().textureBox.size.y == doctest::Approx(0.5));
    CHECK(text.asText().fontSize == doctest::Approx(24.f));

    text.moveHandleTo(0, Point(0.1, 0.1));
    CHECK(text.asText().textureBox.origin.x == doctest::Approx(0.1));
    CHECK(text.asText().textureBox.origin.y == doctest::Approx(0.1));
    CHECK(text.asText().textureBox.size.x == doctest::Approx(0.7));
    CHECK(text.asText().textureBox.size.y == doctest::Approx(0.6));
}

TEST_CASE("AnnotationDocument hit-test prioritizes handles over body")
{
    const int W = 100, H = 100;
    AnnotationDocument doc;

    LineAnnotationData ld;
    // Endpoints at (10,10) and (90,90) in widget pixels.
    ld.textureLine = Line(Point(0.1, 0.1), Point(0.9, 0.9));
    AnnotationId lineId = AnnotationId::nextId();
    doc.addLine(lineId, ld);

    auto t = makeIdentityWidgetTransform(W, H);

    // Click right on the p1 handle.
    auto onP1 = doc.hitTest(Point(10, 10), t, AnnotationId{},
                             /*handleRadius*/6.f, /*bodyTol*/3.f);
    CHECK(onP1.part == AnnotationHitResult::Part::Handle);
    CHECK(onP1.id == lineId);
    CHECK(onP1.handleIdx == 0);

    // Click at midpoint -> body hit.
    auto onBody = doc.hitTest(Point(50, 50), t, AnnotationId{}, 6.f, 3.f);
    CHECK(onBody.part == AnnotationHitResult::Part::Body);
    CHECK(onBody.id == lineId);

    // Click far away -> miss.
    auto miss = doc.hitTest(Point(99, 5), t, AnnotationId{}, 6.f, 3.f);
    CHECK(miss.part == AnnotationHitResult::Part::None);
}

TEST_CASE("AnnotationDocument hit-test selects rectangle and ellipse borders only")
{
    const int W = 200, H = 200;
    AnnotationDocument rectDoc;

    RectangleAnnotationData rd;
    rd.textureBox = Rect::from_x_y_w_h(0.2, 0.2, 0.5, 0.4); // (40,40)..(140,120)
    rd.strokeWidth = 6;
    AnnotationId rectangleId = AnnotationId::nextId();
    rectDoc.addRectangle(rectangleId, rd);

    auto t = makeIdentityWidgetTransform(W, H);

    auto rectBorder = rectDoc.hitTest(Point(90, 40), t, AnnotationId{}, 3.f, 2.f, W, H);
    CHECK(rectBorder.part == AnnotationHitResult::Part::Body);
    CHECK(rectBorder.id == rectangleId);

    auto rectInterior = rectDoc.hitTest(Point(90, 80), t, rectangleId, 3.f, 2.f, W, H);
    CHECK(rectInterior.part == AnnotationHitResult::Part::None);

    AnnotationDocument ellipseDoc;
    EllipseAnnotationData ed;
    ed.textureBox = Rect::from_x_y_w_h(0.2, 0.2, 0.5, 0.4);
    ed.strokeWidth = 6;
    AnnotationId ellipseId = AnnotationId::nextId();
    ellipseDoc.addEllipse(ellipseId, ed);

    auto ellipseBorder = ellipseDoc.hitTest(Point(140, 80), t, AnnotationId{}, 3.f, 2.f, W, H);
    CHECK(ellipseBorder.part == AnnotationHitResult::Part::Body);
    CHECK(ellipseBorder.id == ellipseId);

    auto ellipseInterior = ellipseDoc.hitTest(Point(90, 80), t, ellipseId, 3.f, 2.f, W, H);
    CHECK(ellipseInterior.part == AnnotationHitResult::Part::None);
}

TEST_CASE("AnnotationDocument hit-test prefers topmost element")
{
    const int W = 100, H = 100;
    AnnotationDocument doc;

    // Two overlapping text boxes covering the same widget region.
    TextAnnotationData td1;
    td1.textureBox = Rect::from_x_y_w_h(0.2, 0.2, 0.6, 0.6);
    AnnotationId bottomId = AnnotationId::nextId();
    doc.addText(bottomId, td1);

    TextAnnotationData td2;
    td2.textureBox = Rect::from_x_y_w_h(0.3, 0.3, 0.4, 0.4);
    AnnotationId topId = AnnotationId::nextId();
    doc.addText(topId, td2);

    auto t = makeIdentityWidgetTransform(W, H);

    auto hit = doc.hitTest(Point(50, 50), t, AnnotationId{}, 6.f, 3.f);
    CHECK(hit.part == AnnotationHitResult::Part::Body);
    CHECK(hit.id == topId);
}

TEST_CASE("AnnotationDocument hit-test gives selected line handle priority")
{
    const int W = 100, H = 100;
    AnnotationDocument doc;

    // A selected line below the text with an endpoint inside the top text box.
    // The selected handle should still win over the topmost text body.
    LineAnnotationData ld;
    ld.textureLine = Line(Point(0.0, 0.0), Point(0.5, 0.5));
    AnnotationId lineId = AnnotationId::nextId();
    doc.addLine(lineId, ld);

    // A text box drawn on top, covering the (10..90, 10..90) widget region.
    TextAnnotationData td;
    td.textureBox = Rect::from_x_y_w_h(0.1, 0.1, 0.8, 0.8);
    AnnotationId textId = AnnotationId::nextId();
    doc.addText(textId, td);

    auto t = makeIdentityWidgetTransform(W, H);

    auto hitWithSelection = doc.hitTest(Point(50, 50), t, lineId, 6.f, 3.f);
    CHECK(hitWithSelection.part == AnnotationHitResult::Part::Handle);
    CHECK(hitWithSelection.id == lineId);
    CHECK(hitWithSelection.handleIdx == 1);

    // Without selection, the topmost text body wins at the same point.
    auto hitNoSelection = doc.hitTest(Point(50, 50), t, AnnotationId{}, 6.f, 3.f);
    CHECK(hitNoSelection.part == AnnotationHitResult::Part::Body);
    CHECK(hitNoSelection.id == textId);
    CHECK(hitNoSelection.handleIdx == -1);
}

TEST_CASE("AnnotationDocument hit-test gives selected text handles priority over body")
{
    const int W = 100, H = 100;
    AnnotationDocument doc;

    TextAnnotationData td;
    td.textureBox = Rect::from_x_y_w_h(0.1, 0.1, 0.8, 0.8);
    AnnotationId textId = AnnotationId::nextId();
    doc.addText(textId, td);

    auto t = makeIdentityWidgetTransform(W, H);

    auto corner = doc.hitTest(Point(90, 90), t, textId, 6.f, 3.f, W, H);
    CHECK(corner.part == AnnotationHitResult::Part::Handle);
    CHECK(corner.id == textId);
    CHECK(corner.handleIdx == 2);

    auto body = doc.hitTest(Point(50, 50), t, textId, 6.f, 3.f, W, H);
    CHECK(body.part == AnnotationHitResult::Part::Body);
    CHECK(body.id == textId);
    CHECK(body.handleIdx == -1);
}

TEST_CASE("fitTextAnnotationFontSizeToPixelBox scales to the limiting box dimension")
{
    CHECK(fitTextAnnotationFontSizeToPixelBox(ImVec2(200, 50), 20.f, ImVec2(100, 100))
          == doctest::Approx(10.f));
    CHECK(fitTextAnnotationFontSizeToPixelBox(ImVec2(100, 200), 20.f, ImVec2(100, 50))
          == doctest::Approx(5.f));
    CHECK(fitTextAnnotationFontSizeToPixelBox(ImVec2(100, 100), 20.f, ImVec2(400, 400), 1.f, 60.f)
          == doctest::Approx(60.f));
}

TEST_CASE("AnnotationDocument hit-test honors stroke-width tolerance for lines")
{
    const int W = 200, H = 200;
    AnnotationDocument doc;

    LineAnnotationData ld;
    ld.textureLine = Line(Point(0.0, 0.5), Point(1.0, 0.5)); // y=100 in widget
    ld.strokeWidth = 10;
    doc.addLine(AnnotationId::nextId(), ld);

    auto t = makeIdentityWidgetTransform(W, H);

    // Click 4px above the line: within the stroke-width tolerance (5) so hit.
    auto hit = doc.hitTest(Point(100, 96), t, AnnotationId{},
                           /*handleRadius*/3.f, /*bodyTol*/0.f);
    CHECK(hit.part == AnnotationHitResult::Part::Body);

    // Click 20px above the line: well outside tolerance, miss.
    auto miss = doc.hitTest(Point(100, 80), t, AnnotationId{}, 3.f, 0.f);
    CHECK(miss.part == AnnotationHitResult::Part::None);
}

// ---------------------------------------------------------------------------
// ModifiedImage annotation integration. These don't exercise compositing
// (which needs a GL context) — only the bookkeeping around document
// ownership, pending-changes, discard, and addModifier resetting state.
// ---------------------------------------------------------------------------

TEST_CASE("ModifiedImage tracks annotations as pending changes")
{
    auto item = std::make_shared<ImageItem>();
    auto data = std::make_shared<ImageItemData>();
    ModifiedImage mi(item, data);

    CHECK_FALSE(mi.hasPendingChanges());
    CHECK_FALSE(mi.hasAnnotations());

    LineAnnotationData ld;
    mi.annotations().addLine(AnnotationId::nextId(), ld);
    mi.markAnnotationsDirty();

    CHECK(mi.hasAnnotations());
    CHECK(mi.hasPendingChanges());
}

TEST_CASE("ModifiedImage discardChanges clears annotations")
{
    auto item = std::make_shared<ImageItem>();
    auto data = std::make_shared<ImageItemData>();
    ModifiedImage mi(item, data);

    TextAnnotationData td;
    td.text = "hello";
    mi.annotations().addText(AnnotationId::nextId(), td);
    mi.markAnnotationsDirty();
    REQUIRE(mi.hasAnnotations());

    mi.discardChanges();

    CHECK_FALSE(mi.hasAnnotations());
    CHECK_FALSE(mi.hasPendingChanges());
    CHECK(mi.annotations().empty());
}

// ---------------------------------------------------------------------------
// AnnotationTool fan-out, deletion, and undo. These don't drive any ImGui
// drag — they invoke the same commit/delete paths that the live tool runs
// once a drag finishes.
// ---------------------------------------------------------------------------

namespace {

struct ToolFixture
{
    std::shared_ptr<ImageItem> itemA = std::make_shared<ImageItem>();
    std::shared_ptr<ImageItem> itemB = std::make_shared<ImageItem>();
    std::shared_ptr<ImageItemData> dataA = std::make_shared<ImageItemData>();
    std::shared_ptr<ImageItemData> dataB = std::make_shared<ImageItemData>();
    ModifiedImage imA{itemA, dataA};
    ModifiedImage imB{itemB, dataB};
    AnnotationTool tool;

    ToolFixture()
    {
        tool.setApplyToVisibleImagesFunc([this](const std::function<void(ModifiedImage&)>& op) {
            op(imA);
            op(imB);
        });
    }
};

} // namespace

TEST_CASE("AnnotationTool commitNewLine adds matching ids to all visible images")
{
    ToolFixture f;

    LineAnnotationData ld;
    ld.textureLine = Line(Point(0.1, 0.2), Point(0.4, 0.5));
    ld.endStyle = LineEndpointStyle::Arrow;
    ld.strokeStyle = AnnotationStrokeStyle::Dashed;
    AnnotationId createdId = f.tool.commitNewLine(ld);

    REQUIRE(createdId.isValid());
    CHECK(f.tool.selectedId() == createdId);

    auto* a = f.imA.annotations().findById(createdId);
    auto* b = f.imB.annotations().findById(createdId);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    CHECK(a->kind() == AnnotationElement::Kind::Line);
    CHECK(b->kind() == AnnotationElement::Kind::Line);
    CHECK(a->asLine().textureLine.p1.x == doctest::Approx(0.1));
    CHECK(b->asLine().textureLine.p2.y == doctest::Approx(0.5));
    CHECK(a->asLine().startStyle == LineEndpointStyle::None);
    CHECK(a->asLine().endStyle == LineEndpointStyle::Arrow);
    CHECK(b->asLine().strokeStyle == AnnotationStrokeStyle::Dashed);
}

TEST_CASE("Arrow-style line remains a line annotation")
{
    LineAnnotationData ld;
    ld.textureLine = Line(Point(0.1, 0.1), Point(0.9, 0.9));
    ld.endStyle = LineEndpointStyle::Arrow;
    ld.strokeStyle = AnnotationStrokeStyle::Dotted;

    AnnotationElement arrow(AnnotationId::nextId(), ld);

    CHECK(arrow.kind() == AnnotationElement::Kind::Line);
    CHECK(arrow.asLine().endStyle == LineEndpointStyle::Arrow);
    CHECK(arrow.asLine().strokeStyle == AnnotationStrokeStyle::Dotted);
}

TEST_CASE("AnnotationTool commitNewRectangle and commitNewEllipse add matching ids to all visible images")
{
    ToolFixture f;

    RectangleAnnotationData rd;
    rd.textureBox = Rect::from_x_y_w_h(0.1, 0.2, 0.3, 0.4);
    AnnotationId rectangleId = f.tool.commitNewRectangle(rd);

    EllipseAnnotationData ed;
    ed.textureBox = Rect::from_x_y_w_h(0.3, 0.2, 0.4, 0.2);
    AnnotationId ellipseId = f.tool.commitNewEllipse(ed);

    REQUIRE(rectangleId.isValid());
    REQUIRE(ellipseId.isValid());
    CHECK(f.tool.selectedId() == ellipseId);

    auto* rectA = f.imA.annotations().findById(rectangleId);
    auto* rectB = f.imB.annotations().findById(rectangleId);
    auto* ellA = f.imA.annotations().findById(ellipseId);
    auto* ellB = f.imB.annotations().findById(ellipseId);
    REQUIRE(rectA != nullptr);
    REQUIRE(rectB != nullptr);
    REQUIRE(ellA != nullptr);
    REQUIRE(ellB != nullptr);
    CHECK(rectA->kind() == AnnotationElement::Kind::Rectangle);
    CHECK(rectB->asRectangle().textureBox.size.y == doctest::Approx(0.4));
    CHECK(ellA->kind() == AnnotationElement::Kind::Ellipse);
    CHECK(ellB->asEllipse().textureBox.origin.x == doctest::Approx(0.3));
}

TEST_CASE("AnnotationTool cancelCurrentAction consumes placement modes")
{
    AnnotationTool tool;

    CHECK(!tool.cancelCurrentAction());

    tool.setMode(AnnotationTool::Mode::AddText);
    CHECK(tool.cancelCurrentAction());
    CHECK(tool.mode() == AnnotationTool::Mode::Select);
    CHECK(!tool.cancelCurrentAction());
}

TEST_CASE("AnnotationTool deleteSelected removes the line from all visible images")
{
    ToolFixture f;

    LineAnnotationData ld;
    ld.textureLine = Line(Point(0.0, 0.0), Point(0.7, 0.7));
    AnnotationId createdId = f.tool.commitNewLine(ld);
    REQUIRE(f.imA.annotations().findById(createdId) != nullptr);
    REQUIRE(f.imB.annotations().findById(createdId) != nullptr);

    f.tool.deleteSelected();

    CHECK(f.imA.annotations().findById(createdId) == nullptr);
    CHECK(f.imB.annotations().findById(createdId) == nullptr);
    CHECK(!f.tool.selectedId().isValid());
}

TEST_CASE("AnnotationTool undo restores after create")
{
    ToolFixture f;

    LineAnnotationData ld;
    ld.textureLine = Line(Point(0.2, 0.2), Point(0.6, 0.6));
    AnnotationId createdId = f.tool.commitNewLine(ld);

    REQUIRE(f.imA.canUndo());
    REQUIRE(f.imB.canUndo());

    f.imA.undoLastChange();
    f.imB.undoLastChange();

    CHECK(f.imA.annotations().findById(createdId) == nullptr);
    CHECK(f.imB.annotations().findById(createdId) == nullptr);
    CHECK(!f.imA.canUndo());
    CHECK(!f.imB.canUndo());
}

TEST_CASE("AnnotationTool undo restores after delete")
{
    ToolFixture f;

    LineAnnotationData ld;
    ld.textureLine = Line(Point(0.3, 0.3), Point(0.8, 0.8));
    ld.strokeWidth = 5;
    ld.startStyle = LineEndpointStyle::Arrow;
    ld.endStyle = LineEndpointStyle::Arrow;
    ld.strokeStyle = AnnotationStrokeStyle::Dotted;
    AnnotationId createdId = f.tool.commitNewLine(ld);

    f.tool.deleteSelected();
    REQUIRE(f.imA.annotations().findById(createdId) == nullptr);
    REQUIRE(f.imB.annotations().findById(createdId) == nullptr);

    // Top of stack is the delete-undo (re-adds). Below it is the create-undo.
    f.imA.undoLastChange();
    f.imB.undoLastChange();

    auto* a = f.imA.annotations().findById(createdId);
    auto* b = f.imB.annotations().findById(createdId);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    CHECK(a->asLine().strokeWidth == 5);
    CHECK(a->asLine().startStyle == LineEndpointStyle::Arrow);
    CHECK(a->asLine().endStyle == LineEndpointStyle::Arrow);
    CHECK(a->asLine().strokeStyle == AnnotationStrokeStyle::Dotted);
    CHECK(b->asLine().textureLine.p1.x == doctest::Approx(0.3));
}
