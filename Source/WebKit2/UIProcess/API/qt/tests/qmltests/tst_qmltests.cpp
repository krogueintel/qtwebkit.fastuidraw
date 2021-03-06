/*
    Copyright (C) 2011 Nokia Corporation and/or its subsidiary(-ies)

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

#include "../bytearraytestdata.h"
#include "../util.h"

#include "private/qquickwebview_p.h"
#include <QVarLengthArray>
#include <QtQuickTest/quicktest.h>

int main(int argc, char** argv)
{
    QVarLengthArray<char*, 8> arguments;
    for (int i = 0; i < argc; ++i)
        arguments.append(argv[i]);

    arguments.append(const_cast<char*>("-import"));
    arguments.append(const_cast<char*>(IMPORT_DIR));

    argc = arguments.count();
    argv = arguments.data();

    suppressDebugOutput();
    addQtWebProcessToPath();

    qmlRegisterType<ByteArrayTestData>("Test", 1, 0, "ByteArrayTestData");

#ifdef DISABLE_FLICKABLE_VIEWPORT
    QQuickWebViewExperimental::setFlickableViewportEnabled(false);
#endif
    return quick_test_main(argc, argv, "qmltests", QUICK_TEST_SOURCE_DIR);
}
