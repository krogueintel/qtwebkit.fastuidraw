#include "FastUIDrawResources.h"

namespace {
  class AtlasSet:fastuidraw::noncopyable
  {
  public:
    static
    AtlasSet&
    atlas_set(void)
    {
      static AtlasSet R;
      return R;
    }

    fastuidraw::reference_counted_ptr<fastuidraw::GlyphCache> m_glyph_cache;
    fastuidraw::reference_counted_ptr<fastuidraw::ImageAtlas> m_image_atlas;
    fastuidraw::reference_counted_ptr<fastuidraw::ColorStopAtlas> m_color_stop_atlas;
    fastuidraw::reference_counted_ptr<fastuidraw::GlyphSelector> m_glyph_selector;
    
  private:
    AtlasSet(void)
    {}
  };
}

void
WebCore::FastUIDrawResources::
setResources(fastuidraw::reference_counted_ptr<fastuidraw::GlyphCache> g,
             fastuidraw::reference_counted_ptr<fastuidraw::ImageAtlas> i,
             fastuidraw::reference_counted_ptr<fastuidraw::ColorStopAtlas> c,
             fastuidraw::reference_counted_ptr<fastuidraw::GlyphSelector> s)
{
  AtlasSet &R(AtlasSet::atlas_set());
  R.m_glyph_cache = g;
  R.m_image_atlas = i;
  R.m_color_stop_atlas = c;
  R.m_glyph_selector = s;
}

const fastuidraw::reference_counted_ptr<fastuidraw::GlyphCache>&
WebCore::FastUIDrawResources::
glyphCache(void)
{
  return AtlasSet::atlas_set().m_glyph_cache;
}

const fastuidraw::reference_counted_ptr<fastuidraw::ImageAtlas>&
WebCore::FastUIDrawResources::
imageAtlas(void)
{
  return AtlasSet::atlas_set().m_image_atlas;
}

const fastuidraw::reference_counted_ptr<fastuidraw::ColorStopAtlas>&
WebCore::FastUIDrawResources::
colorAtlas(void)
{
  return AtlasSet::atlas_set().m_color_stop_atlas;
}

const fastuidraw::reference_counted_ptr<fastuidraw::GlyphSelector>&
WebCore::FastUIDrawResources::
glyphSelector(void)
{
  return AtlasSet::atlas_set().m_glyph_selector;
}
