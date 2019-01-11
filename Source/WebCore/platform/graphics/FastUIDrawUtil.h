#ifndef FastUIDrawUtil_h
#define FastUIDrawUtil_h

#include <fastuidraw/text/font_freetype.hpp>
#include <fastuidraw/painter/glyph_sequence.hpp>

class QRawFont;

namespace WebCore {
  namespace FastUIDraw {
    fastuidraw::reference_counted_ptr<const fastuidraw::FontBase>
    select_font(const QRawFont &desc);

    fastuidraw::reference_counted_ptr<const fastuidraw::FontBase>
    install_custom_font(const QRawFont &desc,
                        fastuidraw::reference_counted_ptr<fastuidraw::FreeTypeFace::GeneratorBase> f);
  }
}

#endif
