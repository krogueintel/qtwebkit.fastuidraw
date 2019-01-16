/*
 * Copyright (C) 2008 Holger Hans Peter Freyther
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "StillImageQt.h"

#include "GraphicsContext.h"
#include "ShadowBlur.h"
#include "FastUIDrawResources.h"
#include "FastUIDrawUtil.h"

#include <QPainter>
#include <iostream>

namespace WebCore {

StillImage::StillImage(const QPixmap& pixmap)
    : m_pixmap(new QPixmap(pixmap))
    , m_ownsPixmap(true)
{
    createFastUIDrawImage();
}

StillImage::StillImage(const QPixmap* pixmap)
    : m_pixmap(pixmap)
    , m_ownsPixmap(false)
{
}

StillImage::StillImage(QPixmap&& pixmap)
    : m_pixmap(new QPixmap())
    , m_ownsPixmap(true)
{
    const_cast<QPixmap*>(m_pixmap)->swap(pixmap);
    createFastUIDrawImage();
}

StillImage::~StillImage()
{
    if (m_ownsPixmap)
        delete m_pixmap;
}

bool StillImage::currentFrameKnownToBeOpaque()
{
    return !m_pixmap->hasAlpha();
}

FloatSize StillImage::size() const
{
    return FloatSize(m_pixmap->width(), m_pixmap->height());
}

PassNativeImagePtr StillImage::nativeImageForCurrentFrame()
{
    return const_cast<PassNativeImagePtr>(m_pixmap);
}

void StillImage::draw(GraphicsContext& ctxt, const FloatRect& dst,
    const FloatRect& src, CompositeOperator op, BlendMode blendMode, ImageOrientationDescription)
{
    if (ctxt.platformContext()->is_qt()) {
        if (m_pixmap->isNull())
            return;

        FloatRect normalizedSrc = src.normalized();
        FloatRect normalizedDst = dst.normalized();

        // source rect needs scaling from the device coords to image coords
        normalizedSrc.scale(m_pixmap->devicePixelRatio());

        CompositeOperator previousOperator = ctxt.compositeOperation();
        BlendMode previousBlendMode = ctxt.blendModeOperation();
        ctxt.setCompositeOperation(op, blendMode);

        if (ctxt.hasShadow()) {
            ShadowBlur shadow(ctxt.state());
            GraphicsContext* shadowContext = shadow.beginShadowLayer(ctxt, normalizedDst);
            if (shadowContext) {
                QPainter* shadowPainter = &shadowContext->platformContext()->qt();
                shadowPainter->drawPixmap(normalizedDst, *m_pixmap, normalizedSrc);
                shadow.endShadowLayer(ctxt);
            }
        }

        ctxt.platformContext()->qt().drawPixmap(normalizedDst, *m_pixmap, normalizedSrc);
        ctxt.setCompositeOperation(previousOperator, previousBlendMode);
    } else {
        fastuidraw::PainterBrush brush;
        FloatRect normalizedSrc = src.normalized();
        FloatRect normalizedDst = dst.normalized();

        readyFastUIDrawBrush(brush);
        brush
          .apply_translate(fastuidraw::vec2(normalizedSrc.x(), normalizedSrc.y()))
          .apply_shear(normalizedSrc.width() / normalizedDst.width(),
                       normalizedSrc.height() / normalizedDst.height())
          .apply_translate(fastuidraw::vec2(-normalizedDst.x(), -normalizedDst.y()));
        ctxt.drawImage(brush, normalizedDst, op, blendMode);
    }
}

void StillImage::createFastUIDrawImage(void)
{
    if (!m_fastuidraw_image && m_pixmap && !m_pixmap->isNull() && m_ownsPixmap) {
        m_fastuidraw_image = WebCore::FastUIDraw::create_fastuidraw_image(*m_pixmap);
    }
}

void StillImage::readyFastUIDrawBrush(fastuidraw::PainterBrush &brush)
{
  brush.reset();
  brush
    .image(m_fastuidraw_image)
    .apply_shear(m_pixmap->devicePixelRatio(), m_pixmap->devicePixelRatio());
}

}
