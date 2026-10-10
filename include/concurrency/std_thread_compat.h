#ifndef STD_THREAD_COMPAT_H
#define STD_THREAD_COMPAT_H

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00  // Windows 10+; current cpp-httplib refuses anything older
#endif

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX            // keep windows.h from defining min()/max() macros
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN  // keep windows.h from pulling in the old winsock.h
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <io.h>

#ifndef CONDITION_VARIABLE_INIT
#define CONDITION_VARIABLE_INIT {0}
#endif
#ifndef CONDITION_VARIABLE_LOCKMODE_SHARED
#define CONDITION_VARIABLE_LOCKMODE_SHARED 0x00000001
#endif
#ifndef IPV6_V6ONLY
#define IPV6_V6ONLY 27
#endif
#ifndef _access_s
#define _access_s(path, mode) _access(path, mode)
#endif
#endif

#if defined(__MINGW32__) && !defined(_GLIBCXX_HAS_GTHREADS)
#include "mingw.thread.h"
#include "mingw.mutex.h"
#include "mingw.condition_variable.h"
#else
#include <thread>
#include <mutex>
#include <condition_variable>
#endif

#endif // STD_THREAD_COMPAT_H