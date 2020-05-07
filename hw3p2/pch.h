// pch.h: This is a precompiled header file.
// Files listed below are compiled only once, improving build performance for future builds.
// This also affects IntelliSense performance, including code completion and many code browsing features.
// However, files listed here are ALL re-compiled if any one of them is updated between builds.
// Do not add files here that you will be updating frequently as this negates the performance advantage.

#ifndef PCH_H
#define PCH_H

#include <windows.h>

#include <iostream>
#include <chrono>
#include <cassert>

#include "Constants.h"
#include "Headers.h"
#include "StatsManager.h"
#include "SenderSocket.h"
#include "Checksum.h"

#define _CRTDBG_MAP_ALLOC  
#include <stdlib.h>  
#include <crtdbg.h> // libraries to check for memory leaks

#endif //PCH_H
