/*
    Copyright (C) 2008 Nokia Corporation and/or its subsidiary(-ies)
    Copyright (C) 2008 Holger Hans Peter Freyther
    Copyright (C) 2009 Girish Ramakrishnan <girish@forwardbias.in>

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
*/

#include "config.h"
#include "qwebview.h"
#include "qwebkitglobal.h"

#include "QWebPageClient.h"
#include "qwebframe.h"
#include "qwebpage_p.h"
#ifndef QT_NO_ACCESSIBILITY
#include "qwebviewaccessible_p.h"
#endif
#include <qbitmap.h>
#include <qdir.h>
#include <qevent.h>
#include <qfile.h>
#include <qpainter.h>
#if HAVE(QTPRINTSUPPORT)
#include <qprinter.h>
#endif

#include <QOpenGLContext>
#include <QFunctionPointer>
#include <iostream>

#include <fastuidraw/gl_backend/ngl_header.hpp>
#include <fastuidraw/gl_backend/gl_get.hpp>
#include <fastuidraw/gl_backend/painter_backend_gl.hpp>

template<GLenum state>
class GLStateRestore
{
public:
  explicit
  GLStateRestore(void)
  {
    m_value = fastuidraw_glIsEnabled(state);
  }

  ~GLStateRestore()
  {
    if (m_value)
      {
        fastuidraw_glEnable(state);
      }
    else
      {
        fastuidraw_glDisable(state);
      }
  }

  GLboolean m_value;
};

template<GLenum state>
class GLStateRestoreArray
{
public:
  explicit
  GLStateRestoreArray(unsigned int N):
    m_values(N)
  {
    for (unsigned int i = 0; i < N; ++i) {
        m_values[i] = fastuidraw_glIsEnabled(state + i);
    }
  }

  ~GLStateRestoreArray()
  {
    for (unsigned int i = 0; i < m_values.size(); ++i) {
        if (m_values[i]) {
            fastuidraw_glEnable(state + i);
        } else {
            fastuidraw_glDisable(state + i);
        }
    }
  }
private:
  std::vector<GLboolean> m_values;
};



class QWebViewPrivate {
public:
    QWebViewPrivate(QWebView *view)
        : view(view)
        , page(0)
        , renderHints(QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform)
    {
        Q_ASSERT(view);
    }

    virtual ~QWebViewPrivate();

    void _q_pageDestroyed();
    void detachCurrentPage();

    QWebView *view;
    QWebPage *page;

    QPainter::RenderHints renderHints;
    bool m_drawWithFastUIDraw;
    fastuidraw::reference_counted_ptr<fastuidraw::Painter> m_painter;
    fastuidraw::reference_counted_ptr<fastuidraw::gl::PainterBackendGL::SurfaceGL> m_surface;
};

QWebViewPrivate::~QWebViewPrivate()
{
    detachCurrentPage();
}

void QWebViewPrivate::_q_pageDestroyed()
{
    page = 0;
    view->setPage(0);
}

/*!
    \class QWebView
    \since 4.4
    \brief The QWebView class provides a widget that is used to view and edit
    web documents.
    \ingroup advanced

    \inmodule QtWebKit

    QWebView is the main widget component of the Qt WebKit web browsing module.
    It can be used in various applications to display web content live from the
    Internet.

    The image below shows QWebView previewed in Qt Creator with the \l{Qt Homepage}.

    \image qwebview-url.png

    A web site can be loaded onto QWebView with the load() function. Like all
    Qt widgets, the show() function must be invoked in order to display
    QWebView. The snippet below illustrates this:

    \snippet webkitsnippets/simple/main.cpp Using QWebView

    Alternatively, setUrl() can also be used to load a web site. If you have
    the HTML content readily available, you can use setHtml() instead.

    The loadStarted() signal is emitted when the view begins loading. The
    loadProgress() signal, on the other hand, is emitted whenever an element of
    the web view completes loading, such as an embedded image, a script, etc.
    Finally, the loadFinished() signal is emitted when the view has loaded
    completely. It's argument - either \c true or \c false - indicates
    load success or failure.

    The page() function returns a pointer to the web page object. See
    \l{Elements of QWebView} for an explanation of how the web page
    is related to the view. To modify your web view's settings, you can access
    the QWebSettings object with the settings() function. With QWebSettings,
    you can change the default fonts, enable or disable features such as
    JavaScript and plugins.

    The title of an HTML document can be accessed with the title() property.
    Additionally, a web site may also specify an icon, which can be accessed
    using the icon() property. If the title or the icon changes, the corresponding
    titleChanged() and iconChanged() signals will be emitted. The
    textSizeMultiplier() property can be used to change the overall size of
    the text displayed in the web view.

    If you require a custom context menu, you can implement it by reimplementing
    \l{QWidget::}{contextMenuEvent()} and populating your QMenu with the actions
    obtained from pageAction(). More functionality such as reloading the view,
    copying selected text to the clipboard, or pasting into the view, is also
    encapsulated within the QAction objects returned by pageAction(). These
    actions can be programmatically triggered using triggerPageAction().
    Alternatively, the actions can be added to a toolbar or a menu directly.
    QWebView maintains the state of the returned actions but allows
    modification of action properties such as \l{QAction::}{text} or
    \l{QAction::}{icon}.

    A QWebView can be printed onto a QPrinter using the print() function.
    This function is marked as a slot and can be conveniently connected to
    \l{QPrintPreviewDialog}'s \l{QPrintPreviewDialog::}{paintRequested()}
    signal.

    If you want to provide support for web sites that allow the user to open
    new windows, such as pop-up windows, you can subclass QWebView and
    reimplement the createWindow() function.

    \section1 Elements of QWebView

    QWebView consists of other objects such as QWebFrame and QWebPage. The
    flowchart below shows these elements are related.

    \image qwebview-diagram.png

    \note It is possible to use QWebPage and QWebFrame, without using QWebView,
    if you do not require QWidget attributes. Nevertheless, Qt WebKit depends
    on QtGui, so you should use a QApplication instead of QCoreApplication.

    \sa {Previewer Example}, {Tab Browser}, {Form Extractor Example},
    {Fancy Browser Example}
*/

#ifndef QT_NO_ACCESSIBILITY
static QAccessibleInterface* accessibleInterfaceFactory(const QString& key, QObject* object)
{
    Q_UNUSED(key)

    if (QWebPage* page = qobject_cast<QWebPage*>(object))
        return new QWebPageAccessible(page);
    if (QWebView* view = qobject_cast<QWebView*>(object))
        return new QWebViewAccessible(view);
    if (QWebFrame* frame = qobject_cast<QWebFrame*>(object))
        return new QWebFrameAccessible(frame);
    return 0;
}
#endif

/*!
    Constructs an empty QWebView with parent \a parent.

    \sa load()
*/
QWebView::QWebView(QWidget *parent)
    : QOpenGLWidget(parent)
{
  QSurfaceFormat sf(QSurfaceFormat::defaultFormat());

    /* On Mesa/i965, GL versions higher than 3.0 are only
     * available as Core Profiles, so we use that.
     */
    sf.setMajorVersion(4);
    sf.setMinorVersion(5);
    sf.setProfile(QSurfaceFormat::CoreProfile);
    setFormat(sf);
    create();

    d = new QWebViewPrivate(this);
    d->m_drawWithFastUIDraw = false;

#if !defined(Q_WS_QWS)
    setAttribute(Qt::WA_InputMethodEnabled);
#endif

    setAttribute(Qt::WA_AcceptTouchEvents);
    setAcceptDrops(true);

    setMouseTracking(true);
    setFocusPolicy(Qt::WheelFocus);
    setAutoFillBackground(true);

#ifndef QT_NO_ACCESSIBILITY
    QAccessible::installFactory(accessibleInterfaceFactory);
#endif
}

/*!
    Destroys the web view.
*/
QWebView::~QWebView()
{
    if (d->m_painter) {
        qFastUIDrawClearResources();
    }
    delete d;
}

/*!
    Returns a pointer to the underlying web page.

    \sa setPage()
*/
QWebPage *QWebView::page() const
{
    if (!d->page) {
        QWebView *that = const_cast<QWebView *>(this);
        that->setPage(new QWebPage(that));
    }
    return d->page;
}

void QWebViewPrivate::detachCurrentPage()
{
    if (!page)
        return;

    page->d->view = 0;

    // if the page client is the special client constructed for
    // delegating the responsibilities to a QWidget, we need
    // to destroy it.

    if (page->d->client && page->d->client->isQWidgetClient())
        page->d->client.reset();

    page->d->client.take();

    // if the page was created by us, we own it and need to
    // destroy it as well.

    if (page->parent() == view)
        delete page;
    else
        page->disconnect(view);

    page = 0;
}

/*!
    Makes \a page the new web page of the web view.

    The parent QObject of the provided page remains the owner
    of the object. If the current page is a child of the web
    view, it will be deleted.

    \sa page()
*/
void QWebView::setPage(QWebPage* page)
{
    if (d->page == page)
        return;

    d->detachCurrentPage();
    d->page = page;

    if (d->page) {
        d->page->setView(this);
        d->page->setPalette(palette());
        // #### connect signals
        QWebFrame *mainFrame = d->page->mainFrame();
        connect(mainFrame, SIGNAL(titleChanged(QString)),
            this, SIGNAL(titleChanged(QString)));
        connect(mainFrame, SIGNAL(iconChanged()),
            this, SIGNAL(iconChanged()));
        connect(mainFrame, SIGNAL(urlChanged(QUrl)),
            this, SIGNAL(urlChanged(QUrl)));

        connect(d->page, SIGNAL(loadStarted()),
            this, SIGNAL(loadStarted()));
        connect(d->page, SIGNAL(loadProgress(int)),
            this, SIGNAL(loadProgress(int)));
        connect(d->page, SIGNAL(loadFinished(bool)),
            this, SIGNAL(loadFinished(bool)));
        connect(d->page, SIGNAL(statusBarMessage(QString)),
            this, SIGNAL(statusBarMessage(QString)));
        connect(d->page, SIGNAL(linkClicked(QUrl)),
            this, SIGNAL(linkClicked(QUrl)));
        connect(d->page, SIGNAL(selectionChanged()),
            this, SIGNAL(selectionChanged()));

        connect(d->page, SIGNAL(microFocusChanged()),
            this, SLOT(updateMicroFocus()));
        connect(d->page, SIGNAL(destroyed()),
            this, SLOT(_q_pageDestroyed()));
    }
    setAttribute(Qt::WA_OpaquePaintEvent, d->page);
    update();
}

/*!
    Loads the specified \a url and displays it.

    \note The view remains the same until enough data has arrived to display the new \a url.

    \sa setUrl(), url(), urlChanged(), QUrl::fromUserInput()
*/
void QWebView::load(const QUrl &url)
{
    page()->mainFrame()->load(url);
}

/*!
    \fn void QWebView::load(const QNetworkRequest &request, QNetworkAccessManager::Operation operation, const QByteArray &body)

    Loads a network request, \a request, using the method specified in \a operation.

    \a body is optional and is only used for POST operations.

    \note The view remains the same until enough data has arrived to display the new url.

    \sa url(), urlChanged()
*/

void QWebView::load(const QNetworkRequest &request, QNetworkAccessManager::Operation operation, const QByteArray &body)
{
    page()->mainFrame()->load(request, operation, body);
}

/*!
    Sets the content of the web view to the specified \a html.

    External objects such as stylesheets or images referenced in the HTML
    document are located relative to \a baseUrl.

    The \a html is loaded immediately; external objects are loaded asynchronously.

    When using this method, WebKit assumes that external resources such as
    JavaScript programs or style sheets are encoded in UTF-8 unless otherwise
    specified. For example, the encoding of an external script can be specified
    through the charset attribute of the HTML script tag. Alternatively, the
    encoding can also be specified by the web server.

    This is a convenience function equivalent to setContent(html, "text/html", baseUrl).

    \warning This function works only for HTML, for other mime types (i.e. XHTML, SVG)
    setContent() should be used instead.

    \sa load(), setContent(), QWebFrame::toHtml(), QWebFrame::setContent()
*/
void QWebView::setHtml(const QString &html, const QUrl &baseUrl)
{
    page()->mainFrame()->setHtml(html, baseUrl);
}

/*!
    Sets the content of the web view to the specified content \a data. If the \a mimeType argument
    is empty it is currently assumed that the content is HTML but in future versions we may introduce
    auto-detection.

    External objects referenced in the content are located relative to \a baseUrl.

    The \a data is loaded immediately; external objects are loaded asynchronously.

    \sa load(), setHtml(), QWebFrame::toHtml()
*/
void QWebView::setContent(const QByteArray &data, const QString &mimeType, const QUrl &baseUrl)
{
    page()->mainFrame()->setContent(data, mimeType, baseUrl);
}

/*!
    Returns a pointer to the view's history of navigated web pages.

    It is equivalent to

    \snippet webkitsnippets/qtwebkit_qwebview_snippet.cpp 0
*/
QWebHistory *QWebView::history() const
{
    return page()->history();
}

/*!
    Returns a pointer to the view/page specific settings object.

    It is equivalent to

    \snippet webkitsnippets/qtwebkit_qwebview_snippet.cpp 1

    \sa QWebSettings::globalSettings()
*/
QWebSettings *QWebView::settings() const
{
    return page()->settings();
}

/*!
    \property QWebView::title
    \brief the title of the web page currently viewed

    By default, this property contains an empty string.

    \sa titleChanged()
*/
QString QWebView::title() const
{
    if (d->page)
        return d->page->mainFrame()->title();
    return QString();
}

/*!
    \property QWebView::url
    \brief the url of the web page currently viewed

    Setting this property clears the view and loads the URL.

    By default, this property contains an empty, invalid URL.

    \sa load(), urlChanged()
*/

void QWebView::setUrl(const QUrl &url)
{
    page()->mainFrame()->setUrl(url);
}

QUrl QWebView::url() const
{
    if (d->page)
        return d->page->mainFrame()->url();
    return QUrl();
}

/*!
    \property QWebView::icon
    \brief the icon associated with the web page currently viewed

    By default, this property contains a null icon.

    \sa iconChanged(), QWebSettings::iconForUrl()
*/
QIcon QWebView::icon() const
{
    if (d->page)
        return d->page->mainFrame()->icon();
    return QIcon();
}

/*!
    \property QWebView::hasSelection
    \brief whether this page contains selected content or not.

    By default, this property is false.

    \sa selectionChanged()
*/
bool QWebView::hasSelection() const
{
    if (d->page)
        return d->page->hasSelection();
    return false;
}

/*!
    \property QWebView::selectedText
    \brief the text currently selected

    By default, this property contains an empty string.

    \sa findText(), selectionChanged(), selectedHtml()
*/
QString QWebView::selectedText() const
{
    if (d->page)
        return d->page->selectedText();
    return QString();
}

/*!
    \since 4.8
    \property QWebView::selectedHtml
    \brief the HTML currently selected

    By default, this property contains an empty string.

    \sa findText(), selectionChanged(), selectedText()
*/
QString QWebView::selectedHtml() const
{
    if (d->page)
        return d->page->selectedHtml();
    return QString();
}

#ifndef QT_NO_ACTION
/*!
    Returns a pointer to a QAction that encapsulates the specified web action \a action.
*/
QAction *QWebView::pageAction(QWebPage::WebAction action) const
{
    return page()->action(action);
}
#endif

/*!
    Triggers the specified \a action. If it is a checkable action the specified
    \a checked state is assumed.

    The following example triggers the copy action and therefore copies any
    selected text to the clipboard.

    \snippet webkitsnippets/qtwebkit_qwebview_snippet.cpp 2

    \sa pageAction()
*/
void QWebView::triggerPageAction(QWebPage::WebAction action, bool checked)
{
    page()->triggerAction(action, checked);
}

/*!
    \property QWebView::modified
    \brief whether the document was modified by the user

    Parts of HTML documents can be editable for example through the
    \c{contenteditable} attribute on HTML elements.

    By default, this property is false.
*/
bool QWebView::isModified() const
{
    if (d->page)
        return d->page->isModified();
    return false;
}

/*
Qt::TextInteractionFlags QWebView::textInteractionFlags() const
{
    // ### FIXME (add to page)
    return Qt::TextInteractionFlags();
}
*/

/*
    \property QWebView::textInteractionFlags
    \brief how the view should handle user input

    Specifies how the user can interact with the text on the page.
*/

/*
void QWebView::setTextInteractionFlags(Qt::TextInteractionFlags flags)
{
    Q_UNUSED(flags)
    // ### FIXME (add to page)
}
*/

/*!
    \reimp
*/
QSize QWebView::sizeHint() const
{
    return QSize(800, 600); // ####...
}

/*!
    \property QWebView::zoomFactor
    \since 4.5
    \brief the zoom factor for the view
*/

void QWebView::setZoomFactor(qreal factor)
{
    page()->mainFrame()->setZoomFactor(factor);
}

qreal QWebView::zoomFactor() const
{
    return page()->mainFrame()->zoomFactor();
}

/*!
  \property QWebView::textSizeMultiplier
  \brief the scaling factor for all text in the frame
  \obsolete

  Use setZoomFactor instead, in combination with the
  ZoomTextOnly attribute in QWebSettings.

  \note Setting this property also enables the
  ZoomTextOnly attribute in QWebSettings.

  By default, this property contains a value of 1.0.
*/

/*!
    Sets the value of the multiplier used to scale the text in a Web page to
    the \a factor specified.
*/
void QWebView::setTextSizeMultiplier(qreal factor)
{
    page()->mainFrame()->setTextSizeMultiplier(factor);
}

/*!
    Returns the value of the multiplier used to scale the text in a Web page.
*/
qreal QWebView::textSizeMultiplier() const
{
    return page()->mainFrame()->textSizeMultiplier();
}

/*!
    \property QWebView::renderHints
    \since 4.6
    \brief the default render hints for the view

    These hints are used to initialize QPainter before painting the Web page.

    QPainter::TextAntialiasing and QPainter::SmoothPixmapTransform are enabled by default.

    \sa QPainter::renderHints()
*/

/*!
    \since 4.6
    Returns the render hints used by the view to render content.

    \sa QPainter::renderHints()
*/
QPainter::RenderHints QWebView::renderHints() const
{
    return d->renderHints;
}

/*!
    \since 4.6
    Sets the render hints used by the view to the specified \a hints.

    \sa QPainter::setRenderHints()
*/
void QWebView::setRenderHints(QPainter::RenderHints hints)
{
    if (hints == d->renderHints)
        return;
    d->renderHints = hints;
    update();
}

/*!
    \since 4.6
    If \a enabled is true, enables the specified render \a hint; otherwise
    disables it.

    \sa renderHints, QPainter::renderHints()
*/
void QWebView::setRenderHint(QPainter::RenderHint hint, bool enabled)
{
    QPainter::RenderHints oldHints = d->renderHints;
    if (enabled)
        d->renderHints |= hint;
    else
        d->renderHints &= ~hint;
    if (oldHints != d->renderHints)
        update();
}


/*!
    Finds the specified string, \a subString, in the page, using the given \a options.

    If the HighlightAllOccurrences flag is passed, the function will highlight all occurrences
    that exist in the page. All subsequent calls will extend the highlight, rather than
    replace it, with occurrences of the new string.

    If the HighlightAllOccurrences flag is not passed, the function will select an occurrence
    and all subsequent calls will replace the current occurrence with the next one.

    To clear the selection, just pass an empty string.

    Returns true if \a subString was found; otherwise returns false.

    \sa selectedText(), selectionChanged()
*/
bool QWebView::findText(const QString &subString, QWebPage::FindFlags options)
{
    if (d->page)
        return d->page->findText(subString, options);
    return false;
}

/*! \reimp
*/
bool QWebView::event(QEvent *e)
{
    if (d->page) {
#ifndef QT_NO_CONTEXTMENU
        if (e->type() == QEvent::ContextMenu) {
            if (!isEnabled())
                return false;
            QContextMenuEvent *event = static_cast<QContextMenuEvent *>(e);
            if (d->page->swallowContextMenuEvent(event)) {
                e->accept();
                return true;
            }
            d->page->updatePositionDependentActions(event->pos());
        } else
#endif // QT_NO_CONTEXTMENU
        if (e->type() == QEvent::ShortcutOverride
            || e->type() == QEvent::Show
            || e->type() == QEvent::Hide) {
            d->page->event(e);
#ifndef QT_NO_CURSOR
        } else if (e->type() == QEvent::CursorChange) {
            // An unsetCursor will set the cursor to Qt::ArrowCursor.
            // Thus this cursor change might be a QWidget::unsetCursor()
            // If this is not the case and it came from WebCore, the
            // QWebPageClient already has set its cursor internally
            // to Qt::ArrowCursor, so updating the cursor is always
            // right, as it falls back to the last cursor set by
            // WebCore.
            // FIXME: Add a QEvent::CursorUnset or similar to Qt.
            if (cursor().shape() == Qt::ArrowCursor)
                d->page->d->client->resetCursor();
#endif
        } else if (e->type() == QEvent::TouchBegin 
            || e->type() == QEvent::TouchEnd
            || e->type() == QEvent::TouchUpdate
            || e->type() == QEvent::TouchCancel) {
            if (d->page->event(e))
                return true;
        } else if (e->type() == QEvent::Leave)
            d->page->event(e);
    }

    return QOpenGLWidget::event(e);
}

/*!
    Prints the main frame to the given \a printer.

    \sa QWebFrame::print(), QPrintPreviewDialog
*/
void QWebView::print(QPrinter *printer) const
{
#if !defined(QT_NO_PRINTER) && HAVE(QTPRINTSUPPORT)
    page()->mainFrame()->print(printer);
#endif
}

/*!
    Convenience slot that stops loading the document.

    It is equivalent to

    \snippet webkitsnippets/qtwebkit_qwebview_snippet.cpp 3

    \sa reload(), pageAction(), loadFinished()
*/
void QWebView::stop()
{
    if (d->page)
        d->page->triggerAction(QWebPage::Stop);
}

/*!
    Convenience slot that loads the previous document in the list of documents
    built by navigating links. Does nothing if there is no previous document.

    It is equivalent to

    \snippet webkitsnippets/qtwebkit_qwebview_snippet.cpp 4

    \sa forward(), pageAction()
*/
void QWebView::back()
{
    if (d->page)
        d->page->triggerAction(QWebPage::Back);
}

/*!
    Convenience slot that loads the next document in the list of documents
    built by navigating links. Does nothing if there is no next document.

    It is equivalent to

    \snippet webkitsnippets/qtwebkit_qwebview_snippet.cpp 5

    \sa back(), pageAction()
*/
void QWebView::forward()
{
    if (d->page)
        d->page->triggerAction(QWebPage::Forward);
}

/*!
    Reloads the current document.

    \sa stop(), pageAction(), loadStarted()
*/
void QWebView::reload()
{
    if (d->page)
        d->page->triggerAction(QWebPage::Reload);
}

static void* get_proc_from_context(void *c, fastuidraw::c_string proc_name)
{
  QOpenGLContext *ctx;
  void *f;

  ctx = static_cast<QOpenGLContext*>(c);
  f = reinterpret_cast<void*>(ctx->getProcAddress(proc_name));

  return f;
}

void QWebView::initializeGL(void)
{
    /* A GL Context must be current so that the GLContext
     * can be queried when creating the FastUIDraw resources
     */
    qFastUIDrawInitializeResources(context(), get_proc_from_context);
    d->m_painter = qFastUIDrawCreatePainter();
}

bool QWebView::drawWithFastUIDraw(void) const
{
    return d->m_drawWithFastUIDraw;
}

void QWebView::drawWithFastUIDraw(bool v)
{
    d->m_drawWithFastUIDraw = v;
}

void QWebView::resizeGL(int w, int h)
{
  if (d->page)
    d->page->setViewportSize(QSize(w, h));

  if (d->m_painter && d->m_surface)
    {
      fastuidraw::ivec2 dims(d->m_surface->dimensions());
      if (w != dims.x() || h != dims.y())
        {
          d->m_surface.clear();
        }
    }
}

void QWebView::paintGL(void)
{
  QPainter p(this);
  if (!d->m_drawWithFastUIDraw || !d->m_painter) {
      QWebFrame *frame = d->page->mainFrame();
      p.setRenderHints(d->renderHints);
    
      frame->render(&p);
      return;
  }

  p.beginNativePainting(); {
      /* endNativePainting() fails to restore the enable/disable on
       * GL_CLIP_DISTANCE, so we need to restore them here ourselves.
       */
      GLStateRestoreArray<GL_CLIP_DISTANCE0> clips(fastuidraw::gl::context_get<GLint>(GL_MAX_CLIP_DISTANCES));
      enum fastuidraw::Painter::screen_orientation orientation(fastuidraw::Painter::y_increases_downwards);
      GLuint fbo;

      fbo = defaultFramebufferObject();

      if (!d->m_surface) {
        int w(width()), h(height());
        fastuidraw::PainterBackend::Surface::Viewport vwp(0, 0, w, h);
        
        d->m_surface = FASTUIDRAWnew fastuidraw::gl::PainterBackendGL::SurfaceGL(fastuidraw::ivec2(w, h));
        d->m_surface->viewport(vwp);
      }
  
      fbo = defaultFramebufferObject();

      d->m_surface->clear_color(fastuidraw::vec4(0.0f, 0.5f, 0.5f, 1.0f));
      d->m_painter->begin(d->m_surface, orientation);
      d->m_painter->end();

      fastuidraw_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
      d->m_surface->blit_surface(GL_NEAREST);
  }
  p.endNativePainting();
}

/*!
    This function is called from the createWindow() method of the associated QWebPage,
    each time the page wants to create a new window of the given \a type. This might
    be the result, for example, of a JavaScript request to open a document in a new window.

    \note If the createWindow() method of the associated page is reimplemented, this
    method is not called, unless explicitly done so in the reimplementation.

    \note In the cases when the window creation is being triggered by JavaScript, apart from
    reimplementing this method application must also set the JavaScriptCanOpenWindows attribute
    of QWebSettings to true in order for it to get called.

    \sa QWebPage::createWindow(), QWebPage::acceptNavigationRequest()
*/
QWebView *QWebView::createWindow(QWebPage::WebWindowType type)
{
    Q_UNUSED(type)
    return 0;
}

/*! \reimp
*/
void QWebView::mouseMoveEvent(QMouseEvent* ev)
{
    if (d->page) {
        const bool accepted = ev->isAccepted();
        d->page->event(ev);
        ev->setAccepted(accepted);
    }
    if (!ev->isAccepted())
      QOpenGLWidget::mouseMoveEvent(ev);
}

/*! \reimp
*/
void QWebView::mousePressEvent(QMouseEvent* ev)
{
    if (d->page) {
        const bool accepted = ev->isAccepted();
        d->page->event(ev);
        ev->setAccepted(accepted);
    }
    if (!ev->isAccepted())
        QOpenGLWidget::mousePressEvent(ev);
}

/*! \reimp
*/
void QWebView::mouseDoubleClickEvent(QMouseEvent* ev)
{
    if (d->page) {
        const bool accepted = ev->isAccepted();
        d->page->event(ev);
        ev->setAccepted(accepted);
    }
    if (!ev->isAccepted())
        QOpenGLWidget::mouseDoubleClickEvent(ev);
}

/*! \reimp
*/
void QWebView::mouseReleaseEvent(QMouseEvent* ev)
{
    if (d->page) {
        const bool accepted = ev->isAccepted();
        d->page->event(ev);
        ev->setAccepted(accepted);
    }
    if (!ev->isAccepted())
        QOpenGLWidget::mouseReleaseEvent(ev);
}

#ifndef QT_NO_CONTEXTMENU
/*! \reimp
*/
void QWebView::contextMenuEvent(QContextMenuEvent* ev)
{
    if (d->page) {
        const bool accepted = ev->isAccepted();
        d->page->event(ev);
        ev->setAccepted(accepted);
    }
    if (!ev->isAccepted())
        QOpenGLWidget::contextMenuEvent(ev);
}
#endif // QT_NO_CONTEXTMENU

#ifndef QT_NO_WHEELEVENT
/*! \reimp
*/
void QWebView::wheelEvent(QWheelEvent* ev)
{
    if (d->page) {
        const bool accepted = ev->isAccepted();
        d->page->event(ev);
        ev->setAccepted(accepted);
    }
    if (!ev->isAccepted())
        QOpenGLWidget::wheelEvent(ev);
}
#endif // QT_NO_WHEELEVENT

/*! \reimp
*/
void QWebView::keyPressEvent(QKeyEvent* ev)
{
    if (d->page)
        d->page->event(ev);
    if (!ev->isAccepted())
        QOpenGLWidget::keyPressEvent(ev);
}

/*! \reimp
*/
void QWebView::keyReleaseEvent(QKeyEvent* ev)
{
    if (d->page)
        d->page->event(ev);
    if (!ev->isAccepted())
        QOpenGLWidget::keyReleaseEvent(ev);
}

/*! \reimp
*/
void QWebView::focusInEvent(QFocusEvent* ev)
{
    if (d->page)
        d->page->event(ev);
    else
        QOpenGLWidget::focusInEvent(ev);
}

/*! \reimp
*/
void QWebView::focusOutEvent(QFocusEvent* ev)
{
    if (d->page)
        d->page->event(ev);
    else
        QOpenGLWidget::focusOutEvent(ev);
}

/*! \reimp
*/
void QWebView::dragEnterEvent(QDragEnterEvent* ev)
{
#if ENABLE(DRAG_SUPPORT)
    if (d->page)
        d->page->event(ev);
#endif
    if (!ev->isAccepted())
        QOpenGLWidget::dragEnterEvent(ev);
}

/*! \reimp
*/
void QWebView::dragLeaveEvent(QDragLeaveEvent* ev)
{
#if ENABLE(DRAG_SUPPORT)
    if (d->page)
        d->page->event(ev);
#endif
    if (!ev->isAccepted())
        QOpenGLWidget::dragLeaveEvent(ev);
}

/*! \reimp
*/
void QWebView::dragMoveEvent(QDragMoveEvent* ev)
{
#if ENABLE(DRAG_SUPPORT)
    if (d->page)
        d->page->event(ev);
#endif
    if (!ev->isAccepted())
        QOpenGLWidget::dragMoveEvent(ev);
}

/*! \reimp
*/
void QWebView::dropEvent(QDropEvent* ev)
{
#if ENABLE(DRAG_SUPPORT)
    if (d->page)
        d->page->event(ev);
#endif
    if (!ev->isAccepted())
        QOpenGLWidget::dropEvent(ev);
}

/*! \reimp
*/
bool QWebView::focusNextPrevChild(bool next)
{
    if (d->page && d->page->focusNextPrevChild(next))
        return true;
    return QOpenGLWidget::focusNextPrevChild(next);
}

/*!\reimp
*/
QVariant QWebView::inputMethodQuery(Qt::InputMethodQuery property) const
{
    if (d->page)
        return d->page->inputMethodQuery(property);
    return QVariant();
}

/*!\reimp
*/
void QWebView::inputMethodEvent(QInputMethodEvent *e)
{
    if (d->page)
        d->page->event(e);
}

/*!\reimp
*/
void QWebView::changeEvent(QEvent *e)
{
    if (d->page && e->type() == QEvent::PaletteChange)
        d->page->setPalette(palette());
    QOpenGLWidget::changeEvent(e);
}

/*!
    \fn void QWebView::titleChanged(const QString &title)

    This signal is emitted whenever the \a title of the main frame changes.

    \sa title()
*/

/*!
    \fn void QWebView::urlChanged(const QUrl &url)

    This signal is emitted when the \a url of the view changes.

    \sa url(), load()
*/

/*!
    \fn void QWebView::statusBarMessage(const QString& text)

    This signal is emitted when the status bar \a text is changed by the page.
*/

/*!
    \fn void QWebView::iconChanged()

    This signal is emitted whenever the icon of the page is loaded or changes.

    In order for icons to be loaded, you will need to set an icon database path
    using QWebSettings::setIconDatabasePath().

    \sa icon(), QWebSettings::setIconDatabasePath()
*/

/*!
    \fn void QWebView::loadStarted()

    This signal is emitted when a new load of the page is started.

    \sa loadProgress(), loadFinished()
*/

/*!
    \fn void QWebView::loadFinished(bool ok)

    This signal is emitted when a load of the page is finished.
    \a ok will indicate whether the load was successful or any error occurred.

    \sa loadStarted()
*/

/*!
    \fn void QWebView::selectionChanged()

    This signal is emitted whenever the selection changes.

    \sa selectedText()
*/

/*!
    \fn void QWebView::loadProgress(int progress)

    This signal is emitted every time an element in the web page
    completes loading and the overall loading progress advances.

    This signal tracks the progress of all child frames.

    The current value is provided by \a progress and scales from 0 to 100,
    which is the default range of QProgressBar.

    \sa loadStarted(), loadFinished()
*/

/*!
    \fn void QWebView::linkClicked(const QUrl &url)

    This signal is emitted whenever the user clicks on a link and the page's linkDelegationPolicy
    property is set to delegate the link handling for the specified \a url.

    \sa QWebPage::linkDelegationPolicy()
*/

#include "moc_qwebview.cpp"

