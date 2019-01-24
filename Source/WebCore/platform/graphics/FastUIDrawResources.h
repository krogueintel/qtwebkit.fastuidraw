#ifndef FastUIDrawResources_h
#define FastUIDrawResources_h

#include <fastuidraw/painter/painter.hpp>
#include <fastuidraw/text/font_database.hpp>

namespace WebCore {
  namespace FastUIDraw {

    /* A GL context must be current when this call is made
     * so that the GL context can be queried to properly
     * configure the (hidden) PainterBackendGL object.
     * Calling this increments a reference counter and
     * only when at entry when the reference counter is zero
     * are objects actually created.
     * \param get_proc function pointer used to fetch GL functions
     * \param get_proc_data opaque data pointer passed to get_proc
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

    const fastuidraw::reference_counted_ptr<fastuidraw::FontDatabase>&
    fontDatabase(void);

    const fastuidraw::reference_counted_ptr<const fastuidraw::Image>&
    checkerboardImage(void);

    const fastuidraw::reference_counted_ptr<const fastuidraw::ColorStopSequenceOnAtlas>&
    threeStopColorStops(void);

    void
    setBrushToNullImage(fastuidraw::PainterBrush &brush);

    fastuidraw::reference_counted_ptr<const fastuidraw::FontBase>
    selectFont(int weight, int slant,
               fastuidraw::c_string style,
               fastuidraw::c_string family,
               fastuidraw::c_string foundry,
               fastuidraw::c_array<const fastuidraw::c_string> langs);

    void
    installCustomFont(int weight, int slant,
                      fastuidraw::c_string style,
                      fastuidraw::c_string family,
                      fastuidraw::c_string foundry,
                      fastuidraw::c_array<const fastuidraw::c_string> langs,
                      fastuidraw::reference_counted_ptr<const fastuidraw::FontBase> font);

    void
    unimplementedFastUIDrawFunc(const char *file, int line, const char *function, unsigned int &count, const char *p);

    void
    warningFastUIDrawFunc(const char *file, int line, const char *function, unsigned int &count, const char *p);
  }
}

#define unimplementedFastUIDraw() do {                                  \
    static unsigned int count = 0;                                      \
    WebCore::FastUIDraw::unimplementedFastUIDrawFunc(__FILE__, __LINE__, __PRETTY_FUNCTION__, count, ""); \
  } while(0)

#define unimplementedFastUIDrawMessage(X) do {                          \
    static unsigned int count = 0;                                      \
    WebCore::FastUIDraw::unimplementedFastUIDrawFunc(__FILE__, __LINE__, __PRETTY_FUNCTION__, count, X); \
  } while(0)

#define warningFastUIDraw(X) do {                                       \
    static unsigned int count = 0;                                      \
    WebCore::FastUIDraw::warningFastUIDrawFunc(__FILE__, __LINE__, __PRETTY_FUNCTION__, count, X); \
  } while(0)

#endif
