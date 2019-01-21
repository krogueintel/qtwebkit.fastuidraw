/*
 * Copyright (C) 2006 Dirk Mueller <mueller@kde.org>
 * Copyright (C) 2006 Zack Rusin <zack@kde.org>
 * Copyright (C) 2006 George Staikos <staikos@kde.org>
 * Copyright (C) 2006 Simon Hausmann <hausmann@kde.org>
 * Copyright (C) 2006 Allan Sandfeld Jensen <sandfeld@kde.org>
 * Copyright (C) 2006 Nikolas Zimmermann <zimmermann@kde.org>
 * Copyright (C) 2003, 2004, 2005, 2006, 2007, 2008 Apple Inc. All rights reserved.
 * Copyright (C) 2008 Nokia Corporation and/or its subsidiary(-ies).
 * Copyright (C) 2008 Dirk Schulze <vbs85@gmx.de>
 * Copyright (C) 2010, 2011 Sencha, Inc.
 * Copyright (C) 2011 Andreas Kling <kling@webkit.org>
 * Copyright (C) 2015 The Qt Company Ltd.
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

#define TransparencyLayerColor(X) fastuidraw::vec4(1.0f, 1.0f, 1.0f, X)

#include "config.h"
#include "GraphicsContext.h"

#if OS(WINDOWS)
#include <windows.h>
#endif

#include "AffineTransform.h"
#include "Color.h"
#include "DisplayListRecorder.h"
#include "FloatConversion.h"
#include "ImageBuffer.h"
#include "ImageBufferDataQt.h"
#include "Path.h"
#include "Pattern.h"
#include "ShadowBlur.h"
#include "GraphicsContext.h"
#include "TransformationMatrix.h"
#include "TransparencyLayer.h"
#include "URL.h"
#include "FastUIDrawResources.h"
#include "FastUIDrawPainter.h"

#include <QBrush>
#include <QGradient>
#include <QPaintDevice>
#include <QPaintEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QPixmap>
#include <QPolygonF>
#include <QStack>
#include <QUrl>
#include <QVector>
#include <private/qpdf_p.h>
#include <wtf/MathExtras.h>

#include <fastuidraw/gl_backend/image_gl.hpp>
#include <fastuidraw/gl_backend/painter_backend_gl.hpp>

#include <iostream>

#if OS(WINDOWS)
QT_BEGIN_NAMESPACE
Q_GUI_EXPORT QPixmap qt_pixmapFromWinHBITMAP(HBITMAP, int hbitmapFormat = 0);
QT_END_NAMESPACE

enum HBitmapFormat {
    HBitmapNoAlpha,
    HBitmapPremultipliedAlpha,
    HBitmapAlpha
};
#endif

namespace std {

template<typename T, size_t N>
static inline ostream& operator<<(ostream &str, const fastuidraw::vecN<T, N> &obj)
{
  str << "(";
  for (size_t i = 0; i != N; ++i)
    {
      if (i != 0)
        {
          str << ", ";
        }
      str << obj[i];
    }
  str << ")";
  return str;
}

static inline ostream& operator<<(ostream &str, WebCore::Color c)
{
  str << fastuidraw::ivec4(c.red(), c.green(), c.blue(), c.alpha());
  return str;
}

static inline ostream& operator<<(ostream &str, const WebCore::FloatPoint &pt)
{
  str << "(" << pt.x() << ", " << pt.y() << ")";
  return str;
}

static inline ostream& operator<<(ostream &str, const WebCore::FloatSize &pt)
{
  str << "(" << pt.width() << ", " << pt.height() << ")";
  return str;
}

static inline ostream& operator<<(ostream &str, const WebCore::FloatRect &rect)
{
  if (rect.width() < 0 || rect.height() < 0)
    {
      str << "Unnormalized";
    }
  str << "Rect(@" << rect.location() << ", Sz = " << rect.size() << ")";
  return str;
}

static inline ostream& operator<<(ostream &str, const WebCore::FloatRoundedRect &rect)
{
  str << "RoundedRect(@" << rect.rect().location() << ", Sz = " << rect.rect().size()
      << ":" << rect.radii().topLeft() << ":" << rect.radii().topRight()
      << ":" << rect.radii().bottomLeft() << ":" << rect.radii().bottomRight() << ")";
  return str;
}

static inline ostream& operator<<(ostream &str, const fastuidraw::Rect &rect)
{
  str << "FUIDraw";
  if (rect.width() < 0 || rect.height() < 0)
    {
      str << "Unnormalized";
    }
  str << "Rect(@" << rect.m_min_point << ", Sz = " << rect.size() << ")";
  return str;
}

}

static bool EnableGraphicsContextTransparencyLayer = true;

namespace {
  class SimpleGlyphRendererChooser:public fastuidraw::Painter::GlyphRendererChooser {
  public:
      virtual
      fastuidraw::GlyphRenderer
      choose_glyph_render(float logical_pixel_size,
                          const fastuidraw::float3x3& /*transformation */,
                          float max_singular_value,
                          float /*min_singular_value*/) const
      {
          float sz;
          sz = logical_pixel_size * max_singular_value;
          return (sz < 32.0f) ?
              fastuidraw::GlyphRenderer(static_cast<int>(sz)) :
              fastuidraw::GlyphRenderer(fastuidraw::restricted_rays_glyph);
      }

      virtual
      fastuidraw::GlyphRenderer
      choose_glyph_render(float /*logical_pixel_size */,
                          const fastuidraw::float3x3& /*transformation*/) const
      {
          return fastuidraw::GlyphRenderer(fastuidraw::restricted_rays_glyph);
      }
  };
}

namespace WebCore {

using namespace FastUIDraw; //I should use scoped calls, but this code is just POC.
  
static inline QPainter::CompositionMode toQtCompositionMode(CompositeOperator op)
{
    switch (op) {
    case CompositeClear:
        return QPainter::CompositionMode_Clear;
    case CompositeCopy:
        return QPainter::CompositionMode_Source;
    case CompositeSourceOver:
        return QPainter::CompositionMode_SourceOver;
    case CompositeSourceIn:
        return QPainter::CompositionMode_SourceIn;
    case CompositeSourceOut:
        return QPainter::CompositionMode_SourceOut;
    case CompositeSourceAtop:
        return QPainter::CompositionMode_SourceAtop;
    case CompositeDestinationOver:
        return QPainter::CompositionMode_DestinationOver;
    case CompositeDestinationIn:
        return QPainter::CompositionMode_DestinationIn;
    case CompositeDestinationOut:
        return QPainter::CompositionMode_DestinationOut;
    case CompositeDestinationAtop:
        return QPainter::CompositionMode_DestinationAtop;
    case CompositeXOR:
        return QPainter::CompositionMode_Xor;
    case CompositePlusDarker:
        // there is no exact match, but this is the closest
        return QPainter::CompositionMode_Darken;
    case CompositePlusLighter:
        return QPainter::CompositionMode_Plus;
    case CompositeDifference:
        return QPainter::CompositionMode_Difference;
    default:
        ASSERT_NOT_REACHED();
    }

    return QPainter::CompositionMode_SourceOver;
}

static fastuidraw::Painter::composite_mode_t toFastUIDrawCompositeMode(CompositeOperator op)
{
    switch (op) {
    case CompositeClear:
        return fastuidraw::Painter::composite_porter_duff_clear;
    case CompositeCopy:
        return fastuidraw::Painter::composite_porter_duff_src;
    case CompositeSourceOver:
        return fastuidraw::Painter::composite_porter_duff_src_over;
    case CompositeSourceIn:
        return fastuidraw::Painter::composite_porter_duff_src_in;
    case CompositeSourceOut:
        return fastuidraw::Painter::composite_porter_duff_src_out;
    case CompositeSourceAtop:
        return fastuidraw::Painter::composite_porter_duff_src_atop;
    case CompositeDestinationOver:
        return fastuidraw::Painter::composite_porter_duff_dst_over;
    case CompositeDestinationIn:
        return fastuidraw::Painter::composite_porter_duff_dst_in;
    case CompositeDestinationOut:
        return fastuidraw::Painter::composite_porter_duff_dst_out;
    case CompositeDestinationAtop:
        return fastuidraw::Painter::composite_porter_duff_dst_atop;
    case CompositeXOR:
        return fastuidraw::Painter::composite_porter_duff_xor;

        /* TODO: FastUIDraw needs these additional composite modes */
    case CompositePlusDarker:
    case CompositePlusLighter:
    case CompositeDifference:
        warningFastUIDraw("Unsupported composite mode");
        break;
    default:
        ASSERT_NOT_REACHED();
    }

    return fastuidraw::Painter::composite_porter_duff_src_over;
}

static inline QPainter::CompositionMode toQtCompositionMode(BlendMode op)
{
    switch (op) {
    case BlendModeNormal:
        return QPainter::CompositionMode_SourceOver;
    case BlendModeMultiply:
        return QPainter::CompositionMode_Multiply;
    case BlendModeScreen:
        return QPainter::CompositionMode_Screen;
    case BlendModeOverlay:
        return QPainter::CompositionMode_Overlay;
    case BlendModeDarken:
        return QPainter::CompositionMode_Darken;
    case BlendModeLighten:
        return QPainter::CompositionMode_Lighten;
    case BlendModeColorDodge:
        return QPainter::CompositionMode_ColorDodge;
    case BlendModeColorBurn:
        return QPainter::CompositionMode_ColorBurn;
    case BlendModeHardLight:
        return QPainter::CompositionMode_HardLight;
    case BlendModeSoftLight:
        return QPainter::CompositionMode_SoftLight;
    case BlendModeDifference:
        return QPainter::CompositionMode_Difference;
    case BlendModeExclusion:
        return QPainter::CompositionMode_Exclusion;
    case BlendModeHue:
    case BlendModeSaturation:
    case BlendModeColor:
    case BlendModeLuminosity:
        // Not supported.
        break;
    default:
        ASSERT_NOT_REACHED();
    }

    return QPainter::CompositionMode_SourceOver;
}

static fastuidraw::Painter::blend_w3c_mode_t toFastUIDrawBlendMode(BlendMode op)
{
    switch (op) {
    case BlendModeNormal:
        return fastuidraw::Painter::blend_w3c_normal;
    case BlendModeMultiply:
        return fastuidraw::Painter::blend_w3c_multiply;
    case BlendModeScreen:
        return fastuidraw::Painter::blend_w3c_screen;
    case BlendModeOverlay:
        return fastuidraw::Painter::blend_w3c_overlay;
    case BlendModeDarken:
        return fastuidraw::Painter::blend_w3c_darken;
    case BlendModeLighten:
        return fastuidraw::Painter::blend_w3c_lighten;
    case BlendModeColorDodge:
        return fastuidraw::Painter::blend_w3c_color_dodge;
    case BlendModeColorBurn:
        return fastuidraw::Painter::blend_w3c_color_burn;
    case BlendModeHardLight:
        return fastuidraw::Painter::blend_w3c_hardlight;
    case BlendModeSoftLight:
        return fastuidraw::Painter::blend_w3c_softlight;
    case BlendModeDifference:
        return fastuidraw::Painter::blend_w3c_difference;
    case BlendModeExclusion:
        return fastuidraw::Painter::blend_w3c_exclusion;
    case BlendModeHue:
        return fastuidraw::Painter::blend_w3c_hue;
    case BlendModeSaturation:
        return fastuidraw::Painter::blend_w3c_saturation;
    case BlendModeColor:
        return fastuidraw::Painter::blend_w3c_color;
    case BlendModeLuminosity:
        return fastuidraw::Painter::blend_w3c_luminosity;
    default:
        ASSERT_NOT_REACHED();
    }

    warningFastUIDraw("Unsupported blend mode");
    return fastuidraw::Painter::blend_w3c_normal;
}

static inline Qt::PenCapStyle toQtLineCap(LineCap lc)
{
    switch (lc) {
    case ButtCap:
        return Qt::FlatCap;
    case RoundCap:
        return Qt::RoundCap;
    case SquareCap:
        return Qt::SquareCap;
    default:
        ASSERT_NOT_REACHED();
    }

    return Qt::FlatCap;
}

static inline enum fastuidraw::Painter::cap_style toFastUIDrawCapStyle(LineCap lc)
{
    switch (lc) {
    case ButtCap:
        return fastuidraw::Painter::flat_caps;
    case RoundCap:
        return fastuidraw::Painter::rounded_caps;
    case SquareCap:
        return fastuidraw::Painter::square_caps;
    default:
        ASSERT_NOT_REACHED();
    }

    return fastuidraw::Painter::flat_caps;
}

static inline Qt::PenJoinStyle toQtLineJoin(LineJoin lj)
{
    switch (lj) {
    case MiterJoin:
        return Qt::SvgMiterJoin;
    case RoundJoin:
        return Qt::RoundJoin;
    case BevelJoin:
        return Qt::BevelJoin;
    default:
        ASSERT_NOT_REACHED();
    }

    return Qt::SvgMiterJoin;
}

static inline enum fastuidraw::Painter::join_style toFastUIDrawLineJoin(LineJoin lj)
{
    switch (lj) {
    case MiterJoin:
        return fastuidraw::Painter::miter_clip_joins;
    case RoundJoin:
        return fastuidraw::Painter::rounded_joins;
    case BevelJoin:
        return fastuidraw::Painter::bevel_joins;
    default:
        ASSERT_NOT_REACHED();
    }

    return fastuidraw::Painter::miter_clip_joins;
}

static Qt::PenStyle toQPenStyle(StrokeStyle style)
{
    switch (style) {
    case NoStroke:
        return Qt::NoPen;
        break;
    case SolidStroke:
    case DoubleStroke:
    case WavyStroke:
        return Qt::SolidLine;
        break;
    case DottedStroke:
        return Qt::DotLine;
        break;
    case DashedStroke:
        return Qt::DashLine;
        break;
    default:
        ASSERT_NOT_REACHED();
    }
    return Qt::NoPen;
}

static inline Qt::FillRule toQtFillRule(WindRule rule)
{
    switch (rule) {
    case RULE_EVENODD:
        return Qt::OddEvenFill;
    case RULE_NONZERO:
        return Qt::WindingFill;
    default:
        ASSERT_NOT_REACHED();
    }
    return Qt::OddEvenFill;
}

static inline enum fastuidraw::Painter::fill_rule_t toFastUIDrawFillRule(WindRule rule)
{
    switch (rule) {
    case RULE_EVENODD:
        return fastuidraw::Painter::odd_even_fill_rule;
    case RULE_NONZERO:
        return fastuidraw::Painter::nonzero_fill_rule;
    default:
        ASSERT_NOT_REACHED();
    }
    return fastuidraw::Painter::odd_even_fill_rule;
}

static inline fastuidraw::vec4 FastUIDrawColorValue(Color color, float alpha)
{
  fastuidraw::vec4 return_value(color.red(), color.green(), color.blue(), color.alpha());

  return_value /= 255.0f;
  return_value.w() *= alpha;

  return return_value;
}

static inline void rectFromFloatRect(const FloatRect &inRect,
                                     fastuidraw::Rect *out_rect)
{
  out_rect->m_min_point.x() = fastuidraw::t_min(inRect.x(), inRect.maxX());
  out_rect->m_min_point.y() = fastuidraw::t_min(inRect.y(), inRect.maxY());

  out_rect->m_max_point.x() = fastuidraw::t_max(inRect.x(), inRect.maxX());
  out_rect->m_max_point.y() = fastuidraw::t_max(inRect.y(), inRect.maxY());
}

static inline fastuidraw::Rect rectFromFloatRect(const FloatRect &inRect)
{
  fastuidraw::Rect R;
  rectFromFloatRect(inRect, &R);
  return R;
}

static inline void createFastUIDrawRoundedRect(const FloatRoundedRect &inRect,
                                               fastuidraw::RoundedRect *out_rect)
{
  rectFromFloatRect(inRect.rect(), out_rect);
  out_rect->m_corner_radii[fastuidraw::Rect::minx_miny_corner] = vec2FromFloatSize(inRect.radii().topLeft());
  out_rect->m_corner_radii[fastuidraw::Rect::maxx_miny_corner] = vec2FromFloatSize(inRect.radii().topRight());
  out_rect->m_corner_radii[fastuidraw::Rect::minx_maxy_corner] = vec2FromFloatSize(inRect.radii().bottomLeft());
  out_rect->m_corner_radii[fastuidraw::Rect::maxx_maxy_corner] = vec2FromFloatSize(inRect.radii().bottomRight());
}

template<typename S, typename T = S>
class MutablePackedValue
{
public:
    MutablePackedValue(const fastuidraw::reference_counted_ptr<fastuidraw::Painter> &p)
      : m_pool(p ? &p->packed_value_pool() : nullptr)
    {}
  
    const S&
    constant_value(void) const { return m_value; }

    S&
    change_value(void) { m_packed_value.reset(); return m_value; }

    const fastuidraw::PainterPackedValue<T>&
    packed_value(void)
    {
        if (!m_packed_value) {
            m_packed_value = m_pool->create_packed_value(m_value);
        }
        return m_packed_value;
    }

private:
    fastuidraw::PainterPackedValuePool *m_pool;
    S m_value;
    fastuidraw::PainterPackedValue<T> m_packed_value;
};

void setPatternGradientOfFastUIDrawBrush(const RefPtr<Pattern> &pattern, const RefPtr<Gradient> &gr,
                                         fastuidraw::PainterBrush &brush,
                                         float alpha)
{
    brush.reset();
    if (gr) {
        gr->readyFastUIDrawBrush(brush);
    } else if (pattern) {
        if (pattern->tileImage()) {
            FloatSize srcSize(pattern->tileImage()->width(),
                              pattern->tileImage()->height());
            FloatRect srcRect(FloatPoint(0.0f, 0.0f), srcSize);
            FloatPoint phase(0.0f, 0.0f);
            FloatSize spacing(0.0f, 0.0f);
        
            pattern->tileImage()->readyFastUIDrawBrush(brush);
            compose_with_pattern(brush, srcRect,
                                 pattern->getPatternSpaceTransform(),
                                 phase, spacing);
        } else {
            warningFastUIDraw("Pattern with tileImage() = nullptr");
        }
    }

    brush.color(1.0f, 1.0f, 1.0f, alpha);
}

class FastUIDrawStateElement
{
public:
  FastUIDrawStateElement(const fastuidraw::reference_counted_ptr<fastuidraw::Painter> &p,
                         PlatformGraphicsContext::FastUIDrawOption options)
    : m_fill_aa(options.m_allow_fill_aa ? fastuidraw::Painter::shader_anti_alias_auto : fastuidraw::Painter::shader_anti_alias_none)
    , m_stroke_aa(options.m_allow_stroke_aa ? fastuidraw::Painter::shader_anti_alias_auto : fastuidraw::Painter::shader_anti_alias_none)
    , m_stroke_brush(p)
    , m_fill_brush(p)
    , m_stroke_params(p)
  {
    
  }
  
  enum fastuidraw::Painter::shader_anti_alias_t m_fill_aa, m_stroke_aa;
  fastuidraw::StrokingStyle m_stroke_style;
  MutablePackedValue<fastuidraw::PainterBrush> m_stroke_brush, m_fill_brush;
  MutablePackedValue<fastuidraw::PainterStrokeParams, fastuidraw::PainterItemShaderData> m_stroke_params;
};

class FastUIDrawTransparencyLayer
{
public:
  FastUIDrawTransparencyLayer(const fastuidraw::reference_counted_ptr<fastuidraw::PainterBackend::Surface> &root_surface,
                              const fastuidraw::reference_counted_ptr<fastuidraw::Painter> &parent,
                              float opacity)
  {
    /* Basic idea: create a gl::TextureImage which will back m_surface with a size
     * that just contains the region bounded by the clipping region.
     */
    fastuidraw::vec2 fm, fM;
    fastuidraw::ivec2 m, M, sz;
    fastuidraw::PainterBackend::Surface::Viewport vwp;
    bool b;

    vwp = root_surface->viewport();    
    b = parent->clip_region_bounds(&fm, &fM);
    /* fastuidraw::Painter delivers the coordinates
     * in normalize device coordinates, transform
     * it into pixel coordinates.
     */
    m = to_pixel_coordinates(fm, vwp.m_dimensions);
    M = to_pixel_coordinates(fM, vwp.m_dimensions);
    
    sz = M - m;
    sz.x() = fastuidraw::t_max(1, sz.x());
    sz.y() = fastuidraw::t_max(1, sz.y());

    /* make the viewport so that the parent transformation maps correctly */
    vwp.m_origin -= m;

    m_blit_rect
      .min_point(fastuidraw::vec2(m))
      .size(fastuidraw::vec2(sz));

    m_image = fastuidraw::gl::ImageAtlasGL::TextureImage::create(FastUIDraw::imageAtlas(), sz.x(), sz.y(), GL_LINEAR);
    m_painter = FASTUIDRAWnew FastUIDraw::PainterHolder();
    m_surface = FASTUIDRAWnew fastuidraw::gl::PainterBackendGL::SurfaceGL(sz, m_image->texture());
    m_surface->clear_color(fastuidraw::vec4(0.0f, 0.0f, 0.0f, 0.0f));
    m_surface->viewport(vwp);
    m_opacity = opacity;

    fastuidraw::float_orthogonal_projection_params pp(0, vwp.m_dimensions.x(), 0, vwp.m_dimensions.y());

    /* Our conversion to pixel coordinates is in coordinates where (0, 0)
     * is the -BOTTOM- left corner (instead of the top left). Thus we set the
     * transformation with that convention when we initialize the clipping
     * region and then we set the transformation as the parent fastuidraw::painter
     * had it.
     */
    m_painter->painter()->begin(m_surface, fastuidraw::Painter::y_increases_downwards, true);
    m_painter->painter()->transformation(pp);
    m_painter->painter()->clip_in_rect(m_blit_rect);
    m_painter->painter()->transformation(parent->transformation());
  }

  static
  fastuidraw::ivec2
  to_pixel_coordinates(fastuidraw::vec2 normalized_coord,
                       fastuidraw::ivec2 viewport_sz)
  {
    normalized_coord += fastuidraw::vec2(1.0f, 1.0f);
    normalized_coord *= 0.5f;
    normalized_coord *= fastuidraw::vec2(viewport_sz);
    return fastuidraw::ivec2(normalized_coord);
  }

  fastuidraw::reference_counted_ptr<fastuidraw::gl::ImageAtlasGL::TextureImage> m_image;
  fastuidraw::reference_counted_ptr<FastUIDraw::PainterHolder> m_painter;
  fastuidraw::reference_counted_ptr<fastuidraw::gl::PainterBackendGL::SurfaceGL> m_surface;
  fastuidraw::Rect m_blit_rect;
  float m_opacity;
};

static inline enum fastuidraw::PainterBrush::image_filter computeFastUIImageFilter(InterpolationQuality quality,
                                                                                   fastuidraw::reference_counted_ptr<const fastuidraw::Image> image)
{
    enum fastuidraw::PainterBrush::image_filter filter;

    switch (quality) {
    case InterpolationNone:
    case InterpolationLow:
        filter = fastuidraw::PainterBrush::image_filter_nearest;
        break;
    case InterpolationDefault:
    case InterpolationMedium:
        filter = fastuidraw::PainterBrush::image_filter_linear;
        break;
    case InterpolationHigh:
        filter = fastuidraw::PainterBrush::image_filter_cubic;
        break;
    }
    
    if (image) {
        filter = fastuidraw::PainterBrush::filter_for_image(image, filter);
    }
    return filter;
}

class GraphicsContextPlatformPrivate {
    WTF_MAKE_NONCOPYABLE(GraphicsContextPlatformPrivate); WTF_MAKE_FAST_ALLOCATED;
public:
    GraphicsContextPlatformPrivate(PlatformGraphicsContext*, const QColor& initialSolidColor);
    ~GraphicsContextPlatformPrivate();

    inline PlatformGraphicsContext *platform_gc() const
    {
        if (platform->is_qt() && !layers.isEmpty()) {
            return &layers.top()->platform_gc;
        }
        else {
            return platform;
        }
    }

    bool is_qt(void) const { return platform->is_qt(); }
    bool is_fastuidraw(void) const { return platform->is_fastuidraw(); }

  /////////////////////////////////////
  // Stuff for Qt
    bool antiAliasingForRectsAndLines;

    QStack<TransparencyLayer*> layers;
    // Counting real layers. Required by isInTransparencyLayer() calls
    // For example, layers with valid alphaMask are not real layers
    int layerCount;

    // reuse this brush for solid color (to prevent expensive QBrush construction)
    QBrush solidColor;

    inline QPainter* p(void) const
    {
        FASTUIDRAWassert(is_qt());
        if (layers.isEmpty())
            return &platform->qt();
        return &layers.top()->painter;
    }

    QRectF clipBoundingRect() const
    {
        return p()->clipBoundingRect();
    }

    void takeOwnershipOfPlatformContext() { platformContextIsOwned = true; }

    InterpolationQuality imageInterpolationQuality;
    bool initialSmoothPixmapTransformHint;
    unsigned int qtStateStackDepth;

  //////////////////////////////////////////////
  // Stuff for FastUIDraw
    fastuidraw::Path m_fastuidraw_square_path, m_fastuidraw_circle_path;
    fastuidraw::PainterPackedValue<fastuidraw::PainterBrush> m_packed_black_brush;
    std::vector<FastUIDrawStateElement> m_fastuidraw_state_stack;
    FastUIDrawStateElement &fastuidraw_state(void) { return m_fastuidraw_state_stack.back(); }
    fastuidraw::reference_counted_ptr<fastuidraw::PainterBackend::Surface> m_root_surface;
    std::vector<FastUIDrawTransparencyLayer> m_fastuidraw_layers;

    std::string printPrefix(void)
    {
      int cnt;

      cnt = (is_qt()) ? layers.size() : 0;
      return std::string(2 * cnt, ' ');
    }

    PlatformGraphicsContext::FastUIDrawOption fastuidraw_options(void) const
    {
        return platform->fastuidraw_options();
    }

    inline fastuidraw::float3x3 computeFastUIDrawCTM(void)
    {
        const fastuidraw::vec2 &dims(fastuidraw()->surface()->viewport().m_dimensions);
        fastuidraw::float_orthogonal_projection_params ortho(0.0f, dims.x(), dims.y(), 0.0f);
        fastuidraw::float3x3 inverse_ortho;

        /* fastuidraw::Painter::transformation() includes the transformation
         * to normalized device coordiantes, but WebKit wants the transformation
         * to pixel coordinates, so undo the orthogonal projection.
         */
        inverse_ortho.inverse_orthogonal_projection_matrix(ortho);
        return inverse_ortho * fastuidraw()->transformation();
    }

    template<typename T> inline void setFastUIDrawCTM(const T &value)
    {
        fastuidraw::float3x3 matrix;
        const fastuidraw::vec2 &dims(fastuidraw()->surface()->viewport().m_dimensions);
        fastuidraw::float_orthogonal_projection_params ortho(0.0f, dims.x(), dims.y(), 0.0f);

        computeToFastUIDrawMatrixT(value, &matrix);
        /* fastuidraw::Painter::transformation() includes the transformation
         * to normalized device coordiantes, but WebKit gives the transformation
         * to pixel coordinates, so we need to provide the othogonal projection
         * as well.
         */
        fastuidraw()->transformation(fastuidraw::float3x3(ortho) * matrix);
    }
  
    inline const fastuidraw::reference_counted_ptr<fastuidraw::Painter>&
    fastuidraw(void) const
    {
        return m_fastuidraw_layers.empty() ?
            platform->fastuidraw() :
            m_fastuidraw_layers.back().m_painter->painter();
    }
private:
    PlatformGraphicsContext *platform;
    bool platformContextIsOwned;
};

/////////////////////////////////////////////
// GraphicsContextPlatformPrivate methods
GraphicsContextPlatformPrivate::GraphicsContextPlatformPrivate(PlatformGraphicsContext* p, const QColor& initialSolidColor)
    : antiAliasingForRectsAndLines(false)
    , layerCount(0)
    , solidColor(initialSolidColor)
    , imageInterpolationQuality(InterpolationDefault)
    , initialSmoothPixmapTransformHint(false)
    , qtStateStackDepth(0)
    , platform(p)
    , platformContextIsOwned(false)
{
    if (!platform)
        return;

    if (platform->is_qt()) {      
        // Use the default the QPainter was constructed with.
        antiAliasingForRectsAndLines = platform->qt().testRenderHint(QPainter::Antialiasing);

        // Used for default image interpolation quality.
        initialSmoothPixmapTransformHint = platform->qt().testRenderHint(QPainter::SmoothPixmapTransform);

        platform->qt().setRenderHint(QPainter::Antialiasing, true);

        if (platform->qt().paintEngine()
            && platform->qt().paintEngine()->type() != QPaintEngine::OpenGL2)
          {
            //std::cout << "NoGL@" << &platform->qt() << "\n";
          }
    } else {
        fastuidraw::PainterPackedValuePool &pool(platform->fastuidraw()->packed_value_pool());

        m_root_surface = platform->fastuidraw()->surface();
        m_fastuidraw_state_stack.push_back(FastUIDrawStateElement(platform->fastuidraw(), platform->fastuidraw_options()));
        m_packed_black_brush = pool.create_packed_value(fastuidraw::PainterBrush()
                                                        .color(0.0f, 0.0f, 0.0f, 0.0f));
        m_fastuidraw_square_path << fastuidraw::vec2(0.0f, 0.0f)
                                 << fastuidraw::vec2(0.0f, 1.0f)
                                 << fastuidraw::vec2(1.0f, 1.0f)
                                 << fastuidraw::vec2(1.0f, 0.0f)
                                 << fastuidraw::Path::contour_close();

        m_fastuidraw_circle_path << fastuidraw::vec2(1.0f, 0.0f)
                                 << fastuidraw::Path::arc(M_PI * 0.5f, fastuidraw::vec2( 0.0f, +1.0f))
                                 << fastuidraw::Path::arc(M_PI * 0.5f, fastuidraw::vec2(-1.0f,  0.0f))
                                 << fastuidraw::Path::arc(M_PI * 0.5f, fastuidraw::vec2( 0.0f, -1.0f))
                                 << fastuidraw::Path::contour_close_arc(M_PI * 0.5f);
    }
}

GraphicsContextPlatformPrivate::~GraphicsContextPlatformPrivate()
{
    if (!platformContextIsOwned)
        return;

    if (is_qt()) {
        QPainter *painter = &platform->qt();
        QPaintDevice* device = painter->device();
        painter->end();
        delete painter;
        delete device;
    }
    delete platform;
}

///////////////////////////////////
// GraphicsContext methods
void GraphicsContext::platformInit(PlatformGraphicsContext *painter)
{
    FUID_TRACE;
    if (!painter)
        return;

    m_data = new GraphicsContextPlatformPrivate(painter, fillColor());

    if (painter->is_qt()) {
        // solidColor is initialized with the fillColor().
        painter->qt().setBrush(m_data->solidColor);

        QPen pen(painter->qt().pen());
        pen.setColor(strokeColor());
        pen.setJoinStyle(toQtLineJoin(MiterJoin));
        pen.setCapStyle(Qt::FlatCap);
        painter->qt().setPen(pen);
    } else {
        m_data->fastuidraw_state().m_stroke_style
          .cap_style(toFastUIDrawCapStyle(ButtCap))
          .join_style(toFastUIDrawLineJoin(MiterJoin));

        m_data->fastuidraw_state().m_fill_brush.change_value().color(FastUIDrawColorValue(fillColor(), alpha()));
        m_data->fastuidraw_state().m_stroke_brush.change_value().color(FastUIDrawColorValue(strokeColor(), alpha()));
    }
}

void GraphicsContext::platformDestroy()
{
    FUID_TRACE;
    if (m_data && m_data->is_qt()) {
        while (!m_data->layers.isEmpty())
            endTransparencyLayer();
    }

    delete m_data;
}

PlatformGraphicsContext* GraphicsContext::platformContext() const
{
    FUID_TRACE;
    return m_data->platform_gc();
}

AffineTransform GraphicsContext::getCTM(IncludeDeviceScale includeScale) const
{
    FUID_TRACE;
    if (paintingDisabled())
        return AffineTransform();

    if (m_data->is_qt()) {
        const QTransform& matrix = (includeScale == DefinitelyIncludeDeviceScale)
          ? platformContext()->qt().combinedTransform()
          : platformContext()->qt().worldTransform();
        return AffineTransform(matrix.m11(), matrix.m12(), matrix.m21(),
                               matrix.m22(), matrix.dx(), matrix.dy());
    } else {
        AffineTransform return_value;

        computeFromFastUIDrawMatrixT(m_data->computeFastUIDrawCTM(), &return_value);
        return return_value;
    }    
}

void GraphicsContext::savePlatformState()
{
    FUID_TRACE;
    if (m_data->is_qt()) {
        if (!m_data->layers.isEmpty() && !m_data->layers.top()->alphaMask.isNull())
            ++m_data->layers.top()->saveCounter;
        m_data->p()->save();
        ++m_data->qtStateStackDepth;
    } else {
        m_data->m_fastuidraw_state_stack.push_back(m_data->fastuidraw_state());
        m_data->fastuidraw()->save();
    }
}

void GraphicsContext::restorePlatformState()
{
    FUID_TRACE;
    if (m_data->is_qt()) {
        if (!m_data->layers.isEmpty() && !m_data->layers.top()->alphaMask.isNull())
            if (!--m_data->layers.top()->saveCounter)
                popTransparencyLayerInternal();

        m_data->p()->restore();
        --m_data->qtStateStackDepth;
    } else {
        m_data->fastuidraw()->restore();
        m_data->m_fastuidraw_state_stack.pop_back();
    }
}

// Draws a filled rectangle with a stroked border.
// This is only used to draw borders (real fill is done via fillRect), and
// thus it must not cast any shadow.
void GraphicsContext::drawRect(const FloatRect& rect, float borderThickness)
{
    FUID_TRACE;
    if (paintingDisabled())
        return;

    ASSERT(!rect.isEmpty());

    if (m_data->is_qt()) {
        QPainter* p = m_data->p();
        const bool antiAlias = p->testRenderHint(QPainter::Antialiasing);
        p->setRenderHint(QPainter::Antialiasing, m_data->antiAliasingForRectsAndLines);

        // strokeThickness() is disregarded
        QPen oldPen(p->pen());
        QPen newPen(oldPen);
        newPen.setWidthF(borderThickness);
        p->setPen(newPen);

        p->drawRect(rect);

        p->setPen(oldPen);
        p->setRenderHint(QPainter::Antialiasing, antiAlias);
    } else {
        fastuidraw::Rect fRect(rectFromFloatRect(rect));

        m_data->fastuidraw()->fill_rect(fastuidraw::PainterData(m_data->fastuidraw_state().m_fill_brush.packed_value()),
                                        fRect, m_data->fastuidraw_state().m_fill_aa);

        /**
        fastuidraw::Path P;
        fastuidraw::PainterStrokeParams stroke_params;

        P << fRect.point(fastuidraw::Rect::minx_miny_corner)
          << fRect.point(fastuidraw::Rect::minx_maxy_corner)
          << fRect.point(fastuidraw::Rect::maxx_maxy_corner)
          << fRect.point(fastuidraw::Rect::maxx_miny_corner)
          << fastuidraw::Path::contour_close();

        // not clear, but using the stroke brush give incorrect render on github
        stroke_params
          .width(borderThickness)
          .stroking_units(fastuidraw::PainterStrokeParams::path_stroking_units);

        m_data->fastuidraw()->stroke_path(fastuidraw::PainterData(m_data->fastuidraw_state().m_fill_brush.packed_value(),
                                                                  &stroke_params),
                                          P,
                                          m_data->fastuidraw_state().m_stroke_style,
                                          m_data->fastuidraw_state().m_stroke_aa);
        **/
    }
}

// This is only used to draw borders.
// Must not cast any shadow.
void GraphicsContext::drawLine(const FloatPoint& point1, const FloatPoint& point2)
{
    FUID_TRACE;
    if (paintingDisabled())
        return;

    if (strokeStyle() == NoStroke)
        return;

    if (isRecording()) {
        m_displayListRecorder->drawLine(point1, point2);
        return;
    }

    const Color& strokeColor = this->strokeColor();
    float thickness = strokeThickness();
    bool isVerticalLine = (point1.x() + thickness == point2.x());
    float strokeWidth = isVerticalLine ? point2.y() - point1.y() : point2.x() - point1.x();
    if (!thickness || !strokeWidth)
        return;

    StrokeStyle strokeStyle = this->strokeStyle();
    bool drawsDashedLine = strokeStyle == DottedStroke || strokeStyle == DashedStroke;

    if (m_data->is_qt()) {
        QPainter* p = &platformContext()->qt();
        const bool savedAntiAlias = p->testRenderHint(QPainter::Antialiasing);
        p->setRenderHint(QPainter::Antialiasing, m_data->antiAliasingForRectsAndLines);

        float cornerWidth = 0;

        if (drawsDashedLine) {
            p->save();
            // Figure out end points to ensure we always paint corners.
            cornerWidth = strokeStyle == DottedStroke ? thickness : std::min(2 * thickness, std::max(thickness, strokeWidth / 3));

            if (isVerticalLine) {
                p->fillRect(FloatRect(point1.x(), point1.y(), thickness, cornerWidth), strokeColor);
                p->fillRect(FloatRect(point1.x(), point2.y() - cornerWidth, thickness, cornerWidth),  strokeColor);
            } else {
                p->fillRect(FloatRect(point1.x(), point1.y(), cornerWidth, thickness), strokeColor);
                p->fillRect(FloatRect(point2.x() - cornerWidth, point1.y(), cornerWidth, thickness), strokeColor);
            }

            strokeWidth -= 2 * cornerWidth;
            float patternWidth = strokeStyle == DottedStroke ? thickness : std::min(3 * thickness, std::max(thickness, strokeWidth / 3));
            // Check if corner drawing sufficiently covers the line.
            if (strokeWidth <= patternWidth + 1) {
                p->restore();
                return;
            }

            // Pattern starts with full fill and ends with the empty fill.
            // 1. Let's start with the empty phase after the corner.
            // 2. Check if we've got odd or even number of patterns and whether they fully cover the line.
            // 3. In case of even number of patterns and/or remainder, move the pattern start position
            // so that the pattern is balanced between the corners.
            float patternOffset = patternWidth;
            int numberOfSegments = std::floor(strokeWidth / patternWidth);
            bool oddNumberOfSegments = numberOfSegments % 2;
            float remainder = strokeWidth - (numberOfSegments * patternWidth);
            if (oddNumberOfSegments && remainder)
                patternOffset -= remainder / 2.f;
            else if (!oddNumberOfSegments) {
                if (remainder)
                    patternOffset += patternOffset - (patternWidth + remainder) / 2.f;
                else
                    patternOffset += patternWidth / 2.f;
            }

            Qt::PenCapStyle capStyle = Qt::FlatCap;
            QVector<qreal> dashes { patternWidth / thickness, patternWidth / thickness };

            QPen pen = p->pen();
            pen.setCapStyle(capStyle);
            pen.setDashPattern(dashes);
            pen.setDashOffset(patternOffset / thickness);
            p->setPen(pen);
        }

        FloatPoint p1 = point1;
        FloatPoint p2 = point2;
        // Center line and cut off corners for pattern patining.
        if (isVerticalLine) {
            float centerOffset = (p2.x() - p1.x()) / 2;
            p1.move(centerOffset, cornerWidth);
            p2.move(-centerOffset, -cornerWidth);
        } else {
            float centerOffset = (p2.y() - p1.y()) / 2;
            p1.move(cornerWidth, centerOffset);
            p2.move(-cornerWidth, -centerOffset);
        }

        p->drawLine(p1, p2);

        if (drawsDashedLine)
            p->restore();

        p->setRenderHint(QPainter::Antialiasing, savedAntiAlias);
    } else {
        if (drawsDashedLine) {
            unimplementedFastUIDrawMessage("-Dashed");
        }
        fastuidraw::vec2 pts[2];

        pts[0] = vec2FromFloatPoint(point1);
        pts[1] = vec2FromFloatPoint(point2);
        m_data->fastuidraw()->stroke_line_strip(fastuidraw::PainterData(m_data->fastuidraw_state().m_stroke_brush.packed_value(),
                                                                        m_data->fastuidraw_state().m_stroke_params.packed_value()),
                                                fastuidraw::c_array<const fastuidraw::vec2>(pts, 2),
                                                m_data->fastuidraw_state().m_stroke_style,
                                                m_data->fastuidraw_state().m_stroke_aa);
    }
}

// This method is only used to draw the little circles used in lists.
void GraphicsContext::drawEllipse(const FloatRect& rect)
{
    FUID_TRACE;
    if (paintingDisabled())
        return;

    if (m_data->is_qt()) {
        m_data->p()->drawEllipse(rect);
    } else {
        /* This can be realized as drawing a circle, with the
         * main caveat that the stroking won't be quite right.
         */
        m_data->fastuidraw()->save();
        m_data->fastuidraw()->translate(vec2FromFloatPoint(rect.location()));
        m_data->fastuidraw()->shear(rect.width(), rect.height());

        m_data->fastuidraw()->fill_path(fastuidraw::PainterData(m_data->fastuidraw_state().m_fill_brush.packed_value()),
                                        m_data->m_fastuidraw_circle_path,
                                        fastuidraw::Painter::nonzero_fill_rule,
                                        m_data->fastuidraw_state().m_fill_aa);

        m_data->fastuidraw()->clip_out_path(m_data->m_fastuidraw_circle_path,
                                            fastuidraw::Painter::nonzero_fill_rule);

        /* we are going to cheat on the stroking params;
         * we will scale by the geometric average of the
         * inverse of the shearing factors.
         */
        fastuidraw::PainterStrokeParams stroke_params;
        float w;

        w = m_data->fastuidraw_state().m_stroke_params.constant_value().width();
        w /= fastuidraw::t_sqrt(fastuidraw::t_abs(rect.width() * rect.height()));
        stroke_params.width(w);
        m_data->fastuidraw()->stroke_path(fastuidraw::PainterData(m_data->fastuidraw_state().m_stroke_brush.packed_value(),
                                                                  &stroke_params),
                                          m_data->m_fastuidraw_circle_path,
                                          m_data->fastuidraw_state().m_stroke_style,
                                          m_data->fastuidraw_state().m_stroke_aa);
                
        m_data->fastuidraw()->restore();
    }
}

bool GraphicsContext::drawGradientPattern(const Gradient &gradient,
                                          const FloatRect& srcRect, const AffineTransform& patternTransform,
                                          const FloatPoint& phase, const FloatSize& spacing, CompositeOperator compositeOp,
                                          const FloatRect& dstRect, BlendMode blendMode)
{
    FUID_TRACE;
    if (m_data->is_qt()) {
        return false;
    }

    m_data->fastuidraw()->save();

    m_data->fastuidraw()->blend_shader(toFastUIDrawBlendMode(blendMode));
    m_data->fastuidraw()->composite_shader(toFastUIDrawCompositeMode(compositeOp));

    fastuidraw::PainterBrush brush;

    gradient.readyFastUIDrawBrush(brush);
    compose_with_pattern(brush, srcRect, patternTransform, phase, spacing);
    brush
      .apply_translate(fastuidraw::vec2(-dstRect.x(), -dstRect.y()));
    m_data->fastuidraw()->fill_rect(fastuidraw::PainterData(&brush),
                                    rectFromFloatRect(dstRect),
                                    m_data->fastuidraw_state().m_fill_aa);
    
    m_data->fastuidraw()->restore();

    return true;
}

void GraphicsContext::drawPattern(Image& image, const FloatRect& tileRect, const AffineTransform& patternTransform,
    const FloatPoint& phase, const FloatSize& spacing, CompositeOperator op, const FloatRect &destRect, BlendMode blendMode)
{
    FUID_TRACE;
    if (paintingDisabled() || !patternTransform.isInvertible())
        return;

    if (isRecording()) {
        m_displayListRecorder->drawPattern(image, tileRect, patternTransform, phase, spacing, op, destRect, blendMode);
        return;
    }

    if (m_data->is_qt()) {
        QPixmap* framePixmap = image.nativeImageForCurrentFrame();
        if (!framePixmap) // If it's too early we won't have an image yet.
            return;

#if ENABLE(IMAGE_DECODER_DOWN_SAMPLING)
        FloatRect tileRectAdjusted = adjustSourceRectForDownSampling(tileRect, framePixmap->size());
#else
        FloatRect tileRectAdjusted = tileRect;
#endif

        // Qt interprets 0 width/height as full width/height so just short circuit.
        QRectF dr = QRectF(destRect).normalized();
        QRect tr = QRectF(tileRectAdjusted).toRect().normalized();
        if (!dr.width() || !dr.height() || !tr.width() || !tr.height())
            return;

        QPixmap pixmap = *framePixmap;
        if (tr.x() || tr.y() || tr.width() != pixmap.width() || tr.height() != pixmap.height())
            pixmap = pixmap.copy(tr);

        QPoint trTopLeft = tr.topLeft();

        CompositeOperator previousOperator = compositeOperation();

        setCompositeOperation(!pixmap.hasAlpha() && op == CompositeSourceOver ? CompositeCopy : op);

        QPainter* p = &platformContext()->qt();
        QTransform transform(patternTransform);

        QTransform combinedTransform = p->combinedTransform();
        QTransform targetScaleTransform = QTransform::fromScale(combinedTransform.m11(), combinedTransform.m22());
        QTransform transformWithTargetScale = transform * targetScaleTransform;

        // If this would draw more than one scaled tile, we scale the pixmap first and then use the result to draw.
        if (transformWithTargetScale.type() == QTransform::TxScale) {
            QRectF tileRectInTargetCoords = (transformWithTargetScale * QTransform().translate(phase.x(), phase.y())).mapRect(tr);

            bool tileWillBePaintedOnlyOnce = tileRectInTargetCoords.contains(dr);
            if (!tileWillBePaintedOnlyOnce) {
                QSizeF scaledSize(qreal(pixmap.width()) * transformWithTargetScale.m11(), qreal(pixmap.height()) * transformWithTargetScale.m22());
                QPixmap scaledPixmap(scaledSize.toSize());
                if (pixmap.hasAlpha())
                    scaledPixmap.fill(Qt::transparent);

                {
                    QPainter painter(&scaledPixmap);
                    painter.setCompositionMode(QPainter::CompositionMode_Source);
                    painter.setRenderHints(p->renderHints());
                    painter.drawPixmap(QRect(0, 0, scaledPixmap.width(), scaledPixmap.height()), pixmap);
                }
                pixmap = scaledPixmap;
                trTopLeft = transformWithTargetScale.map(trTopLeft);
                transform = targetScaleTransform.inverted().translate(transform.dx(), transform.dy());
            }
        }

        /* Translate the coordinates as phase is not in world matrix coordinate space but the tile rect origin is. */
        transform *= QTransform().translate(phase.x(), phase.y());
        transform.translate(trTopLeft.x(), trTopLeft.y());

        QBrush b(pixmap);
        b.setTransform(transform);
        p->fillRect(dr, b);

        setCompositeOperation(previousOperator);
    } else {
        m_data->fastuidraw()->save();

        m_data->fastuidraw()->blend_shader(toFastUIDrawBlendMode(blendMode));
        m_data->fastuidraw()->composite_shader(toFastUIDrawCompositeMode(op));

        fastuidraw::PainterBrush brush;

        image.readyFastUIDrawBrush(brush);
        compose_with_pattern(brush, tileRect, patternTransform, phase, spacing);

        m_data->fastuidraw()->fill_rect(fastuidraw::PainterData(&brush),
                                        rectFromFloatRect(destRect),
                                        m_data->fastuidraw_state().m_fill_aa);
    
        m_data->fastuidraw()->restore();
    }
}

/*
 FIXME: Removed in https://bugs.webkit.org/show_bug.cgi?id=153174
 Find out if we need to adjust anything

void GraphicsContext::drawConvexPolygon(size_t npoints, const FloatPoint* points, bool shouldAntialias)
{
    if (paintingDisabled())
        return;

    if (npoints <= 1)
        return;

    QPolygonF polygon(npoints);

    for (size_t i = 0; i < npoints; i++)
        polygon[i] = points[i];

    QPainter* p = m_data->p();

    const bool antiAlias = p->testRenderHint(QPainter::Antialiasing);
    p->setRenderHint(QPainter::Antialiasing, shouldAntialias);

    p->drawConvexPolygon(polygon);

    p->setRenderHint(QPainter::Antialiasing, antiAlias);
}

void GraphicsContext::clipConvexPolygon(size_t numPoints, const FloatPoint* points, bool antialiased)
{
    if (paintingDisabled())
        return;

    if (numPoints <= 1)
        return;

    QPainterPath path(points[0]);
    for (size_t i = 1; i < numPoints; ++i)
        path.lineTo(points[i]);
    path.setFillRule(Qt::WindingFill);

    QPainter* p = m_data->p();

    bool painterWasAntialiased = p->testRenderHint(QPainter::Antialiasing);

    if (painterWasAntialiased != antialiased)
        p->setRenderHint(QPainter::Antialiasing, antialiased);

    p->setClipPath(path, Qt::IntersectClip);

    if (painterWasAntialiased != antialiased)
        p->setRenderHint(QPainter::Antialiasing, painterWasAntialiased);
}
*/

void GraphicsContext::fillPath(const Path& path)
{
    FUID_TRACE;
    if (paintingDisabled())
        return;

    if (m_data->is_qt()) {
        QPainter* p = m_data->p();
        QPainterPath platformPath = path.platformPath();
        platformPath.setFillRule(toQtFillRule(fillRule()));

        if (hasShadow()) {
            if (mustUseShadowBlur() || m_state.fillPattern || m_state.fillGradient) {
                ShadowBlur shadow(m_state);
                GraphicsContext* shadowContext = shadow.beginShadowLayer(*this, platformPath.controlPointRect());
                if (shadowContext) {
                    QPainter* shadowPainter = &shadowContext->platformContext()->qt();
                    if (m_state.fillPattern) {
                        shadowPainter->fillPath(platformPath, QBrush(m_state.fillPattern->createPlatformPattern()));
                    } else if (m_state.fillGradient) {
                        QBrush brush(*m_state.fillGradient->platformGradient());
                        brush.setTransform(m_state.fillGradient->gradientSpaceTransform());
                        shadowPainter->fillPath(platformPath, brush);
                    } else {
                        shadowPainter->fillPath(platformPath, p->brush());
                    }
                    shadow.endShadowLayer(*this);
                }
            } else {
                QPointF offset(m_state.shadowOffset.width(), m_state.shadowOffset.height());
                p->translate(offset);
                QColor shadowColor = m_state.shadowColor;
                shadowColor.setAlphaF(shadowColor.alphaF() * p->brush().color().alphaF());
                p->fillPath(platformPath, shadowColor);
                p->translate(-offset);
            }
        }
        if (m_state.fillPattern) {
            p->fillPath(platformPath, QBrush(m_state.fillPattern->createPlatformPattern()));
        } else if (m_state.fillGradient) {
            QBrush brush(*m_state.fillGradient->platformGradient());
            brush.setTransform(m_state.fillGradient->gradientSpaceTransform());
            p->fillPath(platformPath, brush);
        } else
            p->fillPath(platformPath, p->brush());
    } else {
        if (hasShadow()) {
            unimplementedFastUIDrawMessage("-Shadow");
        }
        if (m_state.fillPattern) {
            unimplementedFastUIDrawMessage("-fillPattern");
        }
        m_data->fastuidraw()->fill_path(fastuidraw::PainterData(m_data->fastuidraw_state().m_fill_brush.packed_value()),
                                        path.FastUIDrawPath(),
                                        toFastUIDrawFillRule(fillRule()),
                                        m_data->fastuidraw_state().m_fill_aa);
    }
}

inline static void fillPathStroke(QPainter* painter, const QPainterPath& platformPath, const QPen& pen)
{
    if (pen.color().alphaF() < 1.0) {
        QPainterPathStroker pathStroker;
        pathStroker.setJoinStyle(pen.joinStyle());
        pathStroker.setDashOffset(pen.dashOffset());
        pathStroker.setDashPattern(pen.dashPattern());
        pathStroker.setMiterLimit(pen.miterLimit());
        pathStroker.setCapStyle(pen.capStyle());
        pathStroker.setWidth(pen.widthF());
        
        QPainterPath stroke = pathStroker.createStroke(platformPath);
        painter->fillPath(stroke, pen.brush());
    } else {
        painter->strokePath(platformPath, pen);
    }
}

void GraphicsContext::strokePath(const Path& path)
{
    FUID_TRACE;
    if (paintingDisabled())
        return;

    if (m_data->is_qt()) {
        QPainter* p = m_data->p();
        QPen pen(p->pen());
        QPainterPath platformPath = path.platformPath();
        platformPath.setFillRule(toQtFillRule(fillRule()));

        if (hasShadow()) {
            if (mustUseShadowBlur() || m_state.strokePattern || m_state.strokeGradient) {
                ShadowBlur shadow(m_state);
                FloatRect boundingRect = platformPath.controlPointRect();
                boundingRect.inflate(pen.miterLimit() + pen.widthF());
                GraphicsContext* shadowContext = shadow.beginShadowLayer(*this, boundingRect);
                if (shadowContext) {
                    QPainter* shadowPainter = &shadowContext->platformContext()->qt();
                    if (m_state.strokeGradient) {
                        QBrush brush(*m_state.strokeGradient->platformGradient());
                        brush.setTransform(m_state.strokeGradient->gradientSpaceTransform());
                        QPen shadowPen(pen);
                        shadowPen.setBrush(brush);
                        fillPathStroke(shadowPainter, platformPath, shadowPen);
                    } else {
                        fillPathStroke(shadowPainter, platformPath, pen);
                    }
                    shadow.endShadowLayer(*this);
                }
            } else {
                QPointF offset(m_state.shadowOffset.width(), m_state.shadowOffset.height());
                p->translate(offset);
                QColor shadowColor = m_state.shadowColor;
                shadowColor.setAlphaF(shadowColor.alphaF() * pen.color().alphaF());
                QPen shadowPen(pen);
                shadowPen.setColor(shadowColor);
                fillPathStroke(p, platformPath, shadowPen);
                p->translate(-offset);
            }
        }

        if (m_state.strokePattern) {
            QBrush brush = m_state.strokePattern->createPlatformPattern();
            pen.setBrush(brush);
            fillPathStroke(p, platformPath, pen);
        } else if (m_state.strokeGradient) {
            QBrush brush(*m_state.strokeGradient->platformGradient());
            brush.setTransform(m_state.strokeGradient->gradientSpaceTransform());
            pen.setBrush(brush);
            fillPathStroke(p, platformPath, pen);
        } else
            fillPathStroke(p, platformPath, pen);
    } else {
        if (hasShadow()) {
            unimplementedFastUIDrawMessage("-Shadow");
        }
        m_data->fastuidraw()->stroke_path(fastuidraw::PainterData(m_data->fastuidraw_state().m_stroke_brush.packed_value(),
                                                                  m_data->fastuidraw_state().m_stroke_params.packed_value()),
                                          path.FastUIDrawPath(),
                                          m_data->fastuidraw_state().m_stroke_style,
                                          m_data->fastuidraw_state().m_stroke_aa);
    }
}

static inline void drawRepeatPattern(QPainter* p, Pattern& pattern, const FloatRect& rect)
{
    const QBrush brush = pattern.createPlatformPattern();
    if (brush.style() != Qt::TexturePattern)
        return;

    const bool repeatX = pattern.repeatX();
    const bool repeatY = pattern.repeatY();
    // Patterns must be painted so that the top left of the first image is anchored at
    // the origin of the coordinate space

    QRectF targetRect(rect);
    const int w = brush.texture().width();
    const int h = brush.texture().height();

    ASSERT(p);
    QRegion oldClip;
    if (p->hasClipping())
        oldClip = p->clipRegion();

    QRectF clip = targetRect;
    QRectF patternRect = brush.transform().mapRect(QRectF(0, 0, w, h));
    if (!repeatX) {
        clip.setLeft(patternRect.left());
        clip.setWidth(patternRect.width());
    }
    if (!repeatY) {
        clip.setTop(patternRect.top());
        clip.setHeight(patternRect.height());
    }
    if (!repeatX || !repeatY)
        p->setClipRect(clip);

    p->fillRect(targetRect, brush);

    if (!oldClip.isEmpty())
        p->setClipRegion(oldClip);
    else if (!repeatX || !repeatY)
        p->setClipping(false);
}

void GraphicsContext::fillRect(const FloatRect& rect)
{
    FUID_TRACE;
    if (paintingDisabled())
        return;

    if (m_data->is_qt()) {
        QPainter* p = m_data->p();
        QRectF normalizedRect = rect.normalized();

        if (m_state.fillPattern) {
            if (hasShadow()) {
                ShadowBlur shadow(m_state);
                GraphicsContext* shadowContext = shadow.beginShadowLayer(*this, normalizedRect);
                if (shadowContext) {
                    QPainter* shadowPainter = &shadowContext->platformContext()->qt();
                    drawRepeatPattern(shadowPainter, *m_state.fillPattern, normalizedRect);
                    shadow.endShadowLayer(*this);
                }
            }
            drawRepeatPattern(p, *m_state.fillPattern, normalizedRect);
        } else if (m_state.fillGradient) {
            QBrush brush(*m_state.fillGradient->platformGradient());
            brush.setTransform(m_state.fillGradient->gradientSpaceTransform());
            if (hasShadow()) {
                ShadowBlur shadow(m_state);
                GraphicsContext* shadowContext = shadow.beginShadowLayer(*this, normalizedRect);
                if (shadowContext) {
                    QPainter* shadowPainter = &shadowContext->platformContext()->qt();
                    shadowPainter->fillRect(normalizedRect, brush);
                    shadow.endShadowLayer(*this);
                }
            }
            p->fillRect(normalizedRect, brush);
        } else {
            if (hasShadow()) {
                if (mustUseShadowBlur()) {
                    ShadowBlur shadow(m_state);
                    // drawRectShadowWithTiling does not work with rotations, and the fallback of
                    // drawing though clipToImageBuffer() produces scaling artifacts for us.
                    if (!getCTM().preservesAxisAlignment()) {
                        GraphicsContext* shadowContext = shadow.beginShadowLayer(*this, normalizedRect);
                        if (shadowContext) {
                            QPainter* shadowPainter = &shadowContext->platformContext()->qt();
                            shadowPainter->fillRect(normalizedRect, p->brush());
                            shadow.endShadowLayer(*this);
                        }
                    } else
                        shadow.drawRectShadow(*this, FloatRoundedRect(rect));
                } else {
                    // Solid rectangle fill with no blur shadow or transformations applied can be done
                    // faster without using the shadow layer at all.
                    QColor shadowColor = m_state.shadowColor;
                    shadowColor.setAlphaF(shadowColor.alphaF() * p->brush().color().alphaF());
                    p->fillRect(normalizedRect.translated(QPointF(m_state.shadowOffset.width(), m_state.shadowOffset.height())), shadowColor);
                }
            }

            p->fillRect(normalizedRect, p->brush());
        }
    } else {
        if (hasShadow()) {
            unimplementedFastUIDrawMessage("->Shadow");
        }
        QRectF normalizedRect = rect.normalized();
        m_data->fastuidraw()->fill_rect(fastuidraw::PainterData(m_data->fastuidraw_state().m_fill_brush.packed_value()),
                                        rectFromFloatRect(normalizedRect),
                                        m_data->fastuidraw_state().m_fill_aa);
    }
}


void GraphicsContext::fillRect(const FloatRect& rect, const Color& color)
{
    FUID_TRACE;
    if (paintingDisabled() || !color.isValid())
        return;

    if (m_data->is_qt()) {
        QRectF platformRect(rect);
        QPainter* p = m_data->p();
        if (hasShadow()) {
            if (mustUseShadowBlur()) {
                ShadowBlur shadow(m_state);
                shadow.drawRectShadow(*this, FloatRoundedRect(platformRect));
            } else {
                QColor shadowColor = m_state.shadowColor;
                shadowColor.setAlphaF(shadowColor.alphaF() * p->brush().color().alphaF());
                p->fillRect(platformRect.translated(QPointF(m_state.shadowOffset.width(), m_state.shadowOffset.height())), shadowColor);
            }
        }
        p->fillRect(platformRect, QColor(color));
    } else {
        if (hasShadow()) {
            unimplementedFastUIDrawMessage("->Shadow");
        }
        fastuidraw::PainterBrush fillBrush;
        fillBrush.color(FastUIDrawColorValue(color, alpha()));
        m_data->fastuidraw()->fill_rect(fastuidraw::PainterData(&fillBrush),
                                        rectFromFloatRect(rect),
                                        m_data->fastuidraw_state().m_fill_aa);
    }
}

void GraphicsContext::fillRect(const FloatRect& rect, Gradient& gradient)
{
    FUID_TRACE;
    if (paintingDisabled())
        return;

    if (m_data->is_qt()) {
        QPainter* p = m_data->p();
        p->fillRect(rect, *gradient.platformGradient());
    } else {
        fastuidraw::reference_counted_ptr<const fastuidraw::ColorStopSequenceOnAtlas> cs(gradient.fastuidrawGradient());
        fastuidraw::PainterBrush brush;

        gradient.readyFastUIDrawBrush(brush);
        m_data->fastuidraw()->fill_rect(fastuidraw::PainterData(&brush),
                                        fastuidraw::Rect()
                                        .min_point(rect.x(), rect.y())
                                        .size(rect.width(), rect.height()));
    }
}

void GraphicsContext::platformFillRoundedRect(const FloatRoundedRect& rect, const Color& color)
{
    FUID_TRACE;
    if (paintingDisabled() || !color.isValid())
        return;

    if (m_data->is_qt()) {
        Path path;
        path.addRoundedRect(rect);
        QPainter* p = m_data->p();
        if (hasShadow()) {
            if (mustUseShadowBlur()) {
                ShadowBlur shadow(m_state);
                shadow.drawRectShadow(*this, rect);
            } else {
                const QPointF shadowOffset(m_state.shadowOffset.width(), m_state.shadowOffset.height());
                p->translate(shadowOffset);
                p->fillPath(path.platformPath(), QColor(m_state.shadowColor));
                p->translate(-shadowOffset);
            }
        }
        p->fillPath(path.platformPath(), QColor(color));
    } else {
        fastuidraw::RoundedRect fud_rect;

        createFastUIDrawRoundedRect(rect, &fud_rect);
        if (hasShadow()) {
            unimplementedFastUIDrawMessage("->Shadow");
        }
        fastuidraw::PainterBrush brush(FastUIDrawColorValue(color, alpha()));
        m_data->fastuidraw()->fill_rounded_rect(fastuidraw::PainterData(&brush),
                                                fud_rect,
                                                m_data->fastuidraw_state().m_fill_aa);
    }
}

void GraphicsContext::fillRectWithRoundedHole(const FloatRect& rect, const FloatRoundedRect& roundedHoleRect, const Color& color)
{
    FUID_TRACE;
    if (paintingDisabled() || !color.isValid())
        return;

    if (m_data->is_qt()) {
        Path path;
        path.addRect(rect);
        if (!roundedHoleRect.radii().isZero())
            path.addRoundedRect(roundedHoleRect);
        else
            path.addRect(roundedHoleRect.rect());

        QPainterPath platformPath = path.platformPath();
        platformPath.setFillRule(Qt::OddEvenFill);

        QPainter* p = m_data->p();
        if (hasShadow()) {
            if (mustUseShadowBlur()) {
                ShadowBlur shadow(m_state);
                shadow.drawInsetShadow(*this, rect, roundedHoleRect);
            } else {
                const QPointF shadowOffset(m_state.shadowOffset.width(), m_state.shadowOffset.height());
                p->translate(shadowOffset);
                p->fillPath(platformPath, QColor(m_state.shadowColor));
                p->translate(-shadowOffset);
            }
        }

        p->fillPath(platformPath, QColor(color));
    } else {
        if (hasShadow()) {
            unimplementedFastUIDrawMessage("-Shadow");
        }
        save();
        platformClipOutRoundedRect(roundedHoleRect);
        fillRect(rect, color);
        restore();
    }
}

void GraphicsContext::clip(const FloatRect& rect)
{
    FUID_TRACE;
    if (paintingDisabled())
        return;

    if (m_data->is_qt()) {
        m_data->p()->setClipRect(rect, Qt::IntersectClip);
    } else {
        m_data->fastuidraw()->clip_in_rect(rectFromFloatRect(rect));
    }
}

IntRect GraphicsContext::clipBounds() const
{
    FUID_TRACE;
    if (m_data->is_qt()) {
        QPainter* p = m_data->p();
        QRectF clipRect;

        clipRect = p->transform().inverted().mapRect(p->window());

        if (p->hasClipping())
            clipRect = clipRect.intersected(m_data->clipBoundingRect());

        return enclosingIntRect(clipRect);
    } else {
        fastuidraw::vec2 min_bb, max_bb, size_bb;

        m_data->fastuidraw()->clip_region_logical_bounds(&min_bb, &max_bb);
        size_bb = max_bb - min_bb;
        
        return enclosingIntRect(QRectF(min_bb.x(), min_bb.y(),
                                       size_bb.x(), size_bb.y()));
    }
}

bool GraphicsContext::platformClipRoundedRect(const FloatRoundedRect &rect)
{
    FUID_TRACE;
    if (m_data->is_qt())
        return false;

    fastuidraw::RoundedRect fud_rect;
    createFastUIDrawRoundedRect(rect, &fud_rect);
    m_data->fastuidraw()->clip_in_rounded_rect(fud_rect);
    
    return true;
}

bool GraphicsContext::platformClipOutRoundedRect(const FloatRoundedRect &rect)
{
    FUID_TRACE;
    if (m_data->is_qt())
        return false;

    fastuidraw::RoundedRect fud_rect;
    createFastUIDrawRoundedRect(rect, &fud_rect);
    m_data->fastuidraw()->clip_out_rounded_rect(fud_rect);

    return true;
}

void GraphicsContext::clipPath(const Path& path, WindRule clipRule)
{
    FUID_TRACE;
    if (paintingDisabled())
        return;

    if (m_data->is_qt()) {
        QPainter* p = m_data->p();
        QPainterPath platformPath = path.platformPath();
        platformPath.setFillRule(clipRule == RULE_EVENODD ? Qt::OddEvenFill : Qt::WindingFill);
        p->setClipPath(platformPath, Qt::IntersectClip);
    } else {
        m_data->fastuidraw()->clip_in_path(path.FastUIDrawPath(), toFastUIDrawFillRule(clipRule));
    }
}

void GraphicsContext::clipToImageBuffer(ImageBuffer& buffer, const FloatRect& destRect)
{
    FUID_TRACE;
    if (paintingDisabled())
        return;

    if (m_data->is_qt()) {
        IntRect rect = enclosingIntRect(destRect);
        buffer.m_data.m_impl->clip(*this, rect);
    } else {
        unimplementedFastUIDraw();
    }
}

void drawFocusRingForPath(QPainter* p, const QPainterPath& path, const Color& color, bool antiAliasing)
{
    const bool antiAlias = p->testRenderHint(QPainter::Antialiasing);
    p->setRenderHint(QPainter::Antialiasing, antiAliasing);

    const QPen oldPen = p->pen();
    const QBrush oldBrush = p->brush();

    QPen nPen = p->pen();
    nPen.setColor(color);
    p->setBrush(Qt::NoBrush);
    nPen.setStyle(Qt::DotLine);

    p->strokePath(path, nPen);
    p->setBrush(oldBrush);
    p->setPen(oldPen);

    p->setRenderHint(QPainter::Antialiasing, antiAlias);
}

void GraphicsContext::drawFocusRing(const Path& path, float /* width */, float /* offset */, const Color& color)
{
    FUID_TRACE;
    // FIXME: Use 'offset' for something? http://webkit.org/b/49909

    if (paintingDisabled() || !color.isValid())
        return;

    if (m_data->is_qt()) {
        drawFocusRingForPath(m_data->p(), path.platformPath(), color, m_data->antiAliasingForRectsAndLines);
    } else {
        unimplementedFastUIDraw();
    }
}

/**
 * Focus ring handling for form controls is not handled here. Qt style in
 * RenderTheme handles drawing focus on widgets which 
 * need it. It is still handled here for links.
 */
void GraphicsContext::drawFocusRing(const Vector<FloatRect>& rects, float width, float offset, const Color& color)
{
    FUID_TRACE;
    if (paintingDisabled() || !color.isValid())
        return;

    if (m_data->is_qt()) {
        unsigned rectCount = rects.size();

        if (!rects.size())
            return;

        float radius = (width - 1) / 2;
        QPainterPath path;
        for (unsigned i = 0; i < rectCount; ++i) {
            QRectF rect = QRectF(rects[i]).adjusted(-offset - radius, -offset - radius, offset + radius, offset + radius);
            // This is not the most efficient way to add a rect to a path, but if we don't create the tmpPath,
            // we will end up with ugly lines in between rows of text on anchors with multiple lines.
            QPainterPath tmpPath;
            tmpPath.addRoundedRect(rect, radius, radius);
            path = path.united(tmpPath);
        }
        drawFocusRingForPath(m_data->p(), path, color, m_data->antiAliasingForRectsAndLines);
    } else {
        unimplementedFastUIDraw();
    }
}

void GraphicsContext::drawLineForText(const FloatPoint& origin, float width, bool printing, bool doubleLines)
{
    FUID_TRACE;
    if (paintingDisabled())
        return;

    if (isRecording()) {
        DashArray widths;
        widths.append(width);
        widths.append(0);
        m_displayListRecorder->drawLinesForText(origin, widths, printing, doubleLines, strokeThickness());
        return;
    }

    if (m_data->is_qt()) {
        Color localStrokeColor(strokeColor());

        FloatRect bounds = computeLineBoundsAndAntialiasingModeForText(origin, width, printing, localStrokeColor);
        bool strokeColorChanged = strokeColor() != localStrokeColor;
        bool strokeThicknessChanged = strokeThickness() != bounds.height();
        bool needSavePen = strokeColorChanged || strokeThicknessChanged;

        QPainter* p = &platformContext()->qt();
        const bool savedAntiAlias = p->testRenderHint(QPainter::Antialiasing);
        p->setRenderHint(QPainter::Antialiasing, m_data->antiAliasingForRectsAndLines);

        QPen oldPen(p->pen());
        if (needSavePen) {
            QPen newPen(oldPen);
            if (strokeThicknessChanged)
                newPen.setWidthF(bounds.height());
            if (strokeColorChanged)
                newPen.setColor(localStrokeColor);
            p->setPen(newPen);
        }

        QPointF startPoint = bounds.location();
        startPoint.setY(startPoint.y() + bounds.height() / 2);
        QPointF endPoint = startPoint;
        endPoint.setX(endPoint.x() + bounds.width());

        p->drawLine(startPoint, endPoint);

        if (doubleLines) {
            // The space between double underlines is equal to the height of the underline
            // so distance between line centers is 2x height
            startPoint.setY(startPoint.y() + 2 * bounds.height());
            endPoint.setY(endPoint.y() + 2 * bounds.height());
            p->drawLine(startPoint, endPoint);
        }

        if (needSavePen)
            p->setPen(oldPen);

        p->setRenderHint(QPainter::Antialiasing, savedAntiAlias);
    } else {
        unimplementedFastUIDraw();
    }
}

// NOTE: this code is based on GraphicsContextCG implementation
void GraphicsContext::drawLinesForText(const FloatPoint& origin, const DashArray& widths, bool printing, bool doubleLines)
{
    FUID_TRACE;
    if (paintingDisabled())
        return;

    if (widths.size() <= 0)
        return;

    if (isRecording()) {
        m_displayListRecorder->drawLinesForText(origin, widths, printing, doubleLines, strokeThickness());
        return;
    }

    if (m_data->is_qt()) {
        Color localStrokeColor(strokeColor());

        FloatRect bounds = computeLineBoundsAndAntialiasingModeForText(origin, widths.last(), printing, localStrokeColor);
        bool fillColorIsNotEqualToStrokeColor = fillColor() != localStrokeColor;

        // FIXME: drawRects() is significantly slower than drawLine() for thin lines (<= 1px)
        Vector<QRectF, 4> dashBounds;
        ASSERT(!(widths.size() % 2));
        dashBounds.reserveInitialCapacity(dashBounds.size() / 2);
        for (size_t i = 0; i < widths.size(); i += 2)
            dashBounds.append(QRectF(bounds.x() + widths[i], bounds.y(), widths[i+1] - widths[i], bounds.height()));

        if (doubleLines) {
            // The space between double underlines is equal to the height of the underline
            for (size_t i = 0; i < widths.size(); i += 2)
                dashBounds.append(QRectF(bounds.x() + widths[i], bounds.y() + 2 * bounds.height(), widths[i+1] - widths[i], bounds.height()));
        }

        QPainter* p = m_data->p();
        QPen oldPen = p->pen();
        p->setPen(Qt::NoPen);

        if (fillColorIsNotEqualToStrokeColor) {
            const QBrush oldBrush = p->brush();
            p->setBrush(QBrush(localStrokeColor));
            p->drawRects(dashBounds.data(), dashBounds.size());
            p->setBrush(oldBrush);
        } else {
            p->drawRects(dashBounds.data(), dashBounds.size());
        }

        p->setPen(oldPen);
    } else {
        unimplementedFastUIDraw();
    }
}


/*
 *   NOTE: This code is completely based upon the one from
 *   Source/WebCore/platform/graphics/cairo/DrawErrorUnderline.{h|cpp}
 *
 *   Draws an error underline that looks like one of:
 *
 *               H       E                H
 *      /\      /\      /\        /\      /\               -
 *    A/  \    /  \    /  \     A/  \    /  \              |
 *     \   \  /    \  /   /D     \   \  /    \             |
 *      \   \/  C   \/   /        \   \/   C  \            | height = heightSquares * square
 *       \      /\  F   /          \  F   /\   \           |
 *        \    /  \    /            \    /  \   \G         |
 *         \  /    \  /              \  /    \  /          |
 *          \/      \/                \/      \/           -
 *          B                         B
 *          |---|
 *        unitWidth = (heightSquares - 1) * square
 *
 *  The x, y, width, height passed in give the desired bounding box;
 *  x/width are adjusted to make the underline a integer number of units wide.
*/
static void drawErrorUnderline(QPainter *painter, qreal x, qreal y, qreal width, qreal height)
{
    const qreal heightSquares = 2.5;

    qreal square = height / heightSquares;
    qreal halfSquare = 0.5 * square;

    qreal unitWidth = (heightSquares - 1.0) * square;
    int widthUnits = static_cast<int>((width + 0.5 * unitWidth) / unitWidth);

    x += 0.5 * (width - widthUnits * unitWidth);
    width = widthUnits * unitWidth;

    qreal bottom = y + height;
    qreal top = y;

    QPainterPath path;

    // Bottom of squiggle.
    path.moveTo(x - halfSquare, top + halfSquare); // A

    int i = 0;
    for (i = 0; i < widthUnits; i += 2) {
        qreal middle = x + (i + 1) * unitWidth;
        qreal right = x + (i + 2) * unitWidth;

        path.lineTo(middle, bottom); // B

        if (i + 2 == widthUnits)
            path.lineTo(right + halfSquare, top + halfSquare); // D
        else if (i + 1 != widthUnits)
            path.lineTo(right, top + square); // C
    }

    // Top of squiggle.
    for (i -= 2; i >= 0; i -= 2) {
        qreal left = x + i * unitWidth;
        qreal middle = x + (i + 1) * unitWidth;
        qreal right = x + (i + 2) * unitWidth;

        if (i + 1 == widthUnits)
            path.lineTo(middle + halfSquare, bottom - halfSquare); // G
        else {
            if (i + 2 == widthUnits)
                path.lineTo(right, top); // E

            path.lineTo(middle, bottom - halfSquare); // F
        }

        path.lineTo(left, top); // H
    }

    painter->drawPath(path);
}

void GraphicsContext::updateDocumentMarkerResources()
{
    FUID_TRACE;
    // Unnecessary, since our document markers don't use resources.
}

void GraphicsContext::drawLineForDocumentMarker(const FloatPoint& origin, float width, DocumentMarkerLineStyle style)
{
    FUID_TRACE;
    if (paintingDisabled())
        return;

    if (m_data->is_qt()) {
        QPainter* painter = &platformContext()->qt();
        const QPen originalPen = painter->pen();

        switch (style) {
        case DocumentMarkerSpellingLineStyle:
            painter->setPen(Qt::red);
            break;
        case DocumentMarkerGrammarLineStyle:
            painter->setPen(Qt::green);
            break;
        default:
          return;
        }

        drawErrorUnderline(painter, origin.x(), origin.y(), width, cMisspellingLineThickness);
        painter->setPen(originalPen);
    } else {
        unimplementedFastUIDraw();
    }
}

FloatRect GraphicsContext::roundToDevicePixels(const FloatRect& frect, RoundingMode)
{
    FUID_TRACE;
    // It is not enough just to round to pixels in device space. The rotation part of the
    // affine transform matrix to device space can mess with this conversion if we have a
    // rotating image like the hands of the world clock widget. We just need the scale, so
    // we get the affine transform matrix and extract the scale.
    QTransform deviceTransform;
    if (m_data->is_qt()) {
        QPainter* painter = &platformContext()->qt();
        deviceTransform = painter->deviceTransform();
    } else {
        computeFromFastUIDrawMatrix(m_data->computeFastUIDrawCTM(), &deviceTransform);
    }
    if (deviceTransform.isIdentity())
        return frect;

    qreal deviceScaleX = sqrtf(deviceTransform.m11() * deviceTransform.m11() + deviceTransform.m12() * deviceTransform.m12());
    qreal deviceScaleY = sqrtf(deviceTransform.m21() * deviceTransform.m21() + deviceTransform.m22() * deviceTransform.m22());

    QPoint deviceOrigin(frect.x() * deviceScaleX, frect.y() * deviceScaleY);
    QPoint deviceLowerRight(frect.maxX() * deviceScaleX, frect.maxY() * deviceScaleY);

    // Don't let the height or width round to 0 unless either was originally 0
    if (deviceOrigin.y() == deviceLowerRight.y() && frect.height())
        deviceLowerRight.setY(deviceLowerRight.y() + 1);
    if (deviceOrigin.x() == deviceLowerRight.x() && frect.width())
        deviceLowerRight.setX(deviceLowerRight.x() + 1);

    FloatPoint roundedOrigin = FloatPoint(deviceOrigin.x() / deviceScaleX, deviceOrigin.y() / deviceScaleY);
    FloatPoint roundedLowerRight = FloatPoint(deviceLowerRight.x() / deviceScaleX, deviceLowerRight.y() / deviceScaleY);
    return FloatRect(roundedOrigin, roundedLowerRight - roundedOrigin);
}

void GraphicsContext::setPlatformShadow(const FloatSize& size, float, const Color&)
{
    FUID_TRACE;
    // Qt doesn't support shadows natively, they are drawn manually in the draw*
    // functions
    if (m_state.shadowsIgnoreTransforms) {
        // Meaning that this graphics context is associated with a CanvasRenderingContext
        // We flip the height since CG and HTML5 Canvas have opposite Y axis
        m_state.shadowOffset = FloatSize(size.width(), -size.height());
    }
}

void GraphicsContext::clearPlatformShadow()
{
    FUID_TRACE;
}

void GraphicsContext::pushTransparencyLayerInternal(const QRect &rect, qreal opacity, QPixmap& alphaMask)
{
    FUID_TRACE;
    /*
     * this method is only used to implement clipping when rendering to an
     * an offscreen buffer (the stack is
     *   GraphicsContext::clip()
     *   ImageBufferDataPrivate::clip() which is one of
     *      ImageBufferDataPrivateUnaccelerated::clip() or
     *      ImageBufferDataPrivateAccelerated::clip()
     */
    FASTUIDRAWunused(opacity);
    if (m_data->is_qt()) {
        QPainter* p = m_data->p();

        QTransform deviceTransform = p->transform();
        QRect deviceClip = deviceTransform.mapRect(rect);

        alphaMask = alphaMask.transformed(deviceTransform);
        if (alphaMask.width() != deviceClip.width() || alphaMask.height() != deviceClip.height())
            alphaMask = alphaMask.scaled(deviceClip.width(), deviceClip.height());

        m_data->layers.push(new TransparencyLayer(p, deviceClip, 1.0, alphaMask));
    }
}

void GraphicsContext::beginPlatformTransparencyLayer(float opacity)
{
    FUID_TRACE;
    if (paintingDisabled() || !EnableGraphicsContextTransparencyLayer)
        return;

    if (m_data->is_qt()) {
        int x, y, w, h;
        x = y = 0;
        QPainter* p = m_data->p();
        const QPaintDevice* device = p->device();
        w = device->width();
        h = device->height();

        if (p->hasClipping()) {
            QRectF clip = m_data->clipBoundingRect();
            QRectF deviceClip = p->transform().mapRect(clip);
            x = int(qBound(qreal(0), deviceClip.x(), (qreal)w));
            y = int(qBound(qreal(0), deviceClip.y(), (qreal)h));
            w = int(qBound(qreal(0), deviceClip.width(), (qreal)w) + 2);
            h = int(qBound(qreal(0), deviceClip.height(), (qreal)h) + 2);
        }

        //std::cout << "Begin Transparent layer at (" << x << ", "
        //        << y << "), size = (" << w << ", " << h << "), opacity = "
        //       << opacity << "\n";

        QPixmap emptyAlphaMask;
        m_data->layers.push(new TransparencyLayer(p, QRect(x, y, w, h), opacity, emptyAlphaMask));
        ++m_data->layerCount;
    } else {
        if (m_data->fastuidraw_options().m_use_fastuidaw_layers) {
            m_data->fastuidraw()->begin_layer(TransparencyLayerColor(opacity));
            m_data->m_fastuidraw_state_stack.push_back(m_data->fastuidraw_state());
        } else {
            fastuidraw::reference_counted_ptr<fastuidraw::Painter> p(m_data->fastuidraw());

            // ctor calls begin for us.
            m_data->m_fastuidraw_layers.push_back(FastUIDrawTransparencyLayer(m_data->m_root_surface, p, opacity));
            m_data->m_fastuidraw_state_stack.push_back(m_data->fastuidraw_state());
        }
    }
}

void GraphicsContext::popTransparencyLayerInternal()
{
    FUID_TRACE;
    /*
     * this method is only used internally in imlpementing
     * endPlatformTransparencyLayer() when popping transparency
     * in Qt.
     */
    TransparencyLayer* layer = m_data->layers.pop();
    ASSERT(!layer->alphaMask.isNull());
    ASSERT(layer->saveCounter == 0);
    layer->painter.resetTransform();
    layer->painter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    layer->painter.drawPixmap(QPoint(), layer->alphaMask);
    layer->painter.end();

    QPainter* p = m_data->p();
    p->save();
    p->resetTransform();
    p->setOpacity(layer->opacity);
    p->drawPixmap(layer->offset, layer->pixmap);
    p->restore();

    delete layer;
}

void GraphicsContext::endPlatformTransparencyLayer()
{
    FUID_TRACE;
    if (paintingDisabled() || !EnableGraphicsContextTransparencyLayer)
        return;


    if (m_data->is_qt()) {
        while ( ! m_data->layers.top()->alphaMask.isNull() ){
            --m_data->layers.top()->saveCounter;
            popTransparencyLayerInternal();
            if (m_data->layers.isEmpty())
                return;
        }

        TransparencyLayer* layer = m_data->layers.pop();
        --m_data->layerCount; // see the comment for layerCount
        layer->painter.end();

        QPainter* p = m_data->p();

        p->save();
        p->resetTransform();
        p->setOpacity(layer->opacity);
        p->drawPixmap(layer->offset, layer->pixmap);
        p->restore();

        //std::cout << "End transparent layer\n";

        delete layer;
    } else {
        if (m_data->fastuidraw_options().m_use_fastuidaw_layers) {
            m_data->m_fastuidraw_state_stack.pop_back();
            m_data->fastuidraw()->end_layer();
        } else {
            FastUIDrawTransparencyLayer layer(m_data->m_fastuidraw_layers.back());
            const fastuidraw::PainterBackend::Surface::Viewport vwp(m_data->m_root_surface->viewport());

            //std::cout << m_data->printPrefix() << "End transparency layer at "
            //        << m_data->m_fastuidraw_layers.back().m_blit_rect
            //        << "\n";

            m_data->m_fastuidraw_state_stack.pop_back();
            m_data->m_fastuidraw_layers.pop_back();
            layer.m_painter->painter()->end();

            /* now add the necesary draw-image command; the image
             * is to be drawn only with the transformation of from
             * pixel to normalized device coordinates. Note that
             * the projection we are providing is where y increases
             * upwards; this is because we are doing a blit and
             * the rectangle provided by FastUIDraw from
             * painter::clip_region_bounds() is oriented with (0, 0)
             * being the -bottom- left corner.
             */
            m_data->fastuidraw()->save();
            fastuidraw::float_orthogonal_projection_params pp(0, vwp.m_dimensions.x(), 0, vwp.m_dimensions.y());
            fastuidraw::PainterBrush brush;

            /* Note that we translate() the fastuidraw::Painter so that the rect to
             * be drawn is at the origin. We do this because we want the coordinates
             * fed to the brush to be from (0, 0) to layer.m_blit_rect.size().
             */
            brush
              .image(layer.m_image)
              .color(TransparencyLayerColor(layer.m_opacity));
            m_data->fastuidraw()->transformation(pp);
            m_data->fastuidraw()->translate(layer.m_blit_rect.m_min_point);
            
            m_data->fastuidraw()->fill_rect(fastuidraw::PainterData(&brush),
                                            fastuidraw::Rect().size(layer.m_blit_rect.size()),
                                            fastuidraw::Painter::shader_anti_alias_none);
            m_data->fastuidraw()->restore();
        }
    }
}

bool GraphicsContext::supportsTransparencyLayers()
{
    FUID_TRACE;
    return EnableGraphicsContextTransparencyLayer;
}

void GraphicsContext::supportsTransparencyLayers(bool v)
{
    FUID_TRACE;
    EnableGraphicsContextTransparencyLayer = v;
}

void GraphicsContext::clearRect(const FloatRect& rect)
{
    FUID_TRACE;
    if (paintingDisabled())
        return;

    if (m_data->is_qt()) {
        QPainter* p = m_data->p();
        QPainter::CompositionMode currentCompositionMode = p->compositionMode();
        p->setCompositionMode(QPainter::CompositionMode_Source);
        p->fillRect(rect, Qt::transparent);
        p->setCompositionMode(currentCompositionMode);
    } else {
        m_data->fastuidraw()->save();
        m_data->fastuidraw()->composite_shader(fastuidraw::Painter::composite_porter_duff_clear);
        m_data->fastuidraw()->blend_shader(fastuidraw::Painter::blend_w3c_normal);
        m_data->fastuidraw()->fill_rect(fastuidraw::PainterData(m_data->m_packed_black_brush),
                                        rectFromFloatRect(rect),
                                        m_data->fastuidraw_state().m_fill_aa);
        m_data->fastuidraw()->restore();
    }
}

void GraphicsContext::strokeRect(const FloatRect& rect, float lineWidth)
{
    FUID_TRACE;
    if (paintingDisabled())
        return;

    if (m_data->is_qt()) {
        Path path;
        path.addRect(rect);

        float previousStrokeThickness = strokeThickness();

        if (lineWidth != previousStrokeThickness)
            setStrokeThickness(lineWidth);

        strokePath(path);

        if (lineWidth != previousStrokeThickness)
            setStrokeThickness(previousStrokeThickness);
    } else {
        fastuidraw::Path path;
        fastuidraw::PainterStrokeParams stroke_params;

        path << fastuidraw::vec2(rect.x()               , rect.y())
             << fastuidraw::vec2(rect.x()               , rect.y() + rect.height())
             << fastuidraw::vec2(rect.x() + rect.width(), rect.y() + rect.height())
             << fastuidraw::vec2(rect.x() + rect.width(), rect.y())
             << fastuidraw::Path::contour_close();

        stroke_params
          .width(lineWidth);

        m_data->fastuidraw()->stroke_path(fastuidraw::PainterData(&stroke_params,
                                                                  m_data->fastuidraw_state().m_stroke_brush.packed_value()),
                                          path,
                                          m_data->fastuidraw_state().m_stroke_style,
                                          m_data->fastuidraw_state().m_stroke_aa);
    }
}

void GraphicsContext::setLineCap(LineCap lc)
{
    FUID_TRACE;
    if (paintingDisabled())
        return;

    if (m_data->is_qt()) {
        QPainter* p = m_data->p();
        QPen nPen = p->pen();
        nPen.setCapStyle(toQtLineCap(lc));
        p->setPen(nPen);
    } else {
        m_data->fastuidraw_state().m_stroke_style.cap_style(toFastUIDrawCapStyle(lc));
    }
}

void GraphicsContext::setLineDash(const DashArray& dashes, float dashOffset)
{
    FUID_TRACE;
    if (m_data->is_qt()) {
        QPainter* p = m_data->p();
        QPen pen = p->pen();
        unsigned dashLength = dashes.size();
        if (dashLength) {
            QVector<qreal> pattern;
            unsigned count = dashLength;
            if (dashLength % 2)
                count *= 2;

            float penWidth = narrowPrecisionToFloat(double(pen.widthF()));
            if (penWidth <= 0.f)
                penWidth = 1.f;

            for (unsigned i = 0; i < count; i++)
                pattern.append(dashes[i % dashLength] / penWidth);

            pen.setDashPattern(pattern);
            pen.setDashOffset(dashOffset / penWidth);
        } else
            pen.setStyle(Qt::SolidLine);
        p->setPen(pen);
    } else {
        unimplementedFastUIDraw();
    }
}

void GraphicsContext::setLineJoin(LineJoin lj)
{
    FUID_TRACE;
    if (paintingDisabled())
        return;

    if (m_data->is_qt()) {
        QPainter* p = m_data->p();
        QPen nPen = p->pen();
        nPen.setJoinStyle(toQtLineJoin(lj));
        p->setPen(nPen);
    } else {
        m_data->fastuidraw_state().m_stroke_style.join_style(toFastUIDrawLineJoin(lj));
    }
}

void GraphicsContext::setMiterLimit(float limit)
{
    FUID_TRACE;
    if (paintingDisabled())
        return;

    if (m_data->is_qt()) {
        QPainter* p = m_data->p();
        QPen nPen = p->pen();
        nPen.setMiterLimit(limit);
        p->setPen(nPen);
    } else {
        m_data->fastuidraw_state().m_stroke_params.change_value().miter_limit(limit);
    }
}

void GraphicsContext::setPlatformAlpha(float opacity)
{
    FUID_TRACE;
    if (paintingDisabled())
        return;

    if (m_data->is_qt()) {
        QPainter* p = m_data->p();
        p->setOpacity(opacity);
    } else {
        if (m_state.fillPattern || m_state.fillGradient) {
            m_data->fastuidraw_state().m_fill_brush.change_value().color(1.0f, 1.0f, 1.0f, opacity);
        } else {
            m_data->fastuidraw_state().m_fill_brush.change_value().color(FastUIDrawColorValue(fillColor(), opacity));
        }

        if (m_state.strokePattern || m_state.strokeGradient) {
            m_data->fastuidraw_state().m_stroke_brush.change_value().color(1.0f, 1.0f, 1.0f, opacity);
        } else {
            m_data->fastuidraw_state().m_stroke_brush.change_value().color(FastUIDrawColorValue(strokeColor(), opacity));
        }
    }
}

void GraphicsContext::setPlatformCompositeOperation(CompositeOperator op, BlendMode blendMode)
{
    FUID_TRACE;
    if (paintingDisabled())
        return;

    if (m_data->is_qt()) {
        QPainter *p(m_data->p());
        QPainter::CompositionMode cs;

        ASSERT(op == WebCore::CompositeSourceOver || blendMode == WebCore::BlendModeNormal);

        if (op == WebCore::CompositeSourceOver)
            cs = toQtCompositionMode(blendMode);
        else
            cs = toQtCompositionMode(op);

        p->setCompositionMode(cs);
    } else {
        fastuidraw::Painter::composite_mode_t cs(toFastUIDrawCompositeMode(op));
        fastuidraw::Painter::blend_w3c_mode_t bs(toFastUIDrawBlendMode(blendMode));

        m_data->fastuidraw()->composite_shader(cs);
        m_data->fastuidraw()->blend_shader(bs);
    }
}

void GraphicsContext::canvasClip(const Path& path, WindRule windRule)
{
    FUID_TRACE;
    if (paintingDisabled())
        return;

    if (m_data->is_qt()) {
        QPainterPath clipPath = path.platformPath();
        clipPath.setFillRule(toQtFillRule(windRule));
        m_data->p()->setClipPath(clipPath, Qt::IntersectClip);
    } else {
        m_data->fastuidraw()->clip_in_path(path.FastUIDrawPath(), toFastUIDrawFillRule(windRule));
    }
}

void GraphicsContext::clipOut(const Path& path)
{
    FUID_TRACE;
    if (paintingDisabled())
        return;

    if (m_data->is_qt()) {
        QPainter* p = m_data->p();
        QPainterPath clippedOut = path.platformPath();
        QPainterPath newClip;
        newClip.setFillRule(Qt::OddEvenFill);
        if (p->hasClipping()) {
            newClip.addRect(m_data->clipBoundingRect());
            newClip.addPath(clippedOut);
            p->setClipPath(newClip, Qt::IntersectClip);
        } else {
            QRect windowRect = p->transform().inverted().mapRect(p->window());
            newClip.addRect(windowRect);
            newClip.addPath(clippedOut.intersected(newClip));
            p->setClipPath(newClip);
        }
    } else {
        m_data->fastuidraw()->clip_out_path(path.FastUIDrawPath(),
                                            fastuidraw::Painter::odd_even_fill_rule);
    }
}

void GraphicsContext::translate(float x, float y)
{
    FUID_TRACE;
    if (paintingDisabled())
        return;

    if (m_data->is_qt()) {
        m_data->p()->translate(x, y);
    } else {
        m_data->fastuidraw()->translate(fastuidraw::vec2(x, y));
    }
}

void GraphicsContext::rotate(float radians)
{
    FUID_TRACE;
    if (paintingDisabled())
        return;

    if (m_data->is_qt()) {
        QTransform rotation = QTransform().rotateRadians(radians);
        m_data->p()->setTransform(rotation, true);
    } else {
        m_data->fastuidraw()->rotate(radians);
    }
}

void GraphicsContext::scale(const FloatSize& s)
{
    FUID_TRACE;
    if (paintingDisabled())
        return;

    if (m_data->is_qt()) {
        m_data->p()->scale(s.width(), s.height());
    } else {
        m_data->fastuidraw()->shear(s.width(), s.height());
    }
}

void GraphicsContext::clipOut(const FloatRect& rect)
{
    FUID_TRACE;
    if (paintingDisabled())
        return;

    if (m_data->is_qt()) {
        QPainter* p = m_data->p();
        QPainterPath newClip;
        newClip.setFillRule(Qt::OddEvenFill);
        if (p->hasClipping()) {
            newClip.addRect(m_data->clipBoundingRect());
            newClip.addRect(QRectF(rect));
            p->setClipPath(newClip, Qt::IntersectClip);
        } else {
            QRectF clipOutRect(rect);
            QRect window = p->transform().inverted().mapRect(p->window());
            clipOutRect &= window;
            newClip.addRect(window);
            newClip.addRect(clipOutRect);
            p->setClipPath(newClip);
        }
    } else {
        m_data->fastuidraw()->clip_out_rect(rectFromFloatRect(rect));
    }
}

void GraphicsContext::concatCTM(const AffineTransform& transform)
{
    FUID_TRACE;
    if (paintingDisabled())
        return;

    if (m_data->is_qt()) {
        m_data->p()->setWorldTransform(transform, true);
    } else {
        fastuidraw::float3x3 matrix;
        computeToFastUIDrawMatrix(transform, &matrix);
        m_data->fastuidraw()->concat(matrix);
    }
}

void GraphicsContext::setCTM(const AffineTransform& transform)
{
    FUID_TRACE;
    if (paintingDisabled())
        return;

    if (m_data->is_qt()) {
        m_data->p()->setWorldTransform(transform);
    } else {
        m_data->setFastUIDrawCTM(transform);
    }
}

#if ENABLE(3D_TRANSFORMS)
TransformationMatrix GraphicsContext::get3DTransform() const
{
    FUID_TRACE;
    if (paintingDisabled())
        return TransformationMatrix();

    if (m_data->is_qt()) {
        return platformContext()->qt().worldTransform();
    } else {
        TransformationMatrix return_value;

        computeFromFastUIDrawMatrixT(m_data->computeFastUIDrawCTM(), &return_value);
        return return_value;
    }
}

void GraphicsContext::concat3DTransform(const TransformationMatrix& transform)
{
    FUID_TRACE;
    if (paintingDisabled())
        return;

    if (m_data->is_qt()) {
        m_data->p()->setWorldTransform(transform, true);
    } else {
        fastuidraw::float3x3 matrix;
        computeToFastUIDrawMatrix(transform, &matrix);
        m_data->fastuidraw()->concat(matrix);
    }
}

void GraphicsContext::set3DTransform(const TransformationMatrix& transform)
{
    FUID_TRACE;
    if (paintingDisabled())
        return;

    if (m_data->is_qt()) {
        m_data->p()->setWorldTransform(transform, false);
    } else {
        m_data->setFastUIDrawCTM(transform);
    }
}
#endif

void GraphicsContext::setURLForRect(const URL& url, const IntRect& rect)
{
    FUID_TRACE;
#if QT_VERSION >= QT_VERSION_CHECK(5, 6, 0)
    if (paintingDisabled())
        return;

    if (m_data->is_qt()) {
        QPainter* p = m_data->p();
        if (p->paintEngine()->type() == QPaintEngine::Pdf)
            static_cast<QPdfEngine *>(p->paintEngine())->drawHyperlink(p->worldTransform().mapRect(rect), url);
    }
#else
    UNUSED_PARAM(url);
    UNUSED_PARAM(rect);
#endif
}

void GraphicsContext::setPlatformStrokeColor(const Color& color)
{
    FUID_TRACE;
    /* Qt notes:
     *   - QPainter::pen/setPen define how to stroke paths; a pen contains a brush as well.
     *   - QPainter::brush/setBrush define how to fill
     */
    if (paintingDisabled() || !color.isValid())
        return;

    if (m_data->is_qt()) {
        QPainter* p = m_data->p();
        QPen newPen(p->pen());
        m_data->solidColor.setColor(color);
        newPen.setBrush(m_data->solidColor);
        p->setPen(newPen);
    } else {
        m_data->fastuidraw_state().m_stroke_brush.change_value()
          .reset()
          .color(FastUIDrawColorValue(color, alpha()));
    }
}

void GraphicsContext::setPlatformStrokeStyle(StrokeStyle strokeStyle)
{
    FUID_TRACE;
    if (paintingDisabled())
        return;

    if (m_data->is_qt()) {
        QPainter* p = m_data->p();
        QPen newPen(p->pen());
        newPen.setStyle(toQPenStyle(strokeStyle));
        p->setPen(newPen);
    } else {
        unimplementedFastUIDraw();
    }
}

void GraphicsContext::setPlatformStrokeThickness(float thickness)
{
    FUID_TRACE;
    if (paintingDisabled())
        return;

    if (m_data->is_qt()) {
        QPainter* p = m_data->p();
        QPen newPen(p->pen());
        newPen.setWidthF(thickness);
        p->setPen(newPen);
    } else {
        m_data->fastuidraw_state().m_stroke_params.change_value().width(thickness);
    }
}

void GraphicsContext::setPlatformFillColor(const Color& color)
{
    FUID_TRACE;
    /* Qt notes:
     *   - QPainter::pen/setPen define how to stroke paths; a pen contains a brush as well.
     *   - QPainter::brush/setBrush define how to fill
     */
    if (paintingDisabled() || !color.isValid())
        return;

    if (m_data->is_qt()) {
        m_data->solidColor.setColor(color);
        m_data->p()->setBrush(m_data->solidColor);
    } else {
        m_data->fastuidraw_state().m_fill_brush.change_value()
          .reset()
          .color(FastUIDrawColorValue(color, alpha()));
    }
}

void GraphicsContext::setPlatformStrokePatternGradient(const RefPtr<Pattern> &pattern, const RefPtr<Gradient> &gr)
{
    FUID_TRACE;
    if (m_data->is_qt()) {
    } else {
      setPatternGradientOfFastUIDrawBrush(pattern, gr,
                                          m_data->fastuidraw_state().m_stroke_brush.change_value(),
                                          alpha());
    }
}

void GraphicsContext::setPlatformFillPatternGradient(const RefPtr<Pattern> &pattern, const RefPtr<Gradient> &gr)
{
    FUID_TRACE;
    if (m_data->is_qt()) {
    } else {
        setPatternGradientOfFastUIDrawBrush(pattern, gr,
                                            m_data->fastuidraw_state().m_fill_brush.change_value(),
                                            alpha());
    }
}

void GraphicsContext::setPlatformShouldAntialias(bool enable)
{
    FUID_TRACE;
    if (paintingDisabled())
        return;
    if (m_data->is_qt()) {
        m_data->p()->setRenderHint(QPainter::Antialiasing, enable);
    } else {
        m_data->fastuidraw_state().m_fill_aa = (m_data->fastuidraw_options().m_allow_fill_aa && enable) ?
          fastuidraw::Painter::shader_anti_alias_auto :
          fastuidraw::Painter::shader_anti_alias_none;

        m_data->fastuidraw_state().m_stroke_aa = (m_data->fastuidraw_options().m_allow_stroke_aa && enable) ?
          fastuidraw::Painter::shader_anti_alias_auto :
          fastuidraw::Painter::shader_anti_alias_none;
    }
}

#if OS(WINDOWS)

HDC GraphicsContext::getWindowsContext(const IntRect& dstRect, bool supportAlphaBlend, bool mayCreateBitmap)
{
    FUID_TRACE;
    // painting through native HDC is only supported for plugin, where mayCreateBitmap is always true
    Q_ASSERT(mayCreateBitmap);

    if (dstRect.isEmpty())
        return 0;

    // Create a bitmap DC in which to draw.
    BITMAPINFO bitmapInfo;
    bitmapInfo.bmiHeader.biSize          = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth         = dstRect.width();
    bitmapInfo.bmiHeader.biHeight        = dstRect.height();
    bitmapInfo.bmiHeader.biPlanes        = 1;
    bitmapInfo.bmiHeader.biBitCount      = 32;
    bitmapInfo.bmiHeader.biCompression   = BI_RGB;
    bitmapInfo.bmiHeader.biSizeImage     = 0;
    bitmapInfo.bmiHeader.biXPelsPerMeter = 0;
    bitmapInfo.bmiHeader.biYPelsPerMeter = 0;
    bitmapInfo.bmiHeader.biClrUsed       = 0;
    bitmapInfo.bmiHeader.biClrImportant  = 0;

    void* pixels = 0;
    HBITMAP bitmap = ::CreateDIBSection(0, &bitmapInfo, DIB_RGB_COLORS, &pixels, 0, 0);
    if (!bitmap)
        return 0;

    HDC displayDC = ::GetDC(0);
    HDC bitmapDC = ::CreateCompatibleDC(displayDC);
    ::ReleaseDC(0, displayDC);

    ::SelectObject(bitmapDC, bitmap);

    // Fill our buffer with clear if we're going to alpha blend.
    if (supportAlphaBlend) {
        BITMAP bmpInfo;
        GetObject(bitmap, sizeof(bmpInfo), &bmpInfo);
        int bufferSize = bmpInfo.bmWidthBytes * bmpInfo.bmHeight;
        memset(bmpInfo.bmBits, 0, bufferSize);
    }

#if !OS(WINCE)
    // Make sure we can do world transforms.
    SetGraphicsMode(bitmapDC, GM_ADVANCED);

    // Apply a translation to our context so that the drawing done will be at (0,0) of the bitmap.
    XFORM xform;
    xform.eM11 = 1.0f;
    xform.eM12 = 0.0f;
    xform.eM21 = 0.0f;
    xform.eM22 = 1.0f;
    xform.eDx = -dstRect.x();
    xform.eDy = -dstRect.y();
    ::SetWorldTransform(bitmapDC, &xform);
#endif

    return bitmapDC;
}

void GraphicsContext::releaseWindowsContext(HDC hdc, const IntRect& dstRect, bool supportAlphaBlend, bool mayCreateBitmap)
{
    FUID_TRACE;
    // painting through native HDC is only supported for plugin, where mayCreateBitmap is always true
    Q_ASSERT(mayCreateBitmap);

    if (hdc) {

        if (!dstRect.isEmpty()) {

            HBITMAP bitmap = static_cast<HBITMAP>(GetCurrentObject(hdc, OBJ_BITMAP));
            BITMAP info;
            GetObject(bitmap, sizeof(info), &info);
            ASSERT(info.bmBitsPixel == 32);

            QPixmap pixmap = qt_pixmapFromWinHBITMAP(bitmap, supportAlphaBlend ? HBitmapPremultipliedAlpha : HBitmapNoAlpha);
            m_data->p()->drawPixmap(dstRect, pixmap);

            ::DeleteObject(bitmap);
        }

        ::DeleteDC(hdc);
    }
}
#endif

void GraphicsContext::setPlatformImageInterpolationQuality(InterpolationQuality quality)
{
    FUID_TRACE;
    m_data->imageInterpolationQuality = quality;
    if (m_data->is_qt()) {
        // FIXME
        switch (quality) {
        case InterpolationNone:
        case InterpolationLow:
            // use nearest-neigbor
            m_data->p()->setRenderHint(QPainter::SmoothPixmapTransform, false);
            break;

        case InterpolationMedium:
        case InterpolationHigh:
            // use the filter
            m_data->p()->setRenderHint(QPainter::SmoothPixmapTransform, true);
            break;

        case InterpolationDefault:
        default:
            m_data->p()->setRenderHint(QPainter::SmoothPixmapTransform, m_data->initialSmoothPixmapTransformHint);
            break;
        }
    } else {
        /* FastUIDraw: we delay setting the interpolation value in
         * the fill or stroke brush until we get the image that is
         * used. 
         */
    }
}

void GraphicsContext::takeOwnershipOfPlatformContext()
{
    FUID_TRACE;
    m_data->takeOwnershipOfPlatformContext();
}

bool GraphicsContext::isAcceleratedContext() const
{
    FUID_TRACE;
    if (!platformContext())
        return false;

    if (platformContext()->is_fastuidraw())
        return true;
  
    return platformContext()->qt().paintEngine() &&
        (platformContext()->qt().paintEngine()->type() == QPaintEngine::OpenGL2);
}

void GraphicsContext::strokeText(const fastuidraw::GlyphRun& glyphRun)
{
    FUID_TRACE;
    FASTUIDRAWassert(m_data && m_data->is_fastuidraw());
    unimplementedFastUIDraw();
}

void GraphicsContext::fillText(const fastuidraw::GlyphRun& glyphRun)
{
    FUID_TRACE;
    FASTUIDRAWassert(m_data && m_data->is_fastuidraw());

    fastuidraw::PainterData pd(m_data->fastuidraw_state().m_fill_brush.packed_value());
    m_data->fastuidraw()->draw_glyphs(pd, glyphRun, fastuidraw::GlyphRenderer(fastuidraw::restricted_rays_glyph));
}

void GraphicsContext::drawImage(const fastuidraw::PainterBrush &brush,
                                const FloatRect& dst,
                                CompositeOperator op, BlendMode blendMode)
{
    FUID_TRACE;
    FASTUIDRAWassert(m_data && m_data->is_fastuidraw());
    
    if (hasShadow()) {
        unimplementedFastUIDrawMessage("Shadow");
    }

    CompositeOperator previousOperator = compositeOperation();
    BlendMode previousBlendMode = blendModeOperation();

    setCompositeOperation(op, blendMode);
    m_data->fastuidraw()->fill_rect(fastuidraw::PainterData(&brush),
                                    fastuidraw::Rect()
                                    .min_point(fastuidraw::vec2(dst.x(), dst.y()))
                                    .size(fastuidraw::vec2(dst.width(), dst.height())));
    setCompositeOperation(previousOperator, previousBlendMode);
}

}
// vim: ts=4 sw=4 et
