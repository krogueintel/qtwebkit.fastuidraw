#include "FastUIDrawResources.h"
#include "FastUIDrawPainter.h"

#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <mutex>
#include <sstream>
#include <iostream>
#include <fontconfig/fontconfig.h>
#include <fastuidraw/util/c_array.hpp>
#include <fastuidraw/util/vecN.hpp>
#include <fastuidraw/text/font_database.hpp>
#include <fastuidraw/text/glyph_cache.hpp>
#include <fastuidraw/text/font_freetype.hpp>
#include <fastuidraw/painter/painter.hpp>
#include <fastuidraw/painter/glyph_sequence.hpp>
#include <fastuidraw/gl_backend/gl_binding.hpp>
#include <fastuidraw/gl_backend/painter_backend_gl.hpp>
#include <fastuidraw/gl_backend/ngl_header.hpp>

namespace {

  inline
  fastuidraw::c_string
  make_printable(fastuidraw::c_string c)
  {
    return c ? c : "NULL";
  }

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
    fastuidraw::reference_counted_ptr<fastuidraw::FontDatabase> m_font_database;
    fastuidraw::reference_counted_ptr<fastuidraw::gl::PainterBackendGL> m_backend;
    std::vector<fastuidraw::reference_counted_ptr<fastuidraw::Painter> > m_painters;
    std::mutex m_mutex;
    int m_reference_counter;
  private:
    AtlasSet(void):
      m_reference_counter(0)
    {}
  };

  class FontConfig
  {
  public:
    static
    void
    add_system_fonts(const fastuidraw::reference_counted_ptr<fastuidraw::FontDatabase> &dst);

    static
    fastuidraw::reference_counted_ptr<const fastuidraw::FontBase>
    select_font(int weight, int slant,
                fastuidraw::c_string style,
                fastuidraw::c_string family,
                fastuidraw::c_string foundry,
                fastuidraw::c_array<const fastuidraw::c_string> langs,
                fastuidraw::reference_counted_ptr<fastuidraw::FontDatabase> font_database);

    static
    void
    install_custom_font(int weight, int slant,
                        fastuidraw::c_string style,
                        fastuidraw::c_string family,
                        fastuidraw::c_string foundry,
                        fastuidraw::reference_counted_ptr<const fastuidraw::FontBase> font)
    {
      get().m_custom_fonts[make_custom_font_key(weight, slant, style, family, foundry)] = font;
    }

  private:
    // weight, slant, style, family, foundry
    typedef std::tuple<int, int, std::string, std::string, std::string> CustomFontKey;

    static
    CustomFontKey
    make_custom_font_key(int weight, int slant,
                         fastuidraw::c_string style,
                         fastuidraw::c_string family,
                         fastuidraw::c_string foundry)
    {
      return CustomFontKey(weight, slant,
                           make_printable(style),
                           make_printable(family),
                           make_printable(foundry));
    }

    FontConfig(void);
    ~FontConfig(void);

    static
    std::string
    get_string(FcPattern *pattern, const char *label, std::string default_value = std::string());

    static
    int
    get_int(FcPattern *pattern, const char *label, int default_value = 0);

    static
    bool
    get_bool(FcPattern *pattern, const char *label, bool default_value = false);

    static
    fastuidraw::FontProperties
    get_font_properties(FcPattern *pattern);

    static
    FontConfig&
    get(void)
    {
      static FontConfig R;
      return R;
    }

    FcConfig* m_fc;
    fastuidraw::reference_counted_ptr<fastuidraw::FreeTypeLib> m_lib;
    std::map<CustomFontKey, fastuidraw::reference_counted_ptr<const fastuidraw::FontBase> > m_custom_fonts;
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

  class FreeTypeFontGenerator:public fastuidraw::FontDatabase::FontGeneratorBase
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
  gl_image_atlas = FASTUIDRAWnew fastuidraw::gl::ImageAtlasGL(fastuidraw::gl::ImageAtlasGL::params().delayed(true));
  gl_glyph_atlas = FASTUIDRAWnew fastuidraw::gl::GlyphAtlasGL(fastuidraw::gl::GlyphAtlasGL::params().delayed(true));
  gl_colorstop_atlas = FASTUIDRAWnew fastuidraw::gl::ColorStopAtlasGL(fastuidraw::gl::ColorStopAtlasGL::params().delayed(true));

  fastuidraw::gl::PainterBackendGL::ConfigurationGL painter_params;
  painter_params
    .image_atlas(gl_image_atlas)
    .glyph_atlas(gl_glyph_atlas)
    .colorstop_atlas(gl_colorstop_atlas)
    .configure_from_context(true);

  m_backend = fastuidraw::gl::PainterBackendGL::create(painter_params);
  m_glyph_cache = FASTUIDRAWnew fastuidraw::GlyphCache(gl_glyph_atlas);
  m_font_database = FASTUIDRAWnew fastuidraw::FontDatabase();
  
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

  /* Populate m_font_database using FontConfig. */
  FontConfig::add_system_fonts(m_font_database);
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
      m_font_database.clear();
      m_glyph_cache.clear();
      m_backend.clear();
      m_painters.clear();
      m_reference_counter = 0;
    }
  else
    {
      --m_reference_counter;
    }
}

/////////////////////////////
// FontConfig methods
FontConfig::
FontConfig(void)
{
  m_fc = FcInitLoadConfigAndFonts();
  m_lib = FASTUIDRAWnew fastuidraw::FreeTypeLib();
}

FontConfig::
~FontConfig(void)
{
  FcConfigDestroy(m_fc);
}

std::string
FontConfig::
get_string(FcPattern *pattern, const char *label, std::string default_value)
{
  FcChar8 *value(nullptr);
  if (FcPatternGetString(pattern, label, 0, &value) == FcResultMatch)
    {
      return std::string((const char*)value);
    }
  else
    {
      return default_value;
    }
}

int
FontConfig::
get_int(FcPattern *pattern, const char *label, int default_value)
{
  int value(0);
  if (FcPatternGetInteger(pattern, label, 0, &value) == FcResultMatch)
    {
      return value;
    }
  else
    {
      return default_value;
    }
}

bool
FontConfig::
get_bool(FcPattern *pattern, const char *label, bool default_value)
{
  FcBool value(0);
  if (FcPatternGetBool(pattern, label, 0, &value) == FcResultMatch)
    {
      return value;
    }
  else
    {
      return default_value;
    }
}

fastuidraw::FontProperties
FontConfig::
get_font_properties(FcPattern *pattern)
{
  return fastuidraw::FontProperties()
    .style(get_string(pattern, FC_STYLE).c_str())
    .family(get_string(pattern, FC_FAMILY).c_str())
    .foundry(get_string(pattern, FC_FOUNDRY).c_str())
    .source_label(get_string(pattern, FC_FILE).c_str(),
                  get_int(pattern, FC_INDEX))
    .bold(get_int(pattern, FC_WEIGHT) >= FC_WEIGHT_BOLD)
    .italic(get_int(pattern, FC_SLANT) >= FC_SLANT_ITALIC);
}

void
FontConfig::
add_system_fonts(const fastuidraw::reference_counted_ptr<fastuidraw::FontDatabase> &font_database)
{
  FASTUIDRAWassert(font_database);

  FcConfig *config(get().m_fc);
  fastuidraw::reference_counted_ptr<fastuidraw::FreeTypeLib> lib(get().m_lib);
  FcObjectSet *object_set;
  FcFontSet *font_set;
  FcPattern* pattern;
  std::map<std::string, fastuidraw::reference_counted_ptr<DataBufferLoader> > buffer_loaders;

  object_set = FcObjectSetBuild(FC_FOUNDRY, FC_FAMILY, FC_STYLE, FC_WEIGHT,
                                FC_SLANT, FC_SCALABLE, FC_FILE, FC_INDEX,
                                FC_LANG, nullptr);
  pattern = FcPatternCreate();
  FcPatternAddBool(pattern, FC_SCALABLE, FcTrue);
  font_set = FcFontList(config, pattern, object_set);

  for (int i = 0; i < font_set->nfont; ++i)
    {
      std::string filename;

      filename = FontConfig::get_string(font_set->fonts[i], FC_FILE);
      if (filename != "")
        {
          fastuidraw::reference_counted_ptr<DataBufferLoader> b;
          fastuidraw::reference_counted_ptr<const fastuidraw::FontDatabase::FontGeneratorBase> g;
          std::map<std::string, fastuidraw::reference_counted_ptr<DataBufferLoader> >::const_iterator iter;
          int face_index;
          enum fastuidraw::return_code R;
          fastuidraw::FontProperties props(FontConfig::get_font_properties(font_set->fonts[i]));

          iter = buffer_loaders.find(filename);
          if (iter == buffer_loaders.end())
            {
              b = FASTUIDRAWnew DataBufferLoader(filename);
              buffer_loaders[filename] = b;
            }
          else
            {
              b = iter->second;
            }

          face_index = FontConfig::get_int(font_set->fonts[i], FC_INDEX);
          g = FASTUIDRAWnew FreeTypeFontGenerator(b, lib, face_index, props);

          R = font_database->add_font_generator(g);
          if (R != fastuidraw::routine_success)
            {
              std::cout << "FontConfig Warning: unable to add font " << props
                        << " because it was already marked as added\n";
            }
          else
            {
              // std::cout << "FontConfig add font: " << props << "\n";
            }
        }
    }
  FcFontSetDestroy(font_set);
  FcPatternDestroy(pattern);
  FcObjectSetDestroy(object_set);
}

fastuidraw::reference_counted_ptr<const fastuidraw::FontBase>
FontConfig::
select_font(int weight, int slant,
            fastuidraw::c_string style,
            fastuidraw::c_string family,
            fastuidraw::c_string foundry,
            fastuidraw::c_array<const fastuidraw::c_string> langs,
            fastuidraw::reference_counted_ptr<fastuidraw::FontDatabase> font_database)
{ 
  FASTUIDRAWassert(font_database);

  CustomFontKey K(make_custom_font_key(weight, slant, style, family, foundry));
  std::map<CustomFontKey, fastuidraw::reference_counted_ptr<const fastuidraw::FontBase> >::const_iterator iter;
  iter = get().m_custom_fonts.find(K);
  if (iter != get().m_custom_fonts.end())
    {
      return iter->second;
    }

  FcConfig *config(get().m_fc);
  fastuidraw::reference_counted_ptr<fastuidraw::FreeTypeLib> lib(get().m_lib);
  FcPattern* pattern;
  FcLangSet* lang_set(nullptr);
  fastuidraw::reference_counted_ptr<const fastuidraw::FontBase> return_value;

  pattern = FcPatternCreate();
  if (weight >= 0)
    {
      FcPatternAddInteger(pattern, FC_WEIGHT, weight);
    }

  if (slant >= 0)
    {
      FcPatternAddInteger(pattern, FC_SLANT, slant);
    }

  if (style)
    {
      FcPatternAddString(pattern, FC_STYLE, (const FcChar8*)style);
    }

  if (family)
    {
      FcPatternAddString(pattern, FC_FAMILY, (const FcChar8*)family);
    }

  if (foundry)
    {
      FcPatternAddString(pattern, FC_FOUNDRY, (const FcChar8*)foundry);
    }
  FcPatternAddBool(pattern, FC_SCALABLE, FcTrue);

  for (fastuidraw::c_string language : langs)
    {
      if (language)
        {
          if (!lang_set)
            {
              lang_set = FcLangSetCreate();
            }
          FcLangSetAdd(lang_set, (const FcChar8*)language);
        }
    }

  if (lang_set)
    {
      FcPatternAddLangSet(pattern, FC_LANG, lang_set);
    }

  FcConfigSubstitute(config, pattern, FcMatchPattern);
  FcDefaultSubstitute(pattern);

  FcResult r;
  FcPattern *font_pattern = FcFontMatch(config, pattern, &r);
  if (font_pattern)
    {
      FcChar8* filename(nullptr);
      if (FcPatternGetString(font_pattern, FC_FILE, 0, &filename) == FcResultMatch)
        {
          int face_index;
          
          face_index = get_int(font_pattern, FC_INDEX);
          return_value = font_database->fetch_font((fastuidraw::c_string)filename, face_index);
          if (return_value)
            {
              return return_value;
            }
        }
      FcPatternDestroy(font_pattern);
    }
  FcPatternDestroy(pattern);
  if (lang_set)
    {
      FcLangSetDestroy(lang_set);
    }
  return return_value;
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

const fastuidraw::reference_counted_ptr<fastuidraw::FontDatabase>&
WebCore::FastUIDraw::
fontDatabase(void)
{
  std::lock_guard<std::mutex> M(AtlasSet::atlas_set().m_mutex);
  return AtlasSet::atlas_set().m_font_database;
}

fastuidraw::reference_counted_ptr<const fastuidraw::FontBase>
WebCore::FastUIDraw::
selectFont(int weight, int slant,
           fastuidraw::c_string style,
           fastuidraw::c_string family,
           fastuidraw::c_string foundry,
           fastuidraw::c_array<const fastuidraw::c_string> langs)
{
  return FontConfig::select_font(weight, slant, style, family, foundry, langs, fontDatabase());
}

void
WebCore::FastUIDraw::
installCustomFont(int weight, int slant,
                  fastuidraw::c_string style,
                  fastuidraw::c_string family,
                  fastuidraw::c_string foundry,
                  fastuidraw::reference_counted_ptr<const fastuidraw::FontBase> font)
{
  FontConfig::install_custom_font(weight, slant, style, family, foundry, font);
}

void
WebCore::FastUIDraw::
unimplementedFastUIDrawFunc(const char *file, int line, const char *function, unsigned int &count, const char *p)
{
  if (count < 1)
    {
      std::cerr << "[" << file << ", " << line << ": " << function << p << "] unimplemented \n";
    }
  ++count;
}

//////////////////////////////////////////////
// WebCore::FastUIDraw::PainterHolder methods
WebCore::FastUIDraw::PainterHolder::
PainterHolder(void)
{
  AtlasSet &S(AtlasSet::atlas_set());
  std::lock_guard<std::mutex> M(S.m_mutex);

  if (S.m_painters.empty())
    {
      fastuidraw::reference_counted_ptr<fastuidraw::gl::PainterBackendGL> b;

      b = S.m_backend;
      FASTUIDRAWassert(b);
      m_painter = FASTUIDRAWnew fastuidraw::Painter(b->create_shared());
    }
  else
    {
      m_painter = S.m_painters.back();
      S.m_painters.pop_back();
    }
}

WebCore::FastUIDraw::PainterHolder::
~PainterHolder()
{
  AtlasSet &S(AtlasSet::atlas_set());
  std::lock_guard<std::mutex> M(S.m_mutex);

  if (S.m_backend &&
      S.m_backend->painter_shader_registrar() == m_painter->painter_shader_registrar())
    {
      S.m_painters.push_back(m_painter);
    }
}
