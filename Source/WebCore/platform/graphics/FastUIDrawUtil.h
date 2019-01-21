#ifndef FastUIDrawUtil_h
#define FastUIDrawUtil_h

#include <sstream>
#include <QTransform>
#include <fastuidraw/text/font_freetype.hpp>
#include <fastuidraw/painter/glyph_sequence.hpp>
#include <fastuidraw/image.hpp>
#include <fastuidraw/painter/painter.hpp>

#include "FloatRect.h"
#include "FloatPoint.h"
#include "AffineTransform.h"

class QRawFont;
class QImage;
class QPixmap;

namespace WebCore {
  namespace FastUIDraw {
    fastuidraw::reference_counted_ptr<const fastuidraw::FontBase>
    select_font(const QRawFont &desc);

    fastuidraw::reference_counted_ptr<const fastuidraw::FontBase>
    install_custom_font(const QRawFont &desc,
                        fastuidraw::reference_counted_ptr<fastuidraw::FreeTypeFace::GeneratorBase> f);

    fastuidraw::reference_counted_ptr<const fastuidraw::Image>
    create_fastuidraw_image(const QImage &image);

    fastuidraw::reference_counted_ptr<const fastuidraw::Image>
    create_fastuidraw_image(const QPixmap &image);

    class FUIDTrace
    {
    public:
      FUIDTrace(const char *file, int line, const char *function);
      FUIDTrace(const char *file, int line, const char *function, const char *message);
      ~FUIDTrace();

      static void startTracking(void);
      static void endTracking(void);
      static bool trackingActive(void);
    };

    void
    compose_with_pattern(fastuidraw::PainterBrush &brush,
                         const FloatRect& srcRect, const AffineTransform& patternTransform,
                         const FloatPoint& phase, const FloatSize& spacing);

    void
    compose_with_pattern_transformation(fastuidraw::PainterBrush &brush,
                                        const AffineTransform& patternTransform);

    inline
    void
    computeToFastUIDrawMatrix(const QTransform &transform,
                              fastuidraw::float3x3 *dst)
    {
      fastuidraw::float3x3 &matrix(*dst);
      matrix.col_row(0, 0) = transform.m11();
      matrix.col_row(1, 0) = transform.m21();
      matrix.col_row(2, 0) = transform.m31();

      matrix.col_row(0, 1) = transform.m12();
      matrix.col_row(1, 1) = transform.m22();
      matrix.col_row(2, 1) = transform.m32();

      matrix.col_row(0, 2) = transform.m13();
      matrix.col_row(1, 2) = transform.m23();
      matrix.col_row(2, 2) = transform.m33();
    }

    inline
    void
    computeFromFastUIDrawMatrix(const fastuidraw::float3x3 &matrix,
                                QTransform *dst)
    {
      *dst = QTransform(matrix.col_row(0, 0), //m11
                        matrix.col_row(0, 1), //m12
                        matrix.col_row(0, 2), //m13
                        matrix.col_row(1, 0), //m21
                        matrix.col_row(1, 1), //m22
                        matrix.col_row(1, 2), //m23
                        matrix.col_row(2, 0), //m31
                        matrix.col_row(2, 1), //m32
                        matrix.col_row(2, 2)  //m33
                        );
    }

    template<typename T>
    inline
    void
    computeToFastUIDrawMatrixT(const T &transform,
                               fastuidraw::float3x3 *dst)
    {
      QTransform Q = transform;
      computeToFastUIDrawMatrix(Q, dst);
    }

    template<typename T>
    inline
    void
    computeFromFastUIDrawMatrixT(const fastuidraw::float3x3 &matrix,
                                 T *dst)
    {
      QTransform Q;
      computeFromFastUIDrawMatrix(matrix, &Q);
      *dst = Q;
    }

    inline
    fastuidraw::vec2
    vec2FromFloatPoint(const FloatPoint &sz)
    {
      return fastuidraw::vec2(sz.x(), sz.y());
    }

    inline
    fastuidraw::vec2
    vec2FromFloatSize(const FloatSize &sz)
    {
      return fastuidraw::vec2(sz.width(), sz.height());
    }
  }
}

#define FUID_TRACE \
  WebCore::FastUIDraw::FUIDTrace fuid_trace_(__FILE__, __LINE__, __PRETTY_FUNCTION__)

#define FUID_TRACE_D(X) \
  std::string fuid_trace_string_;        \
  { std::ostringstream str; str << X; fuid_trace_string_ = str.str(); } \
  WebCore::FastUIDraw::FUIDTrace fuid_trace_(__FILE__, __LINE__, __PRETTY_FUNCTION__, fuid_trace_string_.c_str())

#endif
