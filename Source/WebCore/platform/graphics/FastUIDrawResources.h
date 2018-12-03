#ifndef FastUIDrawResources_h
#define FastUIDrawResources_h

#include <fastuidraw/painter/painter.hpp>
#include <fastuidraw/text/glyph_selector.hpp>

namespace WebCore {
  namespace FastUIDraw {

    /* A GL context must be current when this call is made
     * so that the GL context can be queried to properly
     * configure the (hidden) PainterBackendGL object.
     * Calling this increments a reference counter and
     * only when at entry when the reference counter is zero
     * are objects actually created.
     */
    void
    initializeResources(void *get_proc_data,
                        void* (*get_proc)(void*, fastuidraw::c_string function_name));

    /* A GL context must be current when this call is made
     * so that GL can be called to release the resources.
     * Calling this decrements a reference counter, once the
     * counter reaches zero, then the resources are actually
     * cleared.
     */
    void
    clearResources(void);

    const fastuidraw::reference_counted_ptr<fastuidraw::GlyphCache>&
    glyphCache(void);

    const fastuidraw::reference_counted_ptr<fastuidraw::ImageAtlas>&
    imageAtlas(void);

    const fastuidraw::reference_counted_ptr<fastuidraw::ColorStopAtlas>&
    colorAtlas(void);

    const fastuidraw::reference_counted_ptr<fastuidraw::GlyphSelector>&
    glyphSelector(void);

    fastuidraw::reference_counted_ptr<fastuidraw::Painter>
    createPainter(void);
  }
}

#endif
