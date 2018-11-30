#ifndef FastUIDrawResources_h
#define FastUIDrawResources_h

#include <fastuidraw/painter/painter.hpp>
#include <fastuidraw/text/glyph_selector.hpp>

namespace WebCore {
  namespace FastUIDrawResources {
    void
    setResources(fastuidraw::reference_counted_ptr<fastuidraw::GlyphCache> g,
                 fastuidraw::reference_counted_ptr<fastuidraw::ImageAtlas> i,
                 fastuidraw::reference_counted_ptr<fastuidraw::ColorStopAtlas> c,
                 fastuidraw::reference_counted_ptr<fastuidraw::GlyphSelector> s);

    const fastuidraw::reference_counted_ptr<fastuidraw::GlyphCache>&
    glyphCache(void);

    const fastuidraw::reference_counted_ptr<fastuidraw::ImageAtlas>&
    imageAtlas(void);

    const fastuidraw::reference_counted_ptr<fastuidraw::ColorStopAtlas>&
    colorAtlas(void);

    const fastuidraw::reference_counted_ptr<fastuidraw::GlyphSelector>&
    glyphSelector(void);
  }
}

#endif
