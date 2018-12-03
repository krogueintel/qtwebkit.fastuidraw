#include "FastUIDrawResources.h"
#include <string>
#include <iostream>
#include <algorithm>
#include <mutex>
#include <sstream>
#include <dirent.h>
#include <fastuidraw/util/c_array.hpp>
#include <fastuidraw/util/vecN.hpp>
#include <fastuidraw/text/glyph_selector.hpp>
#include <fastuidraw/text/glyph_cache.hpp>
#include <fastuidraw/text/font_freetype.hpp>
#include <fastuidraw/painter/painter.hpp>
#include <fastuidraw/painter/glyph_sequence.hpp>
#include <fastuidraw/gl_backend/gl_binding.hpp>
#include <fastuidraw/gl_backend/painter_backend_gl.hpp>
#include <fastuidraw/gl_backend/ngl_header.hpp>

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

    void
    initialize_resources(void *get_proc_data,
                         void* (*get_proc)(void*, fastuidraw::c_string function_name));

    void
    clear_resources(void);

    fastuidraw::reference_counted_ptr<fastuidraw::GlyphCache> m_glyph_cache;
    fastuidraw::reference_counted_ptr<fastuidraw::GlyphSelector> m_glyph_selector;
    fastuidraw::reference_counted_ptr<fastuidraw::gl::PainterBackendGL> m_backend;
    std::mutex m_mutex;
    int m_reference_counter;
    
  private:
    AtlasSet(void):
      m_reference_counter(0)
    {}

  };

  /* The purpose of the DataBufferHolder is to -DELAY-
   * the loading of data until the first time the data
   * is requested.
   */
  class DataBufferLoader:public fastuidraw::reference_counted<DataBufferLoader>::default_base
  {
  public:
    explicit
    DataBufferLoader(const std::string &pfilename):
      m_filename(pfilename)
    {}

    fastuidraw::reference_counted_ptr<fastuidraw::DataBufferBase>
    buffer(void)
    {
      fastuidraw::reference_counted_ptr<fastuidraw::DataBufferBase> R;

      m_mutex.lock();
      if (!m_buffer)
        {
          m_buffer = FASTUIDRAWnew fastuidraw::DataBuffer(m_filename.c_str());
        }
      R = m_buffer;
      m_mutex.unlock();

      return R;
    }

  private:
    std::string m_filename;
    std::mutex m_mutex;
    fastuidraw::reference_counted_ptr<fastuidraw::DataBufferBase> m_buffer;
  };

  class FreeTypeFontGenerator:public fastuidraw::GlyphSelector::FontGeneratorBase
  {
  public:
    FreeTypeFontGenerator(fastuidraw::reference_counted_ptr<DataBufferLoader> buffer,
                          fastuidraw::reference_counted_ptr<fastuidraw::FreeTypeLib> lib,
                          int face_index,
                          const fastuidraw::FontProperties &props):
      m_buffer(buffer),
      m_lib(lib),
      m_face_index(face_index),
      m_props(props)
    {}

    virtual
    fastuidraw::reference_counted_ptr<const fastuidraw::FontBase>
    generate_font(void) const
    {
      fastuidraw::reference_counted_ptr<fastuidraw::FreeTypeFace::GeneratorBase> h;
      fastuidraw::reference_counted_ptr<fastuidraw::DataBufferBase> buffer;
      fastuidraw::reference_counted_ptr<const fastuidraw::FontBase> font;
      buffer = m_buffer->buffer();
      h = FASTUIDRAWnew fastuidraw::FreeTypeFace::GeneratorMemory(buffer, m_face_index);
      font = FASTUIDRAWnew fastuidraw::FontFreeType(h, m_props, m_lib);
      return font;
    }

    virtual
    const fastuidraw::FontProperties&
    font_properties(void) const
    {
      return m_props;
    }

  private:
    fastuidraw::reference_counted_ptr<DataBufferLoader> m_buffer;
    fastuidraw::reference_counted_ptr<fastuidraw::FreeTypeLib> m_lib;
    int m_face_index;
    fastuidraw::FontProperties m_props;
  };

  inline
  std::ostream&
  operator<<(std::ostream &str, const fastuidraw::FontProperties &obj)
  {
    str << obj.source_label() << "(" << obj.foundry()
        << ", " << obj.family() << ", " << obj.style()
        << ", " << obj.italic() << ", " << obj.bold() << ")";
    return str;
  }

  void
  add_fonts_from_file(const std::string &filename,
                      fastuidraw::reference_counted_ptr<fastuidraw::FreeTypeLib> lib,
                      fastuidraw::reference_counted_ptr<fastuidraw::GlyphSelector> glyph_selector)
  {
    FT_Error error_code;
    FT_Face face(nullptr);

    lib->lock();
    error_code = FT_New_Face(lib->lib(), filename.c_str(), 0, &face);
    lib->unlock();

    if (error_code == 0 && face != nullptr && (face->face_flags & FT_FACE_FLAG_SCALABLE) != 0)
      {
        fastuidraw::reference_counted_ptr<DataBufferLoader> buffer_loader;

        buffer_loader = FASTUIDRAWnew DataBufferLoader(filename);
        for(unsigned int i = 0, endi = face->num_faces; i < endi; ++i)
          {
            fastuidraw::reference_counted_ptr<fastuidraw::GlyphSelector::FontGeneratorBase> h;
            std::ostringstream source_label;
            fastuidraw::FontProperties props;
            if (i != 0)
              {
                lib->lock();
                FT_Done_Face(face);
                FT_New_Face(lib->lib(), filename.c_str(), i, &face);
                lib->unlock();
              }
            fastuidraw::FontFreeType::compute_font_properties_from_face(face, props);
            source_label << filename << ":" << i;
            props.source_label(source_label.str().c_str());

            h = FASTUIDRAWnew FreeTypeFontGenerator(buffer_loader, lib, i, props);
            glyph_selector->add_font_generator(h);
          }
      }

    lib->lock();
    if (face != nullptr)
      {
        FT_Done_Face(face);
      }
    lib->unlock();
  }

  void
  add_fonts_from_path(const std::string &filename,
                      const fastuidraw::reference_counted_ptr<fastuidraw::FreeTypeLib> &lib,
                      const fastuidraw::reference_counted_ptr<fastuidraw::GlyphSelector> &glyph_selector)
  {
    DIR *dir;
    struct dirent *entry;

    dir = opendir(filename.c_str());
    if (!dir)
      {
        add_fonts_from_file(filename, lib, glyph_selector);
        return;
      }

    for(entry = readdir(dir); entry != nullptr; entry = readdir(dir))
      {
        std::string file;
        file = entry->d_name;
        if (file != ".." && file != ".")
          {
            add_fonts_from_path(filename + "/" + file, lib, glyph_selector);
          }
      }
    closedir(dir);
  }

  void
  add_fonts_from_path(const std::string &filename,
                      const fastuidraw::reference_counted_ptr<fastuidraw::GlyphSelector> &glyph_selector)
  {
    fastuidraw::reference_counted_ptr<fastuidraw::FreeTypeLib> lib;

    lib = FASTUIDRAWnew fastuidraw::FreeTypeLib();
    add_fonts_from_path(filename, lib, glyph_selector);
  }
}

///////////////////////////////////////
// AtlasSet methods
void
AtlasSet::
initialize_resources(void *get_proc_data,
                     void* (*get_proc)(void*, fastuidraw::c_string function_name))
{
  std::lock_guard<std::mutex> M(m_mutex);

  ++m_reference_counter;
  if (m_reference_counter > 1)
    return;

  fastuidraw::reference_counted_ptr<fastuidraw::gl::GlyphAtlasGL> gl_glyph_atlas;
  fastuidraw::reference_counted_ptr<fastuidraw::gl::ImageAtlasGL> gl_image_atlas;
  fastuidraw::reference_counted_ptr<fastuidraw::gl::ColorStopAtlasGL> gl_colorstop_atlas;

  fastuidraw::gl_binding::get_proc_function(get_proc_data, get_proc, true);
  gl_image_atlas = FASTUIDRAWnew fastuidraw::gl::ImageAtlasGL(fastuidraw::gl::ImageAtlasGL::params());
  gl_glyph_atlas = FASTUIDRAWnew fastuidraw::gl::GlyphAtlasGL(fastuidraw::gl::GlyphAtlasGL::params());
  gl_colorstop_atlas = FASTUIDRAWnew fastuidraw::gl::ColorStopAtlasGL(fastuidraw::gl::ColorStopAtlasGL::params());

  fastuidraw::gl::PainterBackendGL::ConfigurationGL painter_params;
  painter_params
    .image_atlas(gl_image_atlas)
    .glyph_atlas(gl_glyph_atlas)
    .colorstop_atlas(gl_colorstop_atlas)
    .configure_from_context(true);

  m_backend = fastuidraw::gl::PainterBackendGL::create(painter_params);
  m_glyph_cache = FASTUIDRAWnew fastuidraw::GlyphCache(gl_glyph_atlas);
  m_glyph_selector = FASTUIDRAWnew fastuidraw::GlyphSelector();
  
  if (!m_backend->program(fastuidraw::gl::PainterBackendGL::program_all)->link_success())
    {
      FASTUIDRAWassert(!"fastuidraw::PainterBackendGL::program_all failed link");
    }
    
  if (!m_backend->program(fastuidraw::gl::PainterBackendGL::program_without_discard)->link_success())
    {
      FASTUIDRAWassert(!"fastuidraw::PainterBackendGL::program_without_discard failed link");
    }

  if (!m_backend->program(fastuidraw::gl::PainterBackendGL::program_with_discard)->link_success())
    {
      FASTUIDRAWassert(!"fastuidraw::PainterBackendGL::program_with_discard failed link");
    }

  /* Populate m_glyph_selector, this path is just from Ubuntu 18.04 and the right thing
   * would be to use something like FontConfig to collect all the fonts.
   */
  add_fonts_from_path("/usr/share/fonts", m_glyph_selector);
}

void
AtlasSet::
clear_resources(void)
{
  std::lock_guard<std::mutex> M(m_mutex);
  if (m_reference_counter <= 1)
    {
      std::cout << "Clear resources\n"
                << "\tGL_VERSION=" << fastuidraw_glGetString(GL_VERSION)
                << "\tGL_RENDERER=" << fastuidraw_glGetString(GL_RENDERER)
                << "\n";
      m_glyph_selector.clear();
      m_glyph_cache.clear();
      m_backend.clear();
      m_reference_counter = 0;
    }
  else
    {
      --m_reference_counter;
    }
}

///////////////////////////////////////
// WebCore::FastUIDrawResources methods
void
WebCore::FastUIDraw::
initializeResources(void *get_proc_data,
                    void* (*get_proc)(void*, fastuidraw::c_string function_name))
{
  AtlasSet::atlas_set().initialize_resources(get_proc_data, get_proc);
}

void
WebCore::FastUIDraw::
clearResources(void)   
{
  AtlasSet::atlas_set().clear_resources();
}

const fastuidraw::reference_counted_ptr<fastuidraw::GlyphCache>&
WebCore::FastUIDraw::
glyphCache(void)
{
  std::lock_guard<std::mutex> M(AtlasSet::atlas_set().m_mutex);
  return AtlasSet::atlas_set().m_glyph_cache;
}

const fastuidraw::reference_counted_ptr<fastuidraw::ImageAtlas>&
WebCore::FastUIDraw::
imageAtlas(void)
{
  std::lock_guard<std::mutex> M(AtlasSet::atlas_set().m_mutex);
  return AtlasSet::atlas_set().m_backend->image_atlas();
}

const fastuidraw::reference_counted_ptr<fastuidraw::ColorStopAtlas>&
WebCore::FastUIDraw::
colorAtlas(void)
{
  std::lock_guard<std::mutex> M(AtlasSet::atlas_set().m_mutex);
  return AtlasSet::atlas_set().m_backend->colorstop_atlas();
}

const fastuidraw::reference_counted_ptr<fastuidraw::GlyphSelector>&
WebCore::FastUIDraw::
glyphSelector(void)
{
  std::lock_guard<std::mutex> M(AtlasSet::atlas_set().m_mutex);
  return AtlasSet::atlas_set().m_glyph_selector;
}

fastuidraw::reference_counted_ptr<fastuidraw::Painter>
WebCore::FastUIDraw::
createPainter(void)
{
  std::lock_guard<std::mutex> M(AtlasSet::atlas_set().m_mutex);

  fastuidraw::reference_counted_ptr<fastuidraw::gl::PainterBackendGL> b;
  fastuidraw::reference_counted_ptr<fastuidraw::Painter> r;

  b = AtlasSet::atlas_set().m_backend;
  if (b)
    {
      r = FASTUIDRAWnew fastuidraw::Painter(b->create_sharing_shaders());
    }
  return r;
}
