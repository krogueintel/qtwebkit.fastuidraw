#include "FastUIDrawUtil.h"
#include "FastUIDrawResources.h"
#include <fontconfig/fontconfig.h>
#include <QRawFont>
#include <QHash>
#include <vector>
#include <string>
#include <mutex>
#include <iostream>


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

static
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
