/*
 * Copyright (C) 2006, 2007, 2008, 2010 Apple Inc. All rights reserved.
 * Copyright (C) 2007 Alp Toker <alp@atoker.com>
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#include "config.h"
#include "Gradient.h"

#include "Color.h"
#include "FloatRect.h"
#include "FastUIDrawResources.h"
#include "FastUIDrawUtil.h"
#include <wtf/HashFunctions.h>
#include <wtf/Hasher.h>

using WTF::pairIntHash;

namespace WebCore {

Gradient::Gradient(const FloatPoint& p0, const FloatPoint& p1)
    : m_radial(false)
    , m_p0(p0)
    , m_p1(p1)
    , m_r0(0)
    , m_r1(0)
    , m_aspectRatio(1)
    , m_stopsSorted(false)
    , m_spreadMethod(SpreadMethodPad)
    , m_cachedHash(0)
{
    platformInit();
}

Gradient::Gradient(const FloatPoint& p0, float r0, const FloatPoint& p1, float r1, float aspectRatio)
    : m_radial(true)
    , m_p0(p0)
    , m_p1(p1)
    , m_r0(r0)
    , m_r1(r1)
    , m_aspectRatio(aspectRatio)
    , m_stopsSorted(false)
    , m_spreadMethod(SpreadMethodPad)
    , m_cachedHash(0)
{
    platformInit();
}

Gradient::~Gradient()
{
    platformDestroy();
}

void Gradient::adjustParametersForTiledDrawing(FloatSize& size, FloatRect& srcRect, const FloatSize& spacing)
{
    if (m_radial)
        return;

    if (srcRect.isEmpty())
        return;

    if (!spacing.isZero())
        return;

    if (m_p0.x() == m_p1.x()) {
        size.setWidth(1);
        srcRect.setWidth(1);
        srcRect.setX(0);
        return;
    }
    if (m_p0.y() != m_p1.y())
        return;

    size.setHeight(1);
    srcRect.setHeight(1);
    srcRect.setY(0);
}

void Gradient::addColorStop(float value, const Color& color)
{
    float r;
    float g;
    float b;
    float a;
    color.getRGBA(r, g, b, a);
    m_stops.append(ColorStop(value, r, g, b, a));

    m_stopsSorted = false;
    platformDestroy();

    invalidateHash();
}

void Gradient::addColorStop(const Gradient::ColorStop& stop)
{
    m_stops.append(stop);

    m_stopsSorted = false;
    platformDestroy();

    invalidateHash();
}

static inline bool compareStops(const Gradient::ColorStop& a, const Gradient::ColorStop& b)
{
    return a.stop < b.stop;
}

void Gradient::sortStopsIfNecessary()
{
    if (m_stopsSorted)
        return;

    m_stopsSorted = true;

    if (!m_stops.size())
        return;

    std::stable_sort(m_stops.begin(), m_stops.end(), compareStops);

    invalidateHash();
}

bool Gradient::hasAlpha() const
{
    for (size_t i = 0; i < m_stops.size(); i++) {
        if (m_stops[i].alpha < 1)
            return true;
    }

    return false;
}

void Gradient::setSpreadMethod(GradientSpreadMethod spreadMethod)
{
    // FIXME: Should it become necessary, allow calls to this method after m_gradient has been set.
    ASSERT(m_gradient == 0);

    if (m_spreadMethod == spreadMethod)
        return;

    m_spreadMethod = spreadMethod;

    invalidateHash();
}

void Gradient::setGradientSpaceTransform(const AffineTransform& gradientSpaceTransformation)
{
    if (m_gradientSpaceTransformation == gradientSpaceTransformation)
        return;

    m_gradientSpaceTransformation = gradientSpaceTransformation;
    setPlatformGradientSpaceTransform(gradientSpaceTransformation);

    invalidateHash();
}

#if !USE(CAIRO)
void Gradient::setPlatformGradientSpaceTransform(const AffineTransform&)
{
}
#endif

unsigned Gradient::hash() const
{
    if (m_cachedHash)
        return m_cachedHash;

    struct {
        AffineTransform gradientSpaceTransformation;
        FloatPoint p0;
        FloatPoint p1;
        float r0;
        float r1;
        float aspectRatio;
        GradientSpreadMethod spreadMethod;
        bool radial;
    } parameters;

    // StringHasher requires that the memory it hashes be a multiple of two in size.
    COMPILE_ASSERT(!(sizeof(parameters) % 2), Gradient_parameters_size_should_be_multiple_of_two);
    COMPILE_ASSERT(!(sizeof(ColorStop) % 2), Color_stop_size_should_be_multiple_of_two);
    
    // Ensure that any padding in the struct is zero-filled, so it will not affect the hash value.
    memset(&parameters, 0, sizeof(parameters));
    
    parameters.gradientSpaceTransformation = m_gradientSpaceTransformation;
    parameters.p0 = m_p0;
    parameters.p1 = m_p1;
    parameters.r0 = m_r0;
    parameters.r1 = m_r1;
    parameters.aspectRatio = m_aspectRatio;
    parameters.spreadMethod = m_spreadMethod;
    parameters.radial = m_radial;

    unsigned parametersHash = StringHasher::hashMemory(&parameters, sizeof(parameters));
    unsigned stopHash = StringHasher::hashMemory(m_stops.data(), m_stops.size() * sizeof(ColorStop));

    m_cachedHash = pairIntHash(parametersHash, stopHash);

    return m_cachedHash;
}

const fastuidraw::reference_counted_ptr<const fastuidraw::ColorStopSequenceOnAtlas>& Gradient::fastuidrawGradient(void) const
{
    if (!m_fastuidraw_cs) {
        fastuidraw::ColorStopSequence sq;
        unsigned int width(256);
        float last_time(-1.0f);

        for(const auto &colorStop: m_stops) {
            fastuidraw::u8vec4 color;
            float t;

            color.x() = static_cast<uint8_t>(255.0f * colorStop.red);
            color.y() = static_cast<uint8_t>(255.0f * colorStop.green);
            color.z() = static_cast<uint8_t>(255.0f * colorStop.blue);
            color.w() = static_cast<uint8_t>(255.0f * colorStop.alpha);
            t = colorStop.stop;

            if (last_time >= colorStop.stop) {
              t = last_time + 1.0f / 256.0f;
              width = fastuidraw::t_max(width, 256u);
            } else {
                float delta(colorStop.stop - last_time);
                float recip(1.0f / delta);
                /* we need atleast one texel between the two times. */
                width = fastuidraw::t_max(width, static_cast<unsigned int>(recip));
            }

            sq.add(fastuidraw::ColorStop(color, t));
            last_time = t;
        }
        if (m_stops.isEmpty()) {
            sq.add(fastuidraw::ColorStop(fastuidraw::u8vec4(0), 0.0f));
            sq.add(fastuidraw::ColorStop(fastuidraw::u8vec4(0), 1.0f));
        }
        
        m_fastuidraw_cs = FASTUIDRAWnew fastuidraw::ColorStopSequenceOnAtlas(sq,
                                                                             FastUIDraw::colorAtlas(),
                                                                             width);
    }

    return m_fastuidraw_cs;
}

static inline enum fastuidraw::PainterBrush::spread_type_t toFastUIDrawGradientSpreadType(enum GradientSpreadMethod spread)
{
    switch(spread) {
    case SpreadMethodPad:
        return fastuidraw::PainterBrush::spread_clamp;
        break;
    case SpreadMethodRepeat:
        return fastuidraw::PainterBrush::spread_repeat;
        break;
    case SpreadMethodReflect:
        return fastuidraw::PainterBrush::spread_mirror_repeat;
        break;
    }
    return fastuidraw::PainterBrush::spread_repeat;
}

void Gradient::readyFastUIDrawBrush(fastuidraw::PainterBrush &brush) const
{
    const fastuidraw::reference_counted_ptr<const fastuidraw::ColorStopSequenceOnAtlas> &cs(fastuidrawGradient());
    fastuidraw::vec2 q0(FastUIDraw::vec2FromFloatPoint(p0())), q1(FastUIDraw::vec2FromFloatPoint(p1()));
    fastuidraw::float3x3 M;
    fastuidraw::float2x2 N;
    fastuidraw::vec2 T;
    Optional<AffineTransform> inverse_gr;

    /* Gradient::gradientSpaceTransform() maps from coordinates
     * of the gradient TO logical coordinates. FastUIDraw's brush
     * maps from logical coordinates to gradient coordiante, so
     * we need the inverse.
     */
    inverse_gr = gradientSpaceTransform().inverse();
    FastUIDraw::computeToFastUIDrawMatrixT(inverse_gr.value(), &M);

    /* AffineTransform is not a full 3x3, it is just
     * a 2x2 matrix and a translate.
     */
    N(0, 0) = M(0, 0);
    N(0, 1) = M(0, 1);
    N(1, 0) = M(1, 0);
    N(1, 1) = M(1, 1);
    T.x() = M(0, 2);
    T.y() = M(1, 2);

    brush.reset();
    if (isRadial()) {
        brush.radial_gradient(cs,
                              q0, startRadius(),
                              q1, endRadius(),
                              toFastUIDrawGradientSpreadType(spreadMethod()));
    } else {
        brush.linear_gradient(cs, q0, q1,
                              toFastUIDrawGradientSpreadType(spreadMethod()));
    }

    brush
      .transformation_matrix(N)
      .transformation_translate(T);
}

} //namespace
