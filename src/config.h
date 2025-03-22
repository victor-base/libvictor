/**
 * @file config.h
 * @brief Configuration header file for platform-specific macros and settings.
 *
 * This file defines macros and settings that are specific to the operating
 * system being used (Windows, macOS, or Linux). It also includes common
 * configurations such as buffer size, debug mode, and export directives.
 *
 * @note Ensure that the appropriate macros are defined based on the target
 *       operating system during compilation.
 *
 * Macros:
 * - OS_WINDOWS: Defined if the target OS is Windows.
 * - OS_MAC: Defined if the target OS is macOS.
 * - OS_LINUX: Defined if the target OS is Linux.
 * - SHARED_LIB_EXTENSION: File extension for shared libraries (.dll, .dylib, .so).
 * - INLINE: Inline keyword definition based on the platform.
 * - EXPORT: Export directive for shared libraries (specific to Windows).
 * - MAX_BUFFER_SIZE: Maximum buffer size (default: 1024).
 * - DEBUG_MODE: Debug mode flag (default: enabled with value 1).
 *
 * Platform-Specific Includes:
 * - Windows: Includes <windows.h>.
 * - macOS/Linux: Includes <unistd.h>.
 */

//Este archivo TIENE QUE SER REFERENCIADO EN TODOS LOS FUENTE (.c) Mediante #INCLUDE<config.h>
//This file MUST BE INCLUDED IN ALL SOURCES FILES (.c) via #INCLUDE<config.h>

#ifndef CONFIG_H
#define CONFIG_H

#ifdef _WIN32   

#define OS_WINDOWS 1
#define SHARED_LIB_EXTENSION ".dll"
#define INLINE __inline

#elif defined(__APPLE__)

#define OS_MAC 1
#define SHARED_LIB_EXTENSION ".dylib"
#define INLINE inline
#include <unistd.h>
#define EXPORT

#else

#define OS_LINUX 1
#define SHARED_LIB_EXTENSION ".so"
#define INLINE inline

#endif

#ifdef OS_WINDOWS

#include <windows.h>
#define EXPORT __declspec(dllexport)

#else

#include <unistd.h>
#define EXPORT

#endif

#define MAX_BUFFER_SIZE 1024
#define DEBUG_MODE 1

#endif
