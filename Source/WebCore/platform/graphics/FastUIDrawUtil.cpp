#include "FastUIDrawUtil.h"
#include "FastUIDrawResources.h"
#include <fontconfig/fontconfig.h>
#include <QRawFont>
#include <QImage>
#include <QPixmap>
#include <QHash>
#include <vector>
#include <string>
#include <mutex>
#include <iostream>

namespace
{
  class ImageFromPixmap:public fastuidraw::ImageSourceBase
  {
  public:
    ImageFromPixmap(const QImage &image):
      m_image(image)
    {}

    virtual
    bool
    all_same_color(fastuidraw::ivec2 location, int square_size,
                   fastuidraw::u8vec4 *dst) const;

    virtual
    unsigned int
    number_levels(void) const { return 1; }

    virtual
    void
    fetch_texels(unsigned int mimpap_level,
                 fastuidraw::ivec2 location,
                 unsigned int w, unsigned int h,
                 fastuidraw::c_array<fastuidraw::u8vec4> dst) const;

    virtual
    enum fastuidraw::Image::format_t
    format(void) const;

  private:
    fastuidraw::u8vec4
    pixel(int x, int y) const;

    const QImage &m_image;
  };

  class FontHolder:fastuidraw::noncopyable
  {
  public:
    static
    fastuidraw::reference_counted_ptr<const fastuidraw::FontBase>
    get_font(const QRawFont &desc)
    {
      FontHolder &F(get());
      std::lock_guard<std::mutex> M(F.m_mutex);
      QHash<QRawFont, fastuidraw_font>::iterator iter;

      iter = F.m_fonts.find(desc);
      if (iter != F.m_fonts.end())
        {
          return iter.value();
        }
      else
        {
          return nullptr;
        }
    }

    static
    void
    add_font(const QRawFont &desc,
             fastuidraw::reference_counted_ptr<const fastuidraw::FontBase> f)
    {
      FontHolder &F(get());
      std::lock_guard<std::mutex> M(F.m_mutex);
      F.m_fonts[desc] = f;
    }
  
  private:
    FontHolder(void)
    {}

    static
    FontHolder&
    get(void)
    {
      static FontHolder R;
      return R;
    }
  
    typedef fastuidraw::reference_counted_ptr<const fastuidraw::FontBase> fastuidraw_font;

    std::mutex m_mutex;
    QHash<QRawFont, fastuidraw_font> m_fonts;
  };

  int
  font_config_wieght_from_qt_weight(int wt)
  {
    if (wt >= QFont::Black)
      {
        return FC_WEIGHT_EXTRABLACK;
      }
    else if (wt >= QFont::ExtraBold)
      {
        return FC_WEIGHT_EXTRABOLD;
      }
    else if (wt >= QFont::Bold)
      {
        return FC_WEIGHT_BOLD;
      }
    else if (wt >= QFont::DemiBold)
      {
        return FC_WEIGHT_DEMIBOLD;
      }
    else if (wt >= QFont::Medium)
      {
        return FC_WEIGHT_MEDIUM;
      }
    else if (wt >= QFont::Normal)
      {
        return FC_WEIGHT_NORMAL;
      }
    else if (wt >= QFont::Light)
      {
        return FC_WEIGHT_LIGHT;
      }
    else if (wt >= QFont::ExtraLight)
      {
        return FC_WEIGHT_EXTRALIGHT;
      }
    else
      {
        return FC_WEIGHT_THIN;
      }
  }

}

static const char langNameFromQFontDatabaseWritingSystem[][6] = {
    "",     // Any
    "en",  // Latin
    "el",  // Greek
    "ru",  // Cyrillic
    "hy",  // Armenian
    "he",  // Hebrew
    "ar",  // Arabic
    "syr", // Syriac
    "div", // Thaana
    "hi",  // Devanagari
    "bn",  // Bengali
    "pa",  // Gurmukhi
    "gu",  // Gujarati
    "or",  // Oriya
    "ta",  // Tamil
    "te",  // Telugu
    "kn",  // Kannada
    "ml",  // Malayalam
    "si",  // Sinhala
    "th",  // Thai
    "lo",  // Lao
    "bo",  // Tibetan
    "my",  // Myanmar
    "ka",  // Georgian
    "km",  // Khmer
    "zh-cn", // SimplifiedChinese
    "zh-tw", // TraditionalChinese
    "ja",  // Japanese
    "ko",  // Korean
    "vi",  // Vietnamese
    "", // Symbol
    "sga", // Ogham
    "non", // Runic
    "man" // N'Ko
};

///////////////////////////////////
// ImageFromPixmap methods
enum fastuidraw::Image::format_t
ImageFromPixmap::
format(void) const
{
  //return fastuidraw::Image::rgba_format;
  switch(m_image.format())
    {
    default:
      return fastuidraw::Image::rgba_format;

    case QImage::Format_ARGB32_Premultiplied:
    case QImage::Format_ARGB8565_Premultiplied:
    case QImage::Format_ARGB6666_Premultiplied:
    case QImage::Format_ARGB8555_Premultiplied:
    case QImage::Format_ARGB4444_Premultiplied:
    case QImage::Format_RGBA8888_Premultiplied:
    case QImage::Format_A2BGR30_Premultiplied:
    case QImage::Format_A2RGB30_Premultiplied:
      //case QImage::Format_RGBA64_Premultiplied:
      return fastuidraw::Image::premultipied_rgba_format;
    }
}

fastuidraw::u8vec4
ImageFromPixmap::
pixel(int x, int y) const
{
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (x >= m_image.width()) x = m_image.width() - 1;
  if (y >= m_image.height()) y = m_image.height() - 1;
  /* NOTE: doing QColor qcolor(m_image.pixel(x, y)
   * yeilds a QColor where qcolor.alpha() is always 255
   */
  QRgb qcolor(m_image.pixel(x, y));
  return fastuidraw::u8vec4(qRed(qcolor),
                            qGreen(qcolor),
                            qBlue(qcolor),
                            qAlpha(qcolor));
}

bool
ImageFromPixmap::
all_same_color(fastuidraw::ivec2 location, int square_size,
               fastuidraw::u8vec4 *dst) const
{
    *dst = pixel(location.x(), location.y());
    for (int x = location.x(), ix = 0; ix < square_size; ++ix, ++x) {
        for (int y = location.y(), iy = 0; iy < square_size; ++iy, ++y) {
            fastuidraw::u8vec4 p(pixel(x, y));
            if (p != *dst) {
                return false;
            }
        }
    }
    return true;
}

void
ImageFromPixmap::
fetch_texels(unsigned int level,
             fastuidraw::ivec2 location,
             unsigned int w, unsigned int h,
             fastuidraw::c_array<fastuidraw::u8vec4> dst) const
{
    FASTUIDRAWunused(level);
    for (int y = location.y(), iy = 0; iy < h; ++iy, ++y) {
        for (int x = location.x(), ix = 0; ix < w; ++ix, ++x) {
            dst[ix + w * iy] = pixel(x, y);
        }
    }
}

////////////////////////////////////////////////
// WebCore::FastUIDraw methods
fastuidraw::reference_counted_ptr<const fastuidraw::FontBase>
WebCore::FastUIDraw::
install_custom_font(const QRawFont &desc,
                    fastuidraw::reference_counted_ptr<fastuidraw::FreeTypeFace::GeneratorBase> h)
{
  fastuidraw::reference_counted_ptr<const fastuidraw::FontBase> f;

  f = FontHolder::get_font(desc);
  if (f)
    {
      return f;
    }

  int slant;
  const QString in_family(desc.familyName());
  const QString in_style(desc.styleName());
  QByteArray tmp1, tmp2;
  fastuidraw::c_string family, foundry(nullptr), style(nullptr);

  f = FASTUIDRAWnew fastuidraw::FontFreeType(h);

  switch (desc.style())
    {
    case QFont::StyleItalic:
      slant = FC_SLANT_ITALIC;
      break;

    case QFont::StyleOblique:
      slant = FC_SLANT_OBLIQUE;

    default:
      slant = FC_SLANT_ROMAN;
    }

  /* Qt makes the foundry part of the family by
   * suffixing the string with [Foundry]
   */
  QString::const_iterator iter_open, iter_close;
  iter_open = std::find(in_family.begin(), in_family.end(), '[');
  iter_close = std::find(in_family.begin(), in_family.end(), ']');

  if (iter_open != in_family.end())
    {
      QString family_str, foundry_str;

      for (QString::const_iterator i = in_family.begin(); i != iter_open; ++i)
        {
          family_str.push_back(*i);
        }
      tmp1 = family_str.toLatin1();
      family = tmp1.data();

      ++iter_open;
      for (QString::const_iterator i = iter_open; i != iter_close; ++i)
        {
          foundry_str.push_back(*i);
        }
      tmp2 = foundry_str.toLatin1();
      foundry = tmp2.data();
    }
  else
    {
      tmp1 = in_family.toLatin1();
      family = tmp1.data();
    }

  const QList<QFontDatabase::WritingSystem> &qLangs(desc.supportedWritingSystems());
  std::vector<fastuidraw::c_string> langs;
  fastuidraw::c_array<const fastuidraw::c_string> langs_array;

  for (auto c : qLangs)
    {
      langs.push_back(langNameFromQFontDatabaseWritingSystem[c]);
    }

  if (!langs.empty())
    {
      langs_array = fastuidraw::c_array<const fastuidraw::c_string>(&langs[0], langs.size());
    }

  FontHolder::add_font(desc, f);
  installCustomFont(font_config_wieght_from_qt_weight(desc.weight()),
                    slant,
                    style,
                    family,
                    foundry,
                    langs_array,
                    f);
  return f;
}

fastuidraw::reference_counted_ptr<const fastuidraw::FontBase>
WebCore::FastUIDraw::
select_font(const QRawFont &desc)
{
  fastuidraw::reference_counted_ptr<const fastuidraw::FontBase> return_value;

  return_value = FontHolder::get_font(desc);
  if (return_value)
    {
      return return_value;
    }

  int slant;
  const QString in_family(desc.familyName());
  QByteArray tmp1, tmp2;
  fastuidraw::c_string family, foundry(nullptr), style(nullptr);

  switch (desc.style())
    {
    case QFont::StyleItalic:
      slant = FC_SLANT_ITALIC;
      break;

    case QFont::StyleOblique:
      slant = FC_SLANT_OBLIQUE;

    default:
      slant = FC_SLANT_ROMAN;
    }

  /* Qt makes the foundry part of the family by
   * suffixing the string with [Foundry]
   */
  QString::const_iterator iter_open, iter_close;
  iter_open = std::find(in_family.begin(), in_family.end(), '[');
  iter_close = std::find(in_family.begin(), in_family.end(), ']');

  if (iter_open != in_family.end())
    {
      QString family_str, foundry_str;

      for (QString::const_iterator i = in_family.begin(); i != iter_open; ++i)
        {
          family_str.push_back(*i);
        }
      tmp1 = family_str.toLatin1();
      family = tmp1.data();

      ++iter_open;
      for (QString::const_iterator i = iter_open; i != iter_close; ++i)
        {
          foundry_str.push_back(*i);
        }
      tmp2 = foundry_str.toLatin1();
      foundry = tmp2.data();
    }
  else
    {
      tmp1 = in_family.toLatin1();
      family = tmp1.data();
    }

  const QList<QFontDatabase::WritingSystem> &qLangs(desc.supportedWritingSystems());
  std::vector<fastuidraw::c_string> langs;
  fastuidraw::c_array<const fastuidraw::c_string> langs_array;

  for (auto c : qLangs)
    {
      langs.push_back(langNameFromQFontDatabaseWritingSystem[c]);
    }

  if (!langs.empty())
    {
      langs_array = fastuidraw::c_array<const fastuidraw::c_string>(&langs[0], langs.size());
    }

  return_value = selectFont(font_config_wieght_from_qt_weight(desc.weight()),
                            slant, style, family, foundry,
                            langs_array);
  FontHolder::add_font(desc, return_value);
  
  return return_value;
}

fastuidraw::reference_counted_ptr<const fastuidraw::Image>
WebCore::FastUIDraw::
create_fastuidraw_image(const QImage &image)
{
  ImageFromPixmap tmp(image);
  return fastuidraw::Image::create(FastUIDraw::imageAtlas(),
                                   image.width(), image.height(),
                                   tmp);
}

fastuidraw::reference_counted_ptr<const fastuidraw::Image>
WebCore::FastUIDraw::
create_fastuidraw_image(const QPixmap &pixmap)
{
  return create_fastuidraw_image(pixmap.toImage());
}

void
WebCore::FastUIDraw::
compose_with_pattern(fastuidraw::PainterBrush &brush,
                     const FloatRect& srcRect, const AffineTransform& patternTransform,
                     const FloatPoint& phase, const FloatSize& spacing)
{
  fastuidraw::vec2 vec2_phase;

  FASTUIDRAWunused(spacing);

  vec2_phase = vec2FromFloatPoint(phase);

  compose_with_pattern_transformation(brush, patternTransform);
  brush
    .repeat_window(fastuidraw::vec2(srcRect.x(), srcRect.y()),
                   fastuidraw::vec2(srcRect.width(), srcRect.height()),
                   fastuidraw::PainterBrush::spread_repeat,
                   fastuidraw::PainterBrush::spread_repeat)
    .apply_translate(fastuidraw::vec2(-phase.x(), -phase.y()));
}

void
WebCore::FastUIDraw::
compose_with_pattern_transformation(fastuidraw::PainterBrush &brush,
                                    const AffineTransform& patternTransform)
{
  /* patternTransform takes as input -image- pixel coordinates
   * and applies a scaling factor to give logical coordinates.
   * fastuidraw::PainterBrush's transformation goes from logical
   * coordinates to pixel coordinates, so we need to invert
   * the transformation.
   */
  fastuidraw::float3x3 M;
  fastuidraw::float2x2 N;
  fastuidraw::vec2 T;
  Optional<AffineTransform> inverse_tr;

  inverse_tr = patternTransform.inverse();
  FastUIDraw::computeToFastUIDrawMatrixT(inverse_tr.value(), &M);

  N(0, 0) = M(0, 0);
  N(0, 1) = M(0, 1);
  N(1, 0) = M(1, 0);
  N(1, 1) = M(1, 1);
  T.x() = M(0, 2);
  T.y() = M(1, 2);

  brush
    .apply_translate(T)
    .apply_matrix(N);
}

//////////////////////////////////////////
// WebCore::FastUIDraw::FUIDTrace methods
static int fuid_tracking_active = 0;
void
WebCore::FastUIDraw::FUIDTrace::
startTracking(void)
{
  ++fuid_tracking_active;
}

void
WebCore::FastUIDraw::FUIDTrace::
endTracking(void)
{
  --fuid_tracking_active;
}

bool
WebCore::FastUIDraw::FUIDTrace::
trackingActive(void)
{
  return fuid_tracking_active != 0;
}

static int fuid_depth = 0;
WebCore::FastUIDraw::FUIDTrace::
FUIDTrace(const char *file, int line, const char *function)
{
  if (fuid_tracking_active != 0 && false)
    {
      std::cout << "FUID" << std::string(fuid_depth, ' ') << "[" << file << ", "
                << line << ", " << function << "\n";
    }
  ++fuid_depth;
}

WebCore::FastUIDraw::FUIDTrace::
FUIDTrace(const char *file, int line, const char *function, const char *message)
{
  if (fuid_tracking_active != 0 && false)
    {
      std::cout << "FUID" << std::string(fuid_depth, ' ') << "[" << file << ", "
                << line << ", " << function << ":" << message << "\n";
    }
  ++fuid_depth;
}

WebCore::FastUIDraw::FUIDTrace::
~FUIDTrace()
{
  --fuid_depth;
}
