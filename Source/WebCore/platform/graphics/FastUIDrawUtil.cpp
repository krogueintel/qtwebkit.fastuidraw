#include "FastUIDrawUtil.h"
#include "FastUIDrawResources.h"
#include <fontconfig/fontconfig.h>
#include <QRawFont>
#include <iostream>

static
std::ostream&
operator<<(std::ostream &str, const fastuidraw::FontProperties &obj)
{
  str << obj.source_label() << "(foundry = " << obj.foundry()
      << ", family = " << obj.family() << ", style = " << obj.style()
      << ", italic = " << obj.italic() << ", bold = " << obj.bold() << ")";
  return str;
}

fastuidraw::reference_counted_ptr<const fastuidraw::FontBase>
WebCore::FastUIDraw::
select_font(const QRawFont &desc)
{
  fastuidraw::reference_counted_ptr<const fastuidraw::FontBase> return_value;
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

  return_value = selectFont(desc.weight(),
                            slant,
                            style,
                            family,
                            foundry);

  std::cout << "Chose: ";
  if (return_value)
    {
      std::cout << return_value->properties();
    }
  else
    {
      std::cout << "NULL";
    }

  std::cout << "from: weight = " << desc.weight()
            << ", style = " << desc.style()
            << ", family = " << family
            << ", foundry = ";
  if (foundry)
    {
      std::cout << foundry;
    }
  else
    {
      std::cout << "null";
    }
  std::cout << "\n";
  
  return return_value;
}
