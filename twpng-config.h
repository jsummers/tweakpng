// twpng-config.h

#ifndef TWPNG_CONFIG_H
#define TWPNG_CONFIG_H

#ifdef UNICODE
#define _WIN32_WINNT   0x0500
#else
#define WINVER         0x0410
#define _WIN32_WINDOWS 0x0410
#endif

#include <SDKDDKVer.h>

// Comment out the next line to compile without image viewing support.
#define TWPNG_SUPPORT_VIEWER

// Comment out the next line to compile without zlib.
// (Required for image viewing, and editing compressed text chunks.)
#define TWPNG_USE_ZLIB

#if defined(TWPNG_USE_ZLIB) || defined(TWPNG_SUPPORT_VIEWER)
#define TWPNG_HAVE_ZLIB
#endif

#endif // TWPNG_CONFIG_H
