#include "FastUIDrawUtil.h"
#include "FastUIDrawResources.h"
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
  fastuidraw::FontProperties props;
  QByteArray ba;

  ba = desc.familyName().toLatin1();
  props
    .bold(desc.weight() >= QFont::Bold)
    .italic(desc.style() == QFont::StyleItalic)
    .family(ba.data());

  return_value = glyphSelector()->fetch_font(props,
                                             fastuidraw::GlyphSelector::ignore_style);

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
            << ", family = " << ba.data()
            << "\n";
  
  return return_value;
}
