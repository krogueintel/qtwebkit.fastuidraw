/*
 * Copyright (C) 2006 Apple Computer, Inc.
 * Copyright (C) 2006 Alexander Kellett <lypanov@kde.org>
 * Copyright (C) 2006 Oliver Hunt <ojh16@student.canterbury.ac.nz>
 * Copyright (C) 2007 Nikolas Zimmermann <zimmermann@kde.org>
 * Copyright (C) 2008 Rob Buis <buis@kde.org>
 * Copyright (C) 2009 Dirk Schulze <krit@webkit.org>
 * Copyright (C) Research In Motion Limited 2010-2012. All rights reserved.
 * Copyright (C) 2012 Google Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "config.h"

#if ENABLE(SVG)
#include "RenderSVGText.h"

#include "FloatConversion.h"
#include "FloatQuad.h"
#include "FontCache.h"
#include "GraphicsContext.h"
#include "HitTestRequest.h"
#include "LayoutRepainter.h"
#include "PointerEventsHitRules.h"
#include "RenderSVGInlineText.h"
#include "RenderSVGResource.h"
#include "RenderSVGRoot.h"
#include "SVGLengthList.h"
#include "SVGRenderSupport.h"
#include "SVGResourcesCache.h"
#include "SVGRootInlineBox.h"
#include "SVGTextElement.h"
#include "SVGTextLayoutAttributesBuilder.h"
#include "SVGTextRunRenderingContext.h"
#include "SVGTransformList.h"
#include "SVGURIReference.h"
#include "SimpleFontData.h"
#include "TransformState.h"
#include "VisiblePosition.h"

namespace WebCore {

RenderSVGText::RenderSVGText(SVGTextElement* node) 
    : RenderSVGBlock(node)
    , m_needsReordering(false)
    , m_needsPositioningValuesUpdate(true)
    , m_needsTransformUpdate(true)
    , m_needsTextMetricsUpdate(true)
{
}

bool RenderSVGText::isChildAllowed(RenderObject* child, RenderStyle*) const
{
    return child->isInline();
}

RenderSVGText* RenderSVGText::locateRenderSVGTextAncestor(RenderObject* start)
{
    ASSERT(start);
    while (start && !start->isSVGText())
        start = start->parent();
    if (!start || !start->isSVGText())
        return 0;
    return toRenderSVGText(start);
}

const RenderSVGText* RenderSVGText::locateRenderSVGTextAncestor(const RenderObject* start)
{
    ASSERT(start);
    while (start && !start->isSVGText())
        start = start->parent();
    if (!start || !start->isSVGText())
        return 0;
    return toRenderSVGText(start);
}

LayoutRect RenderSVGText::clippedOverflowRectForRepaint(RenderBoxModelObject* repaintContainer) const
{
    return SVGRenderSupport::clippedOverflowRectForRepaint(this, repaintContainer);
}

void RenderSVGText::computeRectForRepaint(RenderBoxModelObject* repaintContainer, LayoutRect& rect, bool fixed) const
{
    FloatRect repaintRect = rect;
    computeFloatRectForRepaint(repaintContainer, repaintRect, fixed);
    rect = enclosingLayoutRect(repaintRect);
}

void RenderSVGText::computeFloatRectForRepaint(RenderBoxModelObject* repaintContainer, FloatRect& repaintRect, bool fixed) const
{
    SVGRenderSupport::computeFloatRectForRepaint(this, repaintContainer, repaintRect, fixed);
}

void RenderSVGText::mapLocalToContainer(RenderBoxModelObject* repaintContainer, bool /* fixed */, bool /* useTransforms */, TransformState& transformState, ApplyContainerFlipOrNot, bool* wasFixed) const
{
    SVGRenderSupport::mapLocalToContainer(this, repaintContainer, transformState, wasFixed);
}

void RenderSVGText::subtreeChildAdded(RenderObject* child)
{
    ASSERT(child);
    if (m_needsPositioningValuesUpdate)
        return;

    // The positioning elements cache doesn't include the new 'child' yet. Clear the
    // cache, as the next buildLayoutAttributesForTextRenderer() call rebuilds it.
    invalidateTextPositioningElements();

    FontCachePurgePreventer fontCachePurgePreventer;
    for (RenderObject* descendant = child; descendant; descendant = descendant->nextInPreOrder(child)) {
        if (descendant->isSVGInlineText())
            m_layoutAttributesBuilder.buildLayoutAttributesForTextRenderer(toRenderSVGInlineText(descendant));
    }

    rebuildLayoutAttributes();
}

static inline bool findPreviousAndNextAttributes(RenderObject* start, RenderSVGInlineText* locateElement, bool& stopAfterNext, SVGTextLayoutAttributes*& previous, SVGTextLayoutAttributes*& next)
{
    ASSERT(start);
    ASSERT(locateElement);
    for (RenderObject* child = start->firstChild(); child; child = child->nextSibling()) {
        if (child->isSVGInlineText()) {
            RenderSVGInlineText* text = toRenderSVGInlineText(child);
            if (locateElement != text) {
                if (stopAfterNext) {
                    next = text->layoutAttributes();
                    return true;
                }

                previous = text->layoutAttributes();
                continue;
            }

            stopAfterNext = true;
            continue;
        }

        if (!child->isSVGInline())
            continue;

        if (findPreviousAndNextAttributes(child, locateElement, stopAfterNext, previous, next))
            return true;
    }

    return false;
}

void RenderSVGText::subtreeChildWillBeDestroyed(RenderSVGInlineText* text, Vector<SVGTextLayoutAttributes*>& affectedAttributes)
{
    ASSERT(text);

    // The positioning elements cache depends on the size of each text renderer in the
    // subtree. If this changes, clear the cache. It's going to be rebuilt below.
    invalidateTextPositioningElements();

    if (m_needsPositioningValuesUpdate)
        return;

    // This logic requires that the 'text' child is still inserted in the tree.
    bool stopAfterNext = false;
    SVGTextLayoutAttributes* previous = 0;
    SVGTextLayoutAttributes* next = 0;
    findPreviousAndNextAttributes(this, text, stopAfterNext, previous, next);
    if (previous)
        affectedAttributes.append(previous);
    if (next)
        affectedAttributes.append(next);

    SVGTextLayoutAttributes* currentLayoutAttributes = text->layoutAttributes();

    size_t position = m_layoutAttributes.find(currentLayoutAttributes);
    ASSERT(position != notFound);
    m_layoutAttributes.remove(position);

    ASSERT(!m_layoutAttributes.contains(currentLayoutAttributes));
}

static inline void recursiveCollectLayoutAttributes(RenderObject* start, Vector<SVGTextLayoutAttributes*>& attributes)
{
    for (RenderObject* child = start->firstChild(); child; child = child->nextSibling()) {
        if (child->isSVGInlineText()) {
            attributes.append(toRenderSVGInlineText(child)->layoutAttributes());
            continue;
        }

        recursiveCollectLayoutAttributes(child, attributes);
    }
}

static inline void checkLayoutAttributesConsistency(RenderSVGText* text, Vector<SVGTextLayoutAttributes*>& expectedLayoutAttributes)
{
#ifndef NDEBUG
    Vector<SVGTextLayoutAttributes*> newLayoutAttributes;
    recursiveCollectLayoutAttributes(text, newLayoutAttributes);
    ASSERT(newLayoutAttributes == expectedLayoutAttributes);
#else
    UNUSED_PARAM(text);
    UNUSED_PARAM(expectedLayoutAttributes);
#endif
}

void RenderSVGText::subtreeChildWasDestroyed(RenderSVGInlineText*, Vector<SVGTextLayoutAttributes*>& affectedAttributes)
{
    if (documentBeingDestroyed() || affectedAttributes.isEmpty())
        return;

    checkLayoutAttributesConsistency(this, m_layoutAttributes);

    size_t size = affectedAttributes.size();
    for (size_t i = 0; i < size; ++i)
        m_layoutAttributesBuilder.rebuildMetricsForTextRenderer(affectedAttributes[i]->context());
}

void RenderSVGText::subtreeStyleChanged(RenderSVGInlineText* text)
{
    ASSERT(text);
    if (m_needsPositioningValuesUpdate)
        return;

    // Only update the metrics cache, but not the text positioning element cache
    // nor the layout attributes cached in the leaf #text renderers.
    FontCachePurgePreventer fontCachePurgePreventer;
    for (RenderObject* descendant = text; descendant; descendant = descendant->nextInPreOrder(text)) {
        if (descendant->isSVGInlineText())
            m_layoutAttributesBuilder.rebuildMetricsForTextRenderer(toRenderSVGInlineText(descendant));
    }
}

void RenderSVGText::subtreeTextChanged(RenderSVGInlineText* text)
{
    ASSERT(text);

    // The positioning elements cache depends on the size of each text renderer in the
    // subtree. If this changes, clear the cache. It's going to be rebuilt below.
    invalidateTextPositioningElements();

    if (m_needsPositioningValuesUpdate)
        return;

    FontCachePurgePreventer fontCachePurgePreventer;
    for (RenderObject* descendant = text; descendant; descendant = descendant->nextInPreOrder(text)) {
        if (descendant->isSVGInlineText())
            m_layoutAttributesBuilder.buildLayoutAttributesForTextRenderer(toRenderSVGInlineText(descendant));
    }
}

void RenderSVGText::invalidateTextPositioningElements()
{
    // Clear the text positioning elements. This should be called when either the children
    // of a DOM text element have changed, or the length of the text in any child element
    // has changed. Failure to clear may leave us with invalid elements, as other code paths
    // do not always cause the position elements to be marked invalid before use.
    m_layoutAttributesBuilder.clearTextPositioningElements();
}

void RenderSVGText::layout()
{
    ASSERT(needsLayout());
    LayoutRepainter repainter(*this, checkForRepaintDuringLayout());

    bool updateCachedBoundariesInParents = false;
    if (m_needsTransformUpdate) {
        SVGTextElement* text = static_cast<SVGTextElement*>(node());
        m_localTransform = text->animatedLocalTransform();
        m_needsTransformUpdate = false;
        updateCachedBoundariesInParents = true;
    }

    // If the root layout size changed (eg. window size changes) or the positioning values change
    // or the transform to the root context has changed then recompute the on-screen font size.
    if (m_needsTextMetricsUpdate || SVGRenderSupport::findTreeRootObject(this)->isLayoutSizeChanged()) {
        for (RenderObject* descendant = this; descendant; descendant = descendant->nextInPreOrder(this)) {
            if (descendant->isSVGInlineText())
                toRenderSVGInlineText(descendant)->updateScaledFont();
        }

        rebuildAllLayoutAttributes();
        updateCachedBoundariesInParents = true;
        m_needsTextMetricsUpdate = false;
    }

    if (m_needsPositioningValuesUpdate) {
        // Perform SVG text layout phase one (see SVGTextLayoutAttributesBuilder for details).
        m_layoutAttributesBuilder.buildLayoutAttributesForWholeTree(this);
        m_needsReordering = true;
        m_needsPositioningValuesUpdate = false;
        updateCachedBoundariesInParents = true;
    }

    // Reduced version of RenderBlock::layoutBlock(), which only takes care of SVG text.
    // All if branches that could cause early exit in RenderBlocks layoutBlock() method are turned into assertions.
    ASSERT(!isInline());
    ASSERT(!simplifiedLayout());
    ASSERT(!scrollsOverflow());
    ASSERT(!hasControlClip());
    ASSERT(!hasColumns());
    ASSERT(!positionedObjects());
    ASSERT(!m_overflow);
    ASSERT(!isAnonymousBlock());

    if (!firstChild())
        setChildrenInline(true);

    // FIXME: We need to find a way to only layout the child boxes, if needed.
    FloatRect oldBoundaries = objectBoundingBox();
    ASSERT(childrenInline());
    forceLayoutInlineChildren();

    if (m_needsReordering)
        m_needsReordering = false;

    if (!updateCachedBoundariesInParents)
        updateCachedBoundariesInParents = oldBoundaries != objectBoundingBox();

    // Invalidate all resources of this client if our layout changed.
    if (everHadLayout() && selfNeedsLayout())
        SVGResourcesCache::clientLayoutChanged(this);

    // If our bounds changed, notify the parents.
    if (updateCachedBoundariesInParents)
        RenderSVGBlock::setNeedsBoundariesUpdate();

    repainter.repaintAfterLayout();
    setNeedsLayout(false);
}

RootInlineBox* RenderSVGText::createRootInlineBox() 
{
    RootInlineBox* box = new (renderArena()) SVGRootInlineBox(this);
    box->setHasVirtualLogicalHeight();
    return box;
}

bool RenderSVGText::nodeAtFloatPoint(const HitTestRequest& request, HitTestResult& result, const FloatPoint& pointInParent, HitTestAction hitTestAction)
{
    PointerEventsHitRules hitRules(PointerEventsHitRules::SVG_TEXT_HITTESTING, request, style()->pointerEvents());
    bool isVisible = (style()->visibility() == VISIBLE);
    if (isVisible || !hitRules.requireVisible) {
        if ((hitRules.canHitStroke && (style()->svgStyle()->hasStroke() || !hitRules.requireStroke))
            || (hitRules.canHitFill && (style()->svgStyle()->hasFill() || !hitRules.requireFill))) {
            FloatPoint localPoint = localToParentTransform().inverse().mapPoint(pointInParent);

            if (!SVGRenderSupport::pointInClippingArea(this, localPoint))
                return false;       

            return RenderBlock::nodeAtPoint(request, result, flooredIntPoint(localPoint), IntPoint(), hitTestAction);
        }
    }

    return false;
}

bool RenderSVGText::nodeAtPoint(const HitTestRequest&, HitTestResult&, const LayoutPoint&, const LayoutPoint&, HitTestAction)
{
    ASSERT_NOT_REACHED();
    return false;
}

VisiblePosition RenderSVGText::positionForPoint(const LayoutPoint& pointInContents)
{
    RootInlineBox* rootBox = firstRootBox();
    if (!rootBox)
        return createVisiblePosition(0, DOWNSTREAM);

    ASSERT(rootBox->isSVGRootInlineBox());
    ASSERT(!rootBox->nextRootBox());
    ASSERT(childrenInline());

    InlineBox* closestBox = static_cast<SVGRootInlineBox*>(rootBox)->closestLeafChildForPosition(pointInContents);
    if (!closestBox)
        return createVisiblePosition(0, DOWNSTREAM);

    return closestBox->renderer()->positionForPoint(LayoutPoint(pointInContents.x(), closestBox->y()));
}

void RenderSVGText::absoluteQuads(Vector<FloatQuad>& quads, bool* wasFixed) const
{
    quads.append(localToAbsoluteQuad(strokeBoundingBox(), false, wasFixed));
}

void RenderSVGText::paint(PaintInfo& paintInfo, const LayoutPoint&)
{
    if (paintInfo.context->paintingDisabled())
        return;

    if (paintInfo.phase != PaintPhaseForeground
     && paintInfo.phase != PaintPhaseSelfOutline
     && paintInfo.phase != PaintPhaseSelection)
         return;

    PaintInfo blockInfo(paintInfo);
    GraphicsContextStateSaver stateSaver(*blockInfo.context);
    blockInfo.applyTransform(localToParentTransform());
    RenderBlock::paint(blockInfo, LayoutPoint());
}

FloatRect RenderSVGText::strokeBoundingBox() const
{
    FloatRect strokeBoundaries = objectBoundingBox();
    const SVGRenderStyle* svgStyle = style()->svgStyle();
    if (!svgStyle->hasStroke())
        return strokeBoundaries;

    ASSERT(node());
    ASSERT(node()->isSVGElement());
    SVGLengthContext lengthContext(static_cast<SVGElement*>(node()));
    strokeBoundaries.inflate(svgStyle->strokeWidth().value(lengthContext));
    return strokeBoundaries;
}

FloatRect RenderSVGText::repaintRectInLocalCoordinates() const
{
    FloatRect repaintRect = strokeBoundingBox();
    SVGRenderSupport::intersectRepaintRectWithResources(this, repaintRect);

    if (const ShadowData* textShadow = style()->textShadow())
        textShadow->adjustRectForShadow(repaintRect);

    return repaintRect;
}

void RenderSVGText::addChild(RenderObject* child, RenderObject* beforeChild)
{
    RenderSVGBlock::addChild(child, beforeChild);
    subtreeChildAdded(child);
}

// Fix for <rdar://problem/8048875>. We should not render :first-line CSS Style
// in a SVG text element context.
RenderBlock* RenderSVGText::firstLineBlock() const
{
    return 0;
}

// Fix for <rdar://problem/8048875>. We should not render :first-letter CSS Style
// in a SVG text element context.
void RenderSVGText::updateFirstLetter()
{
}

void RenderSVGText::rebuildAllLayoutAttributes()
{
    m_layoutAttributes.clear();
    recursiveCollectLayoutAttributes(this, m_layoutAttributes);
    if (m_layoutAttributes.isEmpty())
        return;

    m_layoutAttributesBuilder.rebuildMetricsForWholeTree(this);
}

void RenderSVGText::rebuildLayoutAttributes()
{
    if (m_layoutAttributes.isEmpty()) {
        rebuildAllLayoutAttributes();
        return;
    }

    // Detect changes in layout attributes and only measure those text parts that have changed!
    Vector<SVGTextLayoutAttributes*> newLayoutAttributes;
    recursiveCollectLayoutAttributes(this, newLayoutAttributes);
    if (newLayoutAttributes.isEmpty()) {
        m_layoutAttributes.clear();
        return;
    }

    // Compare m_layoutAttributes with newLayoutAttributes to figure out which attributes got added.
    size_t size = newLayoutAttributes.size();
    for (size_t i = 0; i < size; ++i) {
        SVGTextLayoutAttributes* attributes = newLayoutAttributes[i];
        if (m_layoutAttributes.find(attributes) == notFound)
            m_layoutAttributesBuilder.rebuildMetricsForTextRenderer(attributes->context());
    }

    m_layoutAttributes = newLayoutAttributes;
}

}

#endif // ENABLE(SVG)