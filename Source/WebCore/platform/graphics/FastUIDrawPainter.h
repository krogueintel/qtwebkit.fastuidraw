#ifndef FastUIDrawPainter_h
#define FastUIDrawPainter_h

#include "config.h"
#include <fastuidraw/painter/painter.hpp>
#include <fastuidraw/text/font_database.hpp>

namespace WebCore {
  namespace FastUIDraw {
    /* Rather than creating painters, we have a pool of Painter objects;
     * This object does not actually hold a Painter, rather it has a
     * reference and its dtor returns the Painter to the pool for use.
     */
    class PainterHolder:
      public fastuidraw::reference_counted<PainterHolder>::default_base
    {
    public:
      explicit
      WEBCORE_EXPORT PainterHolder(void);
      WEBCORE_EXPORT ~PainterHolder();
      
      WEBCORE_EXPORT const fastuidraw::reference_counted_ptr<fastuidraw::Painter>&
      painter(void) const
      {
        return m_painter;
      }
    private:
      fastuidraw::reference_counted_ptr<fastuidraw::Painter> m_painter;
      int m_atlas_set_init_count_value;
    };
  }
}

#endif
