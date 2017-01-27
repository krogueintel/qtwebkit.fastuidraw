//
// Copyright 2015 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

// Platform.cpp: Implementation methods for angle::Platform.

#include <platform/Platform.h>

#include "common/debug.h"

namespace
{
angle::Platform *currentPlatform = nullptr;
}

// static
angle::Platform *ANGLEPlatformCurrent()
{
    return currentPlatform;
}

// static
void ANGLEPlatformInitialize(angle::Platform *platformImpl)
{
    ASSERT(platformImpl != nullptr);
    currentPlatform = platformImpl;
}

// static
void ANGLEPlatformShutdown()
{
    currentPlatform = nullptr;
}
