SET(PROJECT_VERSION_MAJOR 0)
SET(PROJECT_VERSION_MINOR 1)
SET(PROJECT_VERSION_PATCH 0)
SET(PROJECT_VERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH})

# -----------------------------------------------------------------------------
# We mention Safari version because many sites check for it.
# Sync with Source/WebCore/Configurations/Version.xcconfig whenever Safari is
# version up.
# -----------------------------------------------------------------------------
SET(WEBKIT_USER_AGENT_MAJOR_VERSION 534)
SET(WEBKIT_USER_AGENT_MINOR_VERSION 16)

ADD_DEFINITIONS(-DWTF_PLATFORM_EFL=1)
SET(WTF_PLATFORM_EFL 1)

# -----------------------------------------------------------------------------
# Determine which font backend will be used
# -----------------------------------------------------------------------------
SET(ALL_FONT_BACKENDS freetype pango)
SET(FONT_BACKEND "freetype" CACHE STRING "choose which network backend to use (one of ${ALL_FONT_BACKENDS})")

FIND_PACKAGE(Cairo 1.10 REQUIRED)
FIND_PACKAGE(EFL REQUIRED)
FIND_PACKAGE(Fontconfig 2.8.0 REQUIRED)
FIND_PACKAGE(Sqlite REQUIRED)
FIND_PACKAGE(LibXml2 2.6 REQUIRED)
FIND_PACKAGE(LibXslt 1.1.7 REQUIRED)
FIND_PACKAGE(ICU REQUIRED)
FIND_PACKAGE(Threads REQUIRED)
FIND_PACKAGE(JPEG REQUIRED)
FIND_PACKAGE(PNG REQUIRED)
FIND_PACKAGE(ZLIB REQUIRED)

FIND_PACKAGE(Glib 2.31.8 REQUIRED)
FIND_PACKAGE(Gthread REQUIRED)
FIND_PACKAGE(LibSoup2 2.37.92 REQUIRED)
SET(ENABLE_GLIB_SUPPORT ON)

SET(WTF_USE_SOUP 1)
ADD_DEFINITIONS(-DWTF_USE_SOUP=1)

ADD_DEFINITIONS(-DENABLE_CONTEXT_MENUS=0)

SET(WTF_USE_PTHREADS 1)
ADD_DEFINITIONS(-DWTF_USE_PTHREADS=1)

SET(WTF_USE_ICU_UNICODE 1)
ADD_DEFINITIONS(-DWTF_USE_ICU_UNICODE=1)

SET(WTF_USE_CAIRO 1)
ADD_DEFINITIONS(-DWTF_USE_CAIRO=1)

SET(JSC_EXECUTABLE_NAME jsc)

SET(WTF_LIBRARY_NAME wtf_efl)
SET(JavaScriptCore_LIBRARY_NAME javascriptcore_efl)
SET(WebCore_LIBRARY_NAME webcore_efl)
SET(WebKit_LIBRARY_NAME ewebkit)
SET(WebKit2_LIBRARY_NAME ewebkit2)

SET(DATA_INSTALL_DIR "share/${WebKit_LIBRARY_NAME}-${PROJECT_VERSION_MAJOR}" CACHE PATH "Installation path for theme data")
SET(THEME_BINARY_DIR ${CMAKE_BINARY_DIR}/WebKit/efl/DefaultTheme)

SET(VERSION_SCRIPT "-Wl,--version-script,${CMAKE_MODULE_PATH}/eflsymbols.filter")

WEBKIT_OPTION_BEGIN()
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_BATTERY_STATUS ON)
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_DRAG_SUPPORT ON)
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_FAST_MOBILE_SCROLLING ON)
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_FILTERS ON)
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_GLIB_SUPPORT ON)
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_INPUT_TYPE_COLOR ON)
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_NETSCAPE_PLUGIN_API OFF)
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_NETWORK_INFO ON)
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_PAGE_VISIBILITY_API ON)
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_REQUEST_ANIMATION_FRAME ON)
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_SHARED_WORKERS ON)
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_VIBRATION ON)
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_VIDEO ON)
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_VIDEO_TRACK ON)
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_WEB_AUDIO ON)
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_WEB_TIMING ON)
WEBKIT_OPTION_DEFAULT_PORT_VALUE(ENABLE_WORKERS ON)
WEBKIT_OPTION_END()

OPTION(ENABLE_ECORE_X "Enable Ecore_X specific usage (cursor, bell)" ON)
IF (ENABLE_ECORE_X)
    IF (ECORE_X_FOUND)
        MESSAGE(STATUS "Using Ecore-X to provide extended support.")
        ADD_DEFINITIONS(-DHAVE_ECORE_X)
    ELSE ()
        MESSAGE(ERROR "Requested Ecore-X but it was not found!")
    ENDIF ()
ENDIF ()

IF (FONT_BACKEND STREQUAL "freetype")
  FIND_PACKAGE(Freetype REQUIRED)
  SET(WTF_USE_FREETYPE 1)
  ADD_DEFINITIONS(-DWTF_USE_FREETYPE=1)
ELSE ()
  FIND_PACKAGE(Pango REQUIRED)
  SET(WTF_USE_PANGO 1)
  ADD_DEFINITIONS(-DWTF_USE_PANGO=1)
ENDIF ()

IF (NOT ENABLE_SVG)
  SET(ENABLE_SVG_FONTS 0)
ENDIF ()

IF (ENABLE_VIDEO OR ENABLE_WEB_AUDIO)
    SET(GSTREAMER_COMPONENTS app interfaces pbutils)
    SET(WTF_USE_GSTREAMER 1)
    ADD_DEFINITIONS(-DWTF_USE_GSTREAMER=1)

    IF (ENABLE_VIDEO)
        LIST(APPEND GSTREAMER_COMPONENTS video)
    ENDIF()

    IF (ENABLE_WEB_AUDIO)
        LIST(APPEND GSTREAMER_COMPONENTS audio fft)
        ADD_DEFINITIONS(-DWTF_USE_WEBAUDIO_GSTREAMER=1)
    ENDIF ()

    FIND_PACKAGE(GStreamer REQUIRED COMPONENTS ${GSTREAMER_COMPONENTS})
ENDIF ()

IF (ENABLE_WEBGL)
  FIND_PACKAGE(OpenGL REQUIRED)
ENDIF ()

SET(CPACK_SOURCE_GENERATOR TBZ2)