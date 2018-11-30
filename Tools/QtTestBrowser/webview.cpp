/*
 * Copyright (C) 2009 Nokia Corporation and/or its subsidiary(-ies)
 * Copyright (C) 2009 Girish Ramakrishnan <girish@forwardbias.in>
 * Copyright (C) 2006 George Staikos <staikos@kde.org>
 * Copyright (C) 2006 Dirk Mueller <mueller@kde.org>
 * Copyright (C) 2006 Zack Rusin <zack@kde.org>
 * Copyright (C) 2006 Simon Hausmann <hausmann@kde.org>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "webview.h"

#include <QAction>
#include <QGraphicsScene>
#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsSceneMouseEvent>
#include <QMenu>
#include <QScrollBar>
#include <QTimer>

#ifndef QT_NO_ANIMATION
#include <QAbstractAnimation>
#include <QAbstractTransition>
#include <QFinalState>
#include <QPropertyAnimation>
#include <QState>
#include <QStateMachine>
#endif

#include <iostream>
#include <string>
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
#include <fastuidraw/gl_backend/ngl_header.hpp>

namespace
{
  HacksForQt*&
  HacksForQtPtr(void)
  {
    static HacksForQt *ptr(0);
    return ptr;
  }

  QGLFormat
  gl45_format(void)
  {
    QGLFormat qf;
    qf.setProfile(QGLFormat::CoreProfile);
    qf.setVersion(4, 5);
    return qf;
  }

  static void*
  get_proc_from_hacks_for_qt(void *c, fastuidraw::c_string proc_name)
  {
    void *f;
    QGLContext *ctx;
    
    ctx = static_cast<QGLContext*>(c);
    f = reinterpret_cast<void*>(ctx->getProcAddress(QString(proc_name)));
    std::cout << proc_name << ":" << f << " of GL version "
              << ctx->format().majorVersion()
              << "." << ctx->format().minorVersion()
              << "\n";
    return f;
  }

  inline
  std::ostream&
  operator<<(std::ostream &str, const fastuidraw::FontProperties &obj)
  {
    str << obj.source_label() << "(" << obj.foundry()
        << ", " << obj.family() << ", " << obj.style()
        << ", " << obj.italic() << ", " << obj.bold() << ")";
    return str;
  }

  /* The purpose of the DaaBufferHolder is to -DELAY-
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

            std::cout << "add font: " << props << "\n";
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

  class FastUIDrawGLLogger:public fastuidraw::gl_binding::CallbackGL
  {
  public:
    virtual
    void
    pre_call(fastuidraw::c_string call_string_values,
             fastuidraw::c_string call_string_src,
             fastuidraw::c_string function_name,
             void *function_ptr,
             fastuidraw::c_string src_file, int src_line)
    {
      FASTUIDRAWunused(call_string_src);
      FASTUIDRAWunused(function_name);
      FASTUIDRAWunused(function_ptr);
      std::cout << "Pre: [" << src_file << "," << src_line << "] "
                << call_string_values << "\n";
    }

    virtual
    void
    post_call(fastuidraw::c_string call_string_values,
              fastuidraw::c_string call_string_src,
              fastuidraw::c_string function_name,
              fastuidraw::c_string error_string,
              void *function_ptr,
              fastuidraw::c_string src_file, int src_line)
    {
      FASTUIDRAWunused(call_string_src);
      FASTUIDRAWunused(function_name);
      FASTUIDRAWunused(function_ptr);
      FASTUIDRAWunused(error_string);
      std::cout << "Post: [" << src_file << "," << src_line << "] "
                << call_string_values;

      if (error_string && *error_string)
        {
          std::cout << "{" << error_string << "}";
        }
      std::cout << "\n";
    }
  };
}

HacksForQt::HacksForQt(void):
  QGLWidget(gl45_format())
{
  hide();

  /* force the GL context to exist and make it current */
  makeCurrent();
  //m_gl_logger = FASTUIDRAWnew FastUIDrawGLLogger();
  fastuidraw::gl_binding::get_proc_function(context(), get_proc_from_hacks_for_qt, true);

  m_image_atlas = FASTUIDRAWnew fastuidraw::gl::ImageAtlasGL(fastuidraw::gl::ImageAtlasGL::params());
  m_glyph_atlas = FASTUIDRAWnew fastuidraw::gl::GlyphAtlasGL(fastuidraw::gl::GlyphAtlasGL::params());
  m_colorstop_atlas = FASTUIDRAWnew fastuidraw::gl::ColorStopAtlasGL(fastuidraw::gl::ColorStopAtlasGL::params());

  fastuidraw::gl::PainterBackendGL::ConfigurationGL painter_params;
  painter_params
    .image_atlas(m_image_atlas)
    .glyph_atlas(m_glyph_atlas)
    .colorstop_atlas(m_colorstop_atlas)
    .configure_from_context(true);

  m_backend = fastuidraw::gl::PainterBackendGL::create(painter_params);
  m_glyph_cache = FASTUIDRAWnew fastuidraw::GlyphCache(m_glyph_atlas);
  m_glyph_selector = FASTUIDRAWnew fastuidraw::GlyphSelector();
  qSetFastUIDrawResources(m_glyph_cache,
                          m_image_atlas,
                          m_colorstop_atlas,
                          m_glyph_selector);

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

  doneCurrent();
}

HacksForQt::~HacksForQt()
{
  makeCurrent();
  m_glyph_cache.clear();
  m_glyph_cache.clear();
  m_image_atlas.clear();
  m_colorstop_atlas.clear();
  m_glyph_selector.clear();
  m_gl_logger.clear();

  qSetFastUIDrawResources(m_glyph_cache,
                          m_image_atlas,
                          m_colorstop_atlas,
                          m_glyph_selector);
  doneCurrent();
}

HacksForQt*
HacksForQt::getHacksForQt(void)
{
    if (!HacksForQtPtr()) {
        HacksForQtPtr() = new HacksForQt();
    }
    return HacksForQtPtr();
}

void
HacksForQt::shut_down(void)
{
  if(HacksForQtPtr())
    {
      delete HacksForQtPtr();
    }
  HacksForQtPtr() = 0;
}

WebViewTraditional::WebViewTraditional(QWidget* parent) :
  QWebView(parent),
  m_drawWithFastUIDraw(false)
{
}

WebViewTraditional::WebViewTraditional(QWidget* parent, enum paint_with_fastuidraw_t) :
  QWebView(HacksForQt::getHacksForQt(),
           FASTUIDRAWnew fastuidraw::Painter(HacksForQt::getHacksForQt()->m_backend->create_sharing_shaders()),
           parent),
  m_drawWithFastUIDraw(true)
{}

bool WebViewTraditional::drawWithFastUIDraw(void) const
{
  return m_drawWithFastUIDraw;
}
  
WebViewGraphicsBased::WebViewGraphicsBased(QWidget* parent)
    : QGraphicsView(parent)
    , m_item(new GraphicsWebView)
    , m_numPaintsTotal(0)
    , m_numPaintsSinceLastMeasure(0)
    , m_measureFps(false)
    , m_resizesToContents(false)
    , m_machine(0)
{
    setScene(new QGraphicsScene(this));
    scene()->addItem(m_item);
    scene()->setFocusItem(m_item);

    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_updateTimer = new QTimer(this);
    m_updateTimer->setInterval(1000);
    connect(m_updateTimer, SIGNAL(timeout()), this, SLOT(updateFrameRate()));
}

void WebViewGraphicsBased::setPage(QWebPage* page)
{
    connect(page->mainFrame(), SIGNAL(contentsSizeChanged(const QSize&)), SLOT(contentsSizeChanged(const QSize&)));
    connect(page, SIGNAL(scrollRequested(int, int, const QRect&)), SLOT(scrollRequested(int, int)));
    graphicsWebView()->setPage(page);
}

void WebViewGraphicsBased::scrollRequested(int x, int y)
{
    if (!m_resizesToContents)
        return;

    // Turn off interactive mode while scrolling, or QGraphicsView will replay the
    // last mouse event which may cause WebKit to initiate a drag operation.
    bool interactive = isInteractive();
    setInteractive(false);

    verticalScrollBar()->setValue(-y);
    horizontalScrollBar()->setValue(-x);

    setInteractive(interactive);
}

void WebViewGraphicsBased::contentsSizeChanged(const QSize& size)
{
    if (m_resizesToContents)
        scene()->setSceneRect(0, 0, size.width(), size.height());
}

void WebViewGraphicsBased::setResizesToContents(bool b)
{
    if (b == m_resizesToContents)
        return;

    m_resizesToContents = b;
    graphicsWebView()->setResizesToContents(m_resizesToContents);

    // When setting resizesToContents ON, our web view widget will always size as big as the
    // web content being displayed, and so will the QWebPage's viewport. It implies that internally
    // WebCore will work as if there was no content rendered offscreen, and then no scrollbars need
    // drawing. In order to keep scrolling working, we:
    //
    // 1) Set QGraphicsView's scrollbars policy back to 'auto'.
    // 2) Set scene's boundaries rect to an invalid size, which automatically makes it to be as big
    //    as it needs to enclose all items onto it. We do that because QGraphicsView also calculates
    //    the size of its scrollable area according to the amount of content in scene that is rendered
    //    offscreen.
    // 3) Set QWebPage's preferredContentsSize according to the size of QGraphicsView's viewport,
    //    so WebCore properly lays pages out.
    //
    // On the other hand, when toggling resizesToContents OFF, we set back the default values, as
    // opposite as described above.
    if (m_resizesToContents) {
        setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        graphicsWebView()->page()->setPreferredContentsSize(size());
        QRectF itemRect(graphicsWebView()->geometry().topLeft(), graphicsWebView()->page()->mainFrame()->contentsSize());
        graphicsWebView()->setGeometry(itemRect);
        scene()->setSceneRect(itemRect);
    } else {
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        graphicsWebView()->page()->setPreferredContentsSize(QSize());
        QRect viewportRect(QPoint(0, 0), size());
        graphicsWebView()->setGeometry(viewportRect);
        scene()->setSceneRect(viewportRect);
    }
}

void WebViewGraphicsBased::resizeEvent(QResizeEvent* event)
{
    QGraphicsView::resizeEvent(event);

    QSize size(event->size());

    if (m_resizesToContents) {
        graphicsWebView()->page()->setPreferredContentsSize(size);
        return;
    }

    QRectF rect(QPoint(0, 0), size);
    graphicsWebView()->setGeometry(rect);
    scene()->setSceneRect(rect);
}

void WebViewGraphicsBased::setFrameRateMeasurementEnabled(bool enabled)
{
    m_measureFps = enabled;
    if (m_measureFps) {
        m_lastConsultTime = m_startTime = QTime::currentTime();
        m_fpsTimer.start();
        m_updateTimer->start();
    } else {
        m_fpsTimer.stop();
        m_updateTimer->stop();
    }
}

void WebViewGraphicsBased::updateFrameRate()
{
    const QTime now = QTime::currentTime();
    int interval = m_lastConsultTime.msecsTo(now);
    int frames = m_fpsTimer.numFrames(interval);
    int current = interval ? frames * 1000 / interval : 0;

    emit currentFPSUpdated(current);

    m_lastConsultTime = now;
}

void WebViewGraphicsBased::animatedFlip()
{
#ifndef QT_NO_ANIMATION
    QSizeF center = graphicsWebView()->boundingRect().size() / 2;
    QPointF centerPoint = QPointF(center.width(), center.height());
    graphicsWebView()->setTransformOriginPoint(centerPoint);

    QPropertyAnimation* animation = new QPropertyAnimation(graphicsWebView(), "rotation", this);
    animation->setDuration(1000);

    int rotation = int(graphicsWebView()->rotation());

    animation->setStartValue(rotation);
    animation->setEndValue(rotation + 180 - (rotation % 180));

    animation->start(QAbstractAnimation::DeleteWhenStopped);
#endif
}

void WebViewGraphicsBased::animatedYFlip()
{
#ifndef QT_NO_ANIMATION
    if (!m_machine) {
        m_machine = new QStateMachine(this);

        QState* s0 = new QState(m_machine);
        s0->assignProperty(this, "yRotation", 0);

        QState* s1 = new QState(m_machine);
        s1->assignProperty(this, "yRotation", 90);

        QAbstractTransition* t1 = s0->addTransition(s1);
        QPropertyAnimation* yRotationAnim = new QPropertyAnimation(this, "yRotation", this);
        t1->addAnimation(yRotationAnim);

        QState* s2 = new QState(m_machine);
        s2->assignProperty(this, "yRotation", -90);
        s1->addTransition(s1, SIGNAL(propertiesAssigned()), s2);

        QState* s3 = new QState(m_machine);
        s3->assignProperty(this, "yRotation", 0);

        QAbstractTransition* t2 = s2->addTransition(s3);
        t2->addAnimation(yRotationAnim);

        QFinalState* final = new QFinalState(m_machine);
        s3->addTransition(s3, SIGNAL(propertiesAssigned()), final);

        m_machine->setInitialState(s0);
        yRotationAnim->setDuration(1000);
    }

    m_machine->start();
#endif
}

void WebViewGraphicsBased::paintEvent(QPaintEvent* event)
{
    QGraphicsView::paintEvent(event);
    if (!m_measureFps)
        return;
}

static QMenu* createContextMenu(QWebPage* page, QPoint position)
{
    QMenu* menu = page->createStandardContextMenu();

    QWebHitTestResult r = page->mainFrame()->hitTestContent(position);

    if (!r.linkUrl().isEmpty()) {
#ifndef QT_NO_DESKTOPSERVICES
        WebPage* webPage = qobject_cast<WebPage*>(page);
        QAction* newTabAction = menu->addAction("Open in Default &Browser", webPage, SLOT(openUrlInDefaultBrowser()));
        newTabAction->setData(r.linkUrl());
        menu->insertAction(menu->actions().at(2), newTabAction);
#endif
        if (r.linkTargetFrame() != r.frame())
            menu->insertAction(menu->actions().at(0), page->action(QWebPage::OpenLinkInThisWindow));
    }
    return menu;
}

void GraphicsWebView::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    setProperty("mouseButtons", QVariant::fromValue(int(event->buttons())));
    setProperty("keyboardModifiers", QVariant::fromValue(int(event->modifiers())));

    QGraphicsWebView::mousePressEvent(event);
}

void WebViewTraditional::mousePressEvent(QMouseEvent* event)
{
    setProperty("mouseButtons", QVariant::fromValue(int(event->buttons())));
    setProperty("keyboardModifiers", QVariant::fromValue(int(event->modifiers())));

    QWebView::mousePressEvent(event);
}

void GraphicsWebView::contextMenuEvent(QGraphicsSceneContextMenuEvent* event)
{
    QMenu* menu = createContextMenu(page(), event->pos().toPoint());
    menu->exec(event->screenPos());
    delete menu;
}

void WebViewTraditional::contextMenuEvent(QContextMenuEvent* event)
{
    QMenu* menu = createContextMenu(page(), event->pos());
    menu->exec(event->globalPos());
    delete menu;
}

