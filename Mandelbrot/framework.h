// header.h : include file for standard system include files,
// or project specific include files
//

#pragma once

#include "targetver.h"
#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
// Windows Header Files
#include <iostream>
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <Objbase.h>
#define _CRTDBG_MAP_ALLOC //to get more details
#include <stdlib.h>  
#include <crtdbg.h>   //for malloc and free

#include <malloc.h>
#include <memory.h>
#include <tchar.h>
#include <strsafe.h>
// C++ RunTime Header Files
#include <string>
using namespace::std;
#include <commdlg.h>
