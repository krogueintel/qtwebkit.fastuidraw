/*
    Copyright (C) 2008 Nokia Corporation and/or its subsidiary(-ies)
    Copyright (C) 2008 Holger Hans Peter Freyther
    Copyright (C) 2009 Torch Mobile Inc. http://www.torchmobile.com/

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to
    the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
    Boston, MA 02110-1301, USA.

    This class provides all functionality needed for loading images, style sheets and html
    pages from the web. It has a memory cache for these objects.
*/
#ifndef FontPlatformData_h
#define FontPlatformData_h

#include "FontDescription.h"
#include "TextFlags.h"
#include "FastUIDrawUtil.h"
#include <QFont>
#include <QHash>
#include <QRawFont>
#include <wtf/RefCounted.h>

namespace WebCore {

class SharedBuffer;

class FontPlatformDataPrivate : public RefCounted<FontPlatformDataPrivate> {
    WTF_MAKE_NONCOPYABLE(FontPlatformDataPrivate); WTF_MAKE_FAST_ALLOCATED;
public:
    explicit
    FontPlatformDataPrivate(void)
        : m_size(0)
        , m_bold(false)
        , m_oblique(false)
        , m_isDeletedValue(false)
    { }
    FontPlatformDataPrivate(const float size, const bool bold, const bool oblique)
        : m_size(size)
        , m_bold(bold)
        , m_oblique(oblique)
        , m_isDeletedValue(false)
    {
// This is necessary for SVG Fonts, which are only supported when using QRawFont.
// It is used to construct the appropriate platform data to use as a fallback.
        QFont font;
        font.setBold(bold);
        font.setItalic(oblique);
        m_rawFont = QRawFont::fromFont(font, QFontDatabase::Any);
        m_rawFont.setPixelSize(size);
    }

    FontPlatformDataPrivate(const QRawFont& rawFont,
                            const fastuidraw::reference_counted_ptr<const fastuidraw::FontBase> &f)
        : m_rawFont(rawFont)
        , m_size(rawFont.pixelSize())
        , m_bold(rawFont.weight() >= QFont::Bold)
        , m_oblique(false)
        , m_isDeletedValue(false)
        , m_fastuidraw_font(f)
    { }

    FontPlatformDataPrivate(WTF::HashTableDeletedValueType)
        : m_isDeletedValue(true)
    { }

    const fastuidraw::reference_counted_ptr<const fastuidraw::FontBase>&
    fastuidraw_font(void) const
    {
        if (!m_fastuidraw_font) {
            m_fastuidraw_font = FastUIDraw::select_font(m_rawFont);
        }
        return m_fastuidraw_font;
    }

    QRawFont
    rawFont(void) const { return m_rawFont; }

    void
    font(QRawFont src,
         const fastuidraw::reference_counted_ptr<const fastuidraw::FontBase>& f =
         fastuidraw::reference_counted_ptr<const fastuidraw::FontBase>())
    {
        m_rawFont = src;
        m_fastuidraw_font = f;
    }

    void
    bold(bool b) { m_bold = b; }

    bool
    bold(void) const { return m_bold; }

    void
    oblique(bool b) { m_oblique = b; }

    bool
    oblique(void) const { return m_oblique; }

    void
    size(float f) { m_size = f; }

    float
    size(void) const { return m_size; }

    void
    isDeletedValue(bool b) { m_isDeletedValue = b; }

    bool
    isDeletedValue(void) const { return m_isDeletedValue; }

private:
    QRawFont m_rawFont;
    float m_size;
    bool m_bold : 1;
    bool m_oblique : 1;
    bool m_isDeletedValue : 1;
    mutable fastuidraw::reference_counted_ptr<const fastuidraw::FontBase> m_fastuidraw_font;
};

class FontPlatformData {
    WTF_MAKE_FAST_ALLOCATED;
public:
    FontPlatformData(float size, bool bold, bool oblique);
    FontPlatformData(const FontDescription&, const AtomicString& familyName, int wordSpacing = 0, int letterSpacing = 0);
    FontPlatformData(const FontPlatformData&, float size);
    FontPlatformData(const QRawFont& rawFont, const fastuidraw::reference_counted_ptr<const fastuidraw::FontBase> &f)
      : m_data(adoptRef(new FontPlatformDataPrivate(rawFont, f)))
    { }
    FontPlatformData(WTF::HashTableDeletedValueType)
        : m_data(adoptRef(new FontPlatformDataPrivate()))
    {
      m_data->isDeletedValue(true);
    }

    bool operator==(const FontPlatformData&) const;

    bool isHashTableDeletedValue() const
    {
        return m_data && m_data->isDeletedValue();
    }

    QRawFont rawFont() const
    {
        Q_ASSERT(!isHashTableDeletedValue());
        if (!m_data)
            return QRawFont();
        return m_data->rawFont();
    }

    const fastuidraw::reference_counted_ptr<const fastuidraw::FontBase> &fastuidraw_font(void) const
    {
        static fastuidraw::reference_counted_ptr<const fastuidraw::FontBase> null;
        if (!m_data)
            return null;
        return m_data->fastuidraw_font();
    }

    float size() const
    {
        Q_ASSERT(!isHashTableDeletedValue());
        if (!m_data)
            return 0;
        return m_data->size();
    }

    FontOrientation orientation() const { return Horizontal; } // FIXME: Implement.
    void setOrientation(FontOrientation) { } // FIXME: Implement.
    PassRefPtr<SharedBuffer> openTypeTable(uint32_t table) const;

    unsigned hash() const;

#ifndef NDEBUG
    String description() const;
#endif
private:
    RefPtr<FontPlatformDataPrivate> m_data;
};

} // namespace WebCore

#endif // FontPlatformData_h
