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
#include <fastuidraw/gl_backend/gl_binding.hpp>
#include <fastuidraw/gl_backend/painter_engine_gl.hpp>
#include <fastuidraw/gl_backend/ngl_header.hpp>
#include <QOpenGLContext>

namespace {

  inline
  fastuidraw::c_string
  make_printable(fastuidraw::c_string c)
  {
    return c ? c : "NULL";
  }

  // weight, slant, style, family, foundry, languages
  typedef std::tuple<int, int, std::string, std::string, std::string, std::vector<std::string> > CustomFontKey;

  CustomFontKey
  make_custom_font_key(int weight, int slant,
                         fastuidraw::c_string style,
                         fastuidraw::c_string family,
                         fastuidraw::c_string foundry,
                         fastuidraw::c_array<const fastuidraw::c_string > langs)
  {
    CustomFontKey K;
    std::get<0>(K) = weight;
    std::get<1>(K) = slant;
    std::get<2>(K) = make_printable(style);
    std::get<3>(K) = make_printable(family);
    std::get<4>(K) = make_printable(foundry);
    
    for (fastuidraw::c_string l : langs)
      {
        std::get<5>(K).push_back(l);
      }
    
    return K;
  }

  static
  std::ostream&
  operator<<(std::ostream &str, const CustomFontKey &K)
  {
    str << "[weight=" << std::get<0>(K)
        << ",slant=" << std::get<1>(K)
        << ",style=" << std::get<2>(K)
        << ", family=" << std::get<3>(K)
        << ", foundry=" << std::get<4>(K)
        << ",langs={";
    for(const std::string &v : std::get<5>(K))
      {
        str << "\"" << v << "\",";
      }
    str << "]";
    return str;
  }

  class FontConfig:public fastuidraw::reference_counted<FontConfig>::concurrent
  {
  public:
    FontConfig();
    ~FontConfig();
    
    void
    add_system_fonts(const fastuidraw::reference_counted_ptr<fastuidraw::FontDatabase> &dst);

    fastuidraw::reference_counted_ptr<const fastuidraw::FontBase>
    select_font(int weight, int slant,
                fastuidraw::c_string style,
                fastuidraw::c_string family,
                fastuidraw::c_string foundry,
                fastuidraw::c_array<const fastuidraw::c_string> langs,
                fastuidraw::reference_counted_ptr<fastuidraw::FontDatabase> font_database);

    void
    install_custom_font(int weight, int slant,
                        fastuidraw::c_string style,
                        fastuidraw::c_string family,
                        fastuidraw::c_string foundry,
                        fastuidraw::c_array<const fastuidraw::c_string > langs,
                        fastuidraw::reference_counted_ptr<const fastuidraw::FontBase> font)
    {
      std::lock_guard<std::mutex> M(m_mutex);
      CustomFontKey K(make_custom_font_key(weight, slant, style, family, foundry, langs));

      //std::cout << "FUID: InstallCustom" << K << "@" << font.get();
      if (m_custom_fonts.find(K) == m_custom_fonts.end())
        {
          //std::cout << ":OK \n";
          m_custom_fonts[K] = font;
        }
      else
        {
          std::cout << "FUID: attemptyed to add font when key"
                    << K << " is already present\n";
        }
    }

  private:
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

    std::mutex m_mutex;
    FcConfig* m_fc;
    fastuidraw::reference_counted_ptr<fastuidraw::FreeTypeLib> m_lib;
    std::map<CustomFontKey, fastuidraw::reference_counted_ptr<const fastuidraw::FontBase> > m_custom_fonts;
  };

  class AtlasSet:fastuidraw::noncopyable
  {
  public:
    static int InitCount;
    
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

    fastuidraw::reference_counted_ptr<fastuidraw::FontDatabase> m_font_database;
    fastuidraw::reference_counted_ptr<const fastuidraw::Image> m_checkerboard_image;
    fastuidraw::reference_counted_ptr<const fastuidraw::ColorStopSequenceOnAtlas> m_three_stops_color_stops;
    fastuidraw::reference_counted_ptr<fastuidraw::gl::PainterEngineGL> m_backend;
    std::vector<fastuidraw::reference_counted_ptr<fastuidraw::Painter> > m_painters;
    fastuidraw::reference_counted_ptr<FontConfig> m_font_config;
    fastuidraw::GlyphRenderer m_glyph_renderer;
    std::mutex m_mutex;
    int m_reference_counter;
  private:
    AtlasSet(void):
      m_font_config(nullptr),
      m_glyph_renderer(fastuidraw::banded_rays_glyph),
      m_reference_counter(0)
    {}
  };

  /* The purpose of the DataBufferHolder is to -DELAY-
   * the loading of data until the first time the data
   * is requested.
   */
  class DataBufferLoader:public fastuidraw::reference_counted<DataBufferLoader>::concurrent
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
int AtlasSet::InitCount = 0;

void
AtlasSet::
initialize_resources(void *get_proc_data,
                     void* (*get_proc)(void*, fastuidraw::c_string function_name))
{
  std::lock_guard<std::mutex> M(m_mutex);

  ++m_reference_counter;
  if (m_reference_counter > 1)
    return;

  fastuidraw::gl_binding::get_proc_function(get_proc_data, get_proc, true);
  fastuidraw::gl::PainterEngineGL::ConfigurationGL painter_params;
  painter_params
    .configure_from_context(true)
    .preferred_blend_type(fastuidraw::PainterBlendShader::dual_src)
    .use_uber_item_shader(true);

  m_backend = fastuidraw::gl::PainterEngineGL::create(painter_params);
  m_font_database = FASTUIDRAWnew fastuidraw::FontDatabase();

  fastuidraw::vecN<fastuidraw::u8vec4, 4> im;
  im[0] = fastuidraw::u8vec4(255, 255, 255, 255);
  im[1] = fastuidraw::u8vec4(0, 0, 0, 255);
  im[2] = fastuidraw::u8vec4(0, 0, 0, 255);
  im[3] = fastuidraw::u8vec4(255, 255, 255, 255);
  fastuidraw::c_array<const fastuidraw::u8vec4> im_ptr(im);
  fastuidraw::c_array<const fastuidraw::c_array<const fastuidraw::u8vec4> > im_aptr(&im_ptr, 1);
  fastuidraw::ImageSourceCArray im_source(fastuidraw::uvec2(2, 2), im_aptr, fastuidraw::Image::rgba_format);

  m_checkerboard_image = m_backend->image_atlas().create(2, 2, im_source);

  fastuidraw::ColorStopSequence cs;
  cs.add(fastuidraw::ColorStop(fastuidraw::u8vec4(255, 0, 0, 255), 0.0f));
  cs.add(fastuidraw::ColorStop(fastuidraw::u8vec4(0, 255, 0, 255), 0.5f));
  cs.add(fastuidraw::ColorStop(fastuidraw::u8vec4(0, 0, 255, 255), 1.0f));
  m_three_stops_color_stops = FASTUIDRAWnew fastuidraw::ColorStopSequenceOnAtlas(cs, m_backend->colorstop_atlas(), 8);

  /* Populate m_font_database using FontConfig. */
  m_font_config = FASTUIDRAWnew FontConfig();
  m_font_config->add_system_fonts(m_font_database);
  ++InitCount;
}

void
AtlasSet::
clear_resources(void)
{
  std::lock_guard<std::mutex> M(m_mutex);
  if (m_reference_counter <= 1)
    {
      m_font_database.clear();
      m_checkerboard_image.clear();
      m_three_stops_color_stops.clear();
      m_backend.clear();
      m_painters.clear();
      m_font_config.clear();
      m_reference_counter = 0;
      ++InitCount;
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

  std::lock_guard<std::mutex> M(m_mutex);
  FcConfig *config(m_fc);
  fastuidraw::reference_counted_ptr<fastuidraw::FreeTypeLib> lib(m_lib);
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
              std::cout << "FUID:FontConfig Warning: unable to add font " << props
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

  CustomFontKey K(make_custom_font_key(weight, slant, style, family, foundry, langs));
  //std::cout << "FUID: FcFont" << K << "--->";

  std::map<CustomFontKey, fastuidraw::reference_counted_ptr<const fastuidraw::FontBase> >::const_iterator iter;
  iter = m_custom_fonts.find(K);
  if (iter != m_custom_fonts.end())
    {
      //std::cout << "Custom(" << iter->second.get() << ")\n";
      return iter->second;
    }

  FcConfig *config(m_fc);
  fastuidraw::reference_counted_ptr<fastuidraw::FreeTypeLib> lib(m_lib);
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
              //std::cout << filename << ":" << face_index << "\n";
            }
        }
      FcPatternDestroy(font_pattern);
    }
  FcPatternDestroy(pattern);

  if (lang_set)
    {
      FcLangSetDestroy(lang_set);
    }
  if (!return_value)
    {
      //std::cout << "null";
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

const fastuidraw::reference_counted_ptr<fastuidraw::gl::PainterEngineGL>&
WebCore::FastUIDraw::
currentBackend(void)
{
  std::lock_guard<std::mutex> M(AtlasSet::atlas_set().m_mutex);
  return AtlasSet::atlas_set().m_backend;
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
  AtlasSet &S(AtlasSet::atlas_set());
  if (S.m_font_config)
    {
      return S.m_font_config->select_font(weight, slant, style, family, foundry, langs, fontDatabase());
    }
  else
    {
      return nullptr;
    }
}

const fastuidraw::reference_counted_ptr<const fastuidraw::Image>&
WebCore::FastUIDraw::
checkerboardImage(void)
{
  return AtlasSet::atlas_set().m_checkerboard_image;
}

const fastuidraw::reference_counted_ptr<const fastuidraw::ColorStopSequenceOnAtlas>&
WebCore::FastUIDraw::
threeStopColorStops(void)
{
  return AtlasSet::atlas_set().m_three_stops_color_stops;
}

fastuidraw::GlyphRenderer
WebCore::FastUIDraw::
defaultGlyphRenderer(void)
{
  return AtlasSet::atlas_set().m_glyph_renderer;
}

void
WebCore::FastUIDraw::
defaultGlyphRenderer(fastuidraw::GlyphRenderer G)
{
  AtlasSet::atlas_set().m_glyph_renderer = G;
}

void
WebCore::FastUIDraw::
installCustomFont(int weight, int slant,
                  fastuidraw::c_string style,
                  fastuidraw::c_string family,
                  fastuidraw::c_string foundry,
                  fastuidraw::c_array<const fastuidraw::c_string> langs,
                  fastuidraw::reference_counted_ptr<const fastuidraw::FontBase> font)
{
  AtlasSet &S(AtlasSet::atlas_set());
  if (S.m_font_config)
    {
      S.m_font_config->install_custom_font(weight, slant, style, family, foundry, langs, font);
    }
}

void
WebCore::FastUIDraw::
setBrushToNullImage(fastuidraw::PainterBrush &brush)
{
  brush
    .reset()
    .color(0.0f, 1.0f, 0.0f, 1.0f)
    .image(checkerboardImage())
    .repeat_window(fastuidraw::vec2(0.0f, 0.0f),
                   fastuidraw::vec2(checkerboardImage()->dimensions()));
}

void
WebCore::FastUIDraw::
unimplementedFastUIDrawFunc(const char *file, int line, const char *function, unsigned int &count, const char *p)
{
  if (count < 1)
    {
      std::cout << "\nFUID: [" << file << ", " << line << ", " << function << "] unimplemented" << p << "\n";
    }
  ++count;
}

void
WebCore::FastUIDraw::
warningFastUIDrawFunc(const char *file, int line, const char *function, unsigned int &count, const char *p)
{
  if (true || count < 1)
    {
      std::cout << "\nFUID: Warning[" << file << ", " << line << ", " << function << "]: " << p << "\n";
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
      fastuidraw::reference_counted_ptr<fastuidraw::gl::PainterEngineGL> b;

      b = S.m_backend;
      FASTUIDRAWassert(b);
      m_painter = FASTUIDRAWnew fastuidraw::Painter(b);
    }
  else
    {
      m_painter = S.m_painters.back();
      S.m_painters.pop_back();
    }
  m_atlas_set_init_count_value = AtlasSet::InitCount;
}

WebCore::FastUIDraw::PainterHolder::
~PainterHolder()
{
  AtlasSet &S(AtlasSet::atlas_set());
  std::lock_guard<std::mutex> M(S.m_mutex);

  if (m_atlas_set_init_count_value == AtlasSet::InitCount)
    {
      S.m_painters.push_back(m_painter);
    }
}
