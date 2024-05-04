// Mandelbrot.cpp : Defines the entry point for the application.
//
// Draws the Mandelbrot set. Supports click to change orgin, drag
// to zoom in, and mouse wheel zoom in and out about a point. The
// user can hold the shift key down while dragging the window
// borders to suppress the delay for repainting along the way.
// Supports backtracking the axis coordinate range via right click.
//
// Also, saves and restores the WindowPlacement, in the registry,
// between executions.
//
// The user has the option of selecting the old FPU logic, or the
// new TTMath library from ttmath.org. This new library provides
// extended precision floating point arithmetic (among other things)
// and is used to see if the resolution of the graphics improves.
// (It does not seem to improve things, but time will tell.)
// 
// Slices up the graphics workload between 12 threads. The machine
// used for development and testing has 10 cores, and 12 logical
// processors, hence the choice of 12 threads. We can specify a
// somewhat arbitrary number of threads, and performance vs the
// number of threads is a little erratic, possibly due to latency
// and/or overhead in the OS. I am seeing a factor of 8 to 10
// improvement with 12 threads.
//
// The number of slices defaults to 5000. In the first version
// the number of slices (12) and the number of threads was the
// same. As the threads run at different speeds, due to the
// amount of black space per slice, the current version uses a
// thread pool where each thread iterates while there is more
// work to do. The WorkQueue class facilitates this pool. This
// way, performance is maximized as no thread completes until
// all threads are completed. Each slice is assigned to the
// next thread in the pool. When the thread finishes with that
// slice, it getes a new slice from the work queue. All threads
// in the pool compete for slices on a first come first serve
// basis. When a thread attempts to get a new slice and there
// are no more, the thread exits. When all threads exit, the
// bitmap is painted to the screen.
// 
// Also, I implemented an HSV/RGB color scheme with a triple log
// scaling factor to get better color results. This is courtesy
// of the Windows 11 Copilot (Preview), which was an immense help.
// The user is able to select the RGB system or the HSV system.
// 
// The number of slices is adjustable, as are the X and Y axes
// ranges. The user can choose to see the axes on the final plot.
// The max iterations can also be adjusted, at the cost of speed.
//
// Added modeless progress dialog box. Gave the user the ability
// to abort with ESCAPE.
// 
// The user can save and restore the plot parameters to and from
// a file using standard File Save and File Open sequences.
// 
// Compilation requires that UNICODE be defined. Some of the
// choices made in code, mainly wstring, do not support detecting
// UNICODE vs non-UNICODE, so don't compile without UNICODE defined.
//
// In case you get linker errors, be sure to include version.lib
// in the linker input line. (This should not be an issue if you
// start with a clone of the repository, which will copy the
// correct Project Properties.)
// 
// Microsoft Visual Studio 2022 Community Edition 64 Bit 17.9.5
//
// Alex Sokolek, Version 1.0.0.1, Copyright (c) March 27, 2024
// 
// Version 1.0.0.2 - April 1, 2024 - Added worker thread pool.
//
// Version 1.0.0.3 - April 17, 2024 - Updated version for release.
//
// Version 1.0.0.4 - April 20, 2024 - Updated comments.
//
// Version 1.0.0.5 - April 21, 2024 - Limited mouse capture range.
//
// Version 1.0.0.6 - April 25, 2024 - Added support for the TTMath library.
//
// Version 1.0.0.7 - April 28, 2024 - Added modeless progress dialog box.
//
// Version 1.0.0.8 - May 4, 2024 - Abort logic and wq dtor bug.
//

#include "framework.h"
#include "Mandelbrot.h"

#include "ApplicationRegistry.h"
#include "QuadDoubleStack.h"
#include "WorkQueue.h"
#include "HSVtoRGB.h"

#include "ttmath.h"

#define MAX_LOADSTRING 100

// Global Variables:
HINSTANCE hInst;                                // current instance
WCHAR szTitle[MAX_LOADSTRING];                  // The title bar text
WCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name

static double    dxMin          = -2.0;
static double    dxMax          = 0.47;
static double    dyMin          = -1.12;
static double    dyMax          = 1.12;
static int       Iterations     = 1000;
static int       Slices         = 5000;
static int       Threads        = 12;
static BOOL      bShowAxes      = false;
static BOOL      bUseHSV        = true;
static BOOL      bUseTTMath     = false;
HWND             hWndProgress;

QuadDoubleStack* qds;
WorkQueue*       wq;

// Forward declarations of functions included in this code module:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
void                ErrorHandler(LPTSTR);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    Parameters(HWND, UINT, WPARAM, LPARAM);
TCHAR*              dTos(DOUBLE);
TCHAR*              iTos(int);
DWORD WINAPI        MandelbrotWorkerThread(LPVOID lpParam);
uint32_t            ReverseRGBBytes(uint32_t);
INT_PTR CALLBACK    MDBoxProc(HWND, UINT, WPARAM, LPARAM);



int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
					 _In_opt_ HINSTANCE hPrevInstance,
					 _In_ LPWSTR    lpCmdLine,
					 _In_ int       nCmdShow)
{
	// Setup to check for memory leaks. (Debug only.)
	#ifdef _DEBUG
	_CrtMemState sOld;
	_CrtMemState sNew;
	_CrtMemState sDiff;
	_CrtMemCheckpoint(&sOld); //take a snapshot
	#endif // _DEBUG

	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);

	// Initialize global strings
	LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
	LoadStringW(hInstance, IDC_MANDELBROT, szWindowClass, MAX_LOADSTRING);

	// Allow only one instance of this application to run at the same time.
	// If not, find and activate the other window and then exit.
	HANDLE hMutex = CreateMutex(NULL, false, _T("{F6D57AC3-1B60-4E1B-85DF-0925A7A58D25}"));
	if (hMutex == NULL || hMutex == (HANDLE)ERROR_INVALID_HANDLE || GetLastError() == ERROR_ALREADY_EXISTS)
	{
		MessageBox(NULL, _T("ERROR: Unable to create mutex!\n\nAnother instance is probably running."), szTitle, MB_OK | MB_ICONSTOP);
		HWND hWnd = FindWindow(NULL, szTitle);
		if (hWnd) PostMessage(hWnd, WM_ACTIVATE, WA_ACTIVE, NULL);

		return false;
	}

	MyRegisterClass(hInstance);

	// Perform application initialization:
	if (!InitInstance (hInstance, nCmdShow))
	{
		return FALSE;
	}

	HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_MANDELBROT));

	// Instantiate the recall stack.
	qds = new QuadDoubleStack;
	if (qds == NULL) ExitProcess(2);

	MSG msg;

	// Main message loop:
	while (GetMessage(&msg, nullptr, 0, 0))
	{
		if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}

	// Delete the recall stack.
	delete qds;

	// Dismiss and close the mutex acquired at the beginning of execution.
	CloseHandle(hMutex);

	// Check for memory leaks. (Debug only.)
	#ifdef _DEBUG
	_CrtMemCheckpoint(&sNew); //take a snapshot 
	if (_CrtMemDifference(&sDiff, &sOld, &sNew)) // if there is a difference
	{
		MessageBeep(MB_ICONEXCLAMATION);
		MessageBox(NULL, _T("MEMORY LEAK(S) DETECTED!\n\nSee debug log."), szTitle, MB_OK | MB_ICONEXCLAMATION);
		OutputDebugString(L"-----------_CrtMemDumpStatistics ---------\n");
		_CrtMemDumpStatistics(&sDiff);
		OutputDebugString(L"-----------_CrtMemDumpAllObjectsSince ---------\n");
		_CrtMemDumpAllObjectsSince(&sOld);
		OutputDebugString(L"-----------_CrtDumpMemoryLeaks ---------\n");
		_CrtDumpMemoryLeaks();
	}
	#endif // _DEBUG

	return (int) msg.wParam;
}



//
//  FUNCTION: MyRegisterClass()
//
//  PURPOSE: Registers the window class.
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
	WNDCLASSEXW wcex;

	wcex.cbSize = sizeof(WNDCLASSEX);

	wcex.style          = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc    = WndProc;
	wcex.cbClsExtra     = 0;
	wcex.cbWndExtra     = 0;
	wcex.hInstance      = hInstance;
	wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_MANDELBROT));
	wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
	wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
	wcex.lpszMenuName   = MAKEINTRESOURCEW(IDC_MANDELBROT);
	wcex.lpszClassName  = szWindowClass;
	wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

	return RegisterClassExW(&wcex);
}



//
//   FUNCTION: InitInstance(HINSTANCE, int)
//
//   PURPOSE: Saves instance handle and creates main window
//
//   COMMENTS:
//
//        In this function, we save the instance handle in a global variable and
//        create and display the main program window. We also load the last
//        saved WindowPlacement from the registry.
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
	hInst = hInstance; // Store instance handle in our global variable

	// Create main window.
	HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
		                      0, CW_USEDEFAULT, 0, nullptr, nullptr, hInstance, nullptr);

	// Abort if failure.
	if (!hWnd)
	{
	   return FALSE;
	}

	ShowWindow(hWnd, nCmdShow);

	// Initialize ApplicationRegistry class and load/restore window placement from the registry
	ApplicationRegistry* pAppReg = new ApplicationRegistry;
	if (pAppReg->Init(hWnd))
	{
		WINDOWPLACEMENT wp;
		if (pAppReg->LoadMemoryBlock(_T("WindowPlacement"), (LPBYTE)&wp, sizeof(wp)))
		{
			if (wp.flags == 0 && wp.showCmd == SW_MINIMIZE) wp.flags = WPF_SETMINPOSITION;

			// This is done after ShowWindow() and before UpdateWindow() so that
			// the first display of the window is in the "as recorded" state, i.e.
			// we don't want two paint messages to generate the Mandelbrot Set twice.
			SetWindowPlacement(hWnd, &wp);
		}
	}
	delete pAppReg;

	UpdateWindow(hWnd);

	return TRUE;
}



//
//  FUNCTION: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  PURPOSE: Processes messages for the main window.
//
//  WM_COMMAND  - process the application menu
//  WM_PAINT    - Paint the main window
//  WM_DESTROY  - post a quit message and return
//
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	// Persistent variables that are shared between various WndProc functions.
	static BOOL bHoldPainting = false;
	static BOOL bPaintingHeld = false;
	static double dxMouse;
	static double dyMouse;
	static double dMouseMagnify;
	static int iMouseDelta;
	static TEXTMETRIC tm;
	static int PixelStepSize = 0;
	static BOOL LeftMouseButtonDown = false;
	static POINT MouseDragOrigin;
	static POINT MouseDragDestination;
	static HDC dc;
	TCHAR szProgress[100];

	// Persistent bitmap support variables.
	static BITMAPINFOHEADER bmih = { 0 };
	bmih.biSize = sizeof(BITMAPINFOHEADER);
	bmih.biWidth = 0;
	bmih.biHeight = 0;
	bmih.biPlanes = 1;
	bmih.biBitCount = 32;
	bmih.biCompression = BI_RGB;
	bmih.biSizeImage = 0;
	bmih.biXPelsPerMeter = 10;
	bmih.biYPelsPerMeter = 10;
	static BITMAPINFO dbmi = { 0 };
	static uint32_t* BitmapData = NULL;

	switch (message)
	{

	case WM_INITDIALOG:
		return true;

	case WM_COMMAND:
	{
		int wmId = LOWORD(wParam);
		// Parse the menu selections:
		switch (wmId)
		{
		case IDM_ABOUT:
			DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
			break;
		case IDM_PARAMETERS:
			DialogBox(hInst, MAKEINTRESOURCE(IDD_PARAMETERS), hWnd, Parameters);
			InvalidateRect(hWnd, NULL, true);
			break;
		case IDM_EXIT:
			DestroyWindow(hWnd);
			break;

		case ID_FILE_OPEN:
		{
			// Temporary copy of parameters to load.
			double dxMinTemp;
			double dxMaxTemp;
			double dyMinTemp;
			double dyMaxTemp;
			int    IterationsTemp;
			int    SlicesTemp;
			int    ThreadsTemp;
			BOOL   bShowAxesTemp;
			BOOL   bUseHSVTemp;
			BOOL   bUseTTMathTemp;

			// Prepare to call GetOpenFileName().
			OPENFILENAME ofn;
			ZeroMemory(&ofn, sizeof(ofn));
			TCHAR* pszOpenFileName = new TCHAR[MAX_PATH];
			StringCchCopy(pszOpenFileName, MAX_PATH, _T("untitled.mbf"));
			ofn.lStructSize = sizeof(OPENFILENAME);
			ofn.hwndOwner = hWnd;
			ofn.lpstrFilter = _T("MandelBrot Files (*.mbf)\0*.mbf\0All Files (*.*)\0*.*\0");
			ofn.lpstrFile = pszOpenFileName;
			ofn.nMaxFile = MAX_PATH;
			ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

			if (!GetOpenFileName(&ofn)) // Retrieves the fully qualified file name that was selected.
			{
				if (CommDlgExtendedError() == 0) // Case of user clicked cancel.
				{
					delete[] pszOpenFileName;
					break;
				}
				ErrorHandler((LPTSTR)_T("GetOpenFileName")); // Case of some other error.
				break;
			}

			// Open selected file.
			HANDLE hFile;
			hFile = CreateFile(pszOpenFileName, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			if (!hFile)
			{
				ErrorHandler((LPTSTR)_T("CreateFile"));
				delete[] pszOpenFileName;
				break;
			}

			// Read each temporary variable in binary from the file.
			BOOL bReadOK = true;
			if (bReadOK) bReadOK = ReadFile(hFile, &dxMinTemp,      sizeof(dxMinTemp),      NULL, NULL);
			if (bReadOK) bReadOK = ReadFile(hFile, &dxMaxTemp,      sizeof(dxMaxTemp),      NULL, NULL);
			if (bReadOK) bReadOK = ReadFile(hFile, &dyMinTemp,      sizeof(dyMinTemp),      NULL, NULL);
			if (bReadOK) bReadOK = ReadFile(hFile, &dyMaxTemp,      sizeof(dyMaxTemp),      NULL, NULL);
			if (bReadOK) bReadOK = ReadFile(hFile, &IterationsTemp, sizeof(IterationsTemp), NULL, NULL);
			if (bReadOK) bReadOK = ReadFile(hFile, &SlicesTemp,     sizeof(SlicesTemp),     NULL, NULL);
			if (bReadOK) bReadOK = ReadFile(hFile, &ThreadsTemp,    sizeof(ThreadsTemp),    NULL, NULL);
			if (bReadOK) bReadOK = ReadFile(hFile, &bShowAxesTemp,  sizeof(bShowAxesTemp),  NULL, NULL);
			if (bReadOK) bReadOK = ReadFile(hFile, &bUseHSVTemp,    sizeof(bUseHSVTemp),    NULL, NULL);
			if (bReadOK) bReadOK = ReadFile(hFile, &bUseTTMathTemp, sizeof(bUseTTMathTemp), NULL, NULL);

			CloseHandle(hFile);
			delete[] pszOpenFileName;

			// Validate the reads.
			if (!bReadOK                        ||
			    dxMinTemp          >= dxMaxTemp ||
				dyMinTemp          >= dyMaxTemp ||
				IterationsTemp     <  1         ||
				SlicesTemp         <  1         ||
				ThreadsTemp        <  1         ||
				ThreadsTemp        >  64          )
			{
				MessageBeep(MB_ICONEXCLAMATION);
				MessageBox(hWnd, _T("Bad .mbf file!"), _T("FileOpen"), MB_OK | MB_ICONEXCLAMATION);
				break;
			}
			
			// Save the coordinates in the recall stack.
			qds->push(hWnd, dxMin, dxMax, dyMin, dyMax);

			// Update the actual values from the temporary copies.
			dxMin          = dxMinTemp;
			dxMax          = dxMaxTemp;
			dyMin          = dyMinTemp;
			dyMax          = dyMaxTemp;
			Iterations     = IterationsTemp;
			Slices         = SlicesTemp;
			Threads        = ThreadsTemp;
			bShowAxes      = bShowAxesTemp;
			bUseHSV        = bUseHSVTemp;
			bUseTTMath     = bUseTTMathTemp;

			// Repaint the image.
			InvalidateRect(hWnd, NULL, true);
			break;
		}

		case ID_FILE_SAVE:
		{
			// Prepare to call GetSaveFileName.
			OPENFILENAME ofn;
			ZeroMemory(&ofn, sizeof(ofn));
			TCHAR* pszSaveFileName = new TCHAR[MAX_PATH];
			StringCchCopy(pszSaveFileName, MAX_PATH, _T("untitled.mbf"));
			ofn.lStructSize = sizeof(OPENFILENAME);
			ofn.hwndOwner = hWnd;
			ofn.lpstrFilter = _T("MandelBrot Files (*.mbf)\0*.mbf\0All Files (*.*)\0*.*\0");
			ofn.lpstrFile = pszSaveFileName;
			ofn.nMaxFile = MAX_PATH;
			ofn.Flags = OFN_OVERWRITEPROMPT;

			if (!GetSaveFileName(&ofn)) // Retrieves the fully qualified file name that was selected.
			{
				if (CommDlgExtendedError() == 0) // Case of user clicked cancel.
				{
					delete[] pszSaveFileName;
					break;
				}
				ErrorHandler((LPTSTR)_T("GetSaveFileName")); // Case of some other error.
				break;
			}

			// Open selected file.
			HANDLE hFile;
			hFile = CreateFile(pszSaveFileName, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
			// Case of some error.
			if (hFile == INVALID_HANDLE_VALUE)
			{
				ErrorHandler((LPTSTR)_T("CreateFile"));
				delete[] pszSaveFileName;
				break;
			}
			
			// Write each variable in binary to the file.
			BOOL bWriteOK = true;
			if (bWriteOK) bWriteOK = WriteFile(hFile, &dxMin,      sizeof(dxMin),      NULL, NULL);
			if (bWriteOK) bWriteOK = WriteFile(hFile, &dxMax,      sizeof(dxMax),      NULL, NULL);
			if (bWriteOK) bWriteOK = WriteFile(hFile, &dyMin,      sizeof(dyMin),      NULL, NULL);
			if (bWriteOK) bWriteOK = WriteFile(hFile, &dyMax,      sizeof(dyMax),      NULL, NULL);
			if (bWriteOK) bWriteOK = WriteFile(hFile, &Iterations, sizeof(Iterations), NULL, NULL);
			if (bWriteOK) bWriteOK = WriteFile(hFile, &Slices,     sizeof(Slices),     NULL, NULL);
			if (bWriteOK) bWriteOK = WriteFile(hFile, &Threads,    sizeof(Threads),    NULL, NULL);
			if (bWriteOK) bWriteOK = WriteFile(hFile, &bShowAxes,  sizeof(bShowAxes),  NULL, NULL);
			if (bWriteOK) bWriteOK = WriteFile(hFile, &bUseHSV,    sizeof(bUseHSV),    NULL, NULL);
			if (bWriteOK) bWriteOK = WriteFile(hFile, &bUseTTMath, sizeof(bUseTTMath), NULL, NULL);

			// Case of some error.
			if (!bWriteOK)
			{
				MessageBeep(MB_ICONEXCLAMATION);
				MessageBox(hWnd, _T("Error writing file"), _T("FILE_OPEN"), MB_OK | MB_ICONEXCLAMATION);
				CloseHandle(hFile);
				delete[] pszSaveFileName;
				break;
			}
			CloseHandle(hFile);
			delete[] pszSaveFileName;
			break;
		}

		default:
			return DefWindowProc(hWnd, message, wParam, lParam);
		}
	}
	break;

	case WM_PAINT:
	{
		if (bHoldPainting)
		{
			bPaintingHeld = true;
			break;
		}

		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hWnd, &ps);

		RECT rect;
		GetClientRect(hWnd, &rect);

		// Initialize bitmap header
		bmih.biWidth = rect.right;
		bmih.biHeight = -(rect.bottom - tm.tmHeight);
		dbmi.bmiHeader = bmih;

		GetTextMetrics(hdc, &tm);

		// Recreate bitmap with, possibly, a new size.
		if (BitmapData != NULL) delete[] BitmapData;
		BitmapData = new uint32_t[rect.right * (rect.bottom - tm.tmHeight)];
		if (BitmapData == NULL) ExitProcess(2);

		// Snapshot the start time.
		LARGE_INTEGER liFrequency, liStart, liEnd;
		QueryPerformanceFrequency(&liFrequency); // 10 megahertz.
		QueryPerformanceCounter(&liStart); // 100 nanosecond ticks.
		double dStart = (double)liStart.QuadPart / liFrequency.QuadPart; // Convert to seconds

		// Create the critical section mutex for the thread pool. (Initially owned by this thread.)
		HANDLE hcsMutex = CreateMutex(NULL, TRUE, _T("{4671AE3A-10E9-469D-A726-9A0C1AD5300D}"));

		// Setup to use the modeless dialog box to display progress.
		dc = GetDC(hWndProgress);
		RECT WindowRect;
		GetWindowRect(hWnd, &WindowRect);
		ShowWindow(hWndProgress, SW_SHOW);
		SetWindowPos(hWndProgress, HWND_NOTOPMOST, WindowRect.left + 50, WindowRect.top + 50, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);

		// Parameters for each thread.
		typedef struct ThreadProcParameters
		{
			HANDLE     hcsMutex;
			WorkQueue* wq;
			double     dxMin;
			double     dxMax;
			double     dyMin;
			double     dyMax;
			uint32_t*  BitmapData;
			int        yMaxPixel;
			int        xMaxPixel;
			int        Iterations;
			BOOL       bUseTTMath;
			BOOL*      pbAbort;
		} THREADPROCPARAMETERS, *PTHREADPROCPARAMETERS;

		// Allocate per thread parameter and handle arrays.
		PTHREADPROCPARAMETERS* pThreadProcParameters = new PTHREADPROCPARAMETERS[Threads];
		HANDLE* phThreadArray = new HANDLE[Threads];

		// MaxPixel is the total pixel count among all threads.
		int MaxPixel = (rect.bottom - tm.tmHeight) * rect.right;
		int StartPixel, EndPixel, Slice;

		// PixelStepSize is the portion allocated to each thread.
		PixelStepSize = MaxPixel / Slices;

		// Instantiate the WorkQueue class
		wq = new WorkQueue;
		BOOL bAbort = false;

		// Create work items in the WorkQueue. Walk the start and end pixel indices.
		for (StartPixel = 0, EndPixel = PixelStepSize, Slice = 0;
			 EndPixel < MaxPixel;
			 StartPixel += PixelStepSize, EndPixel = min(EndPixel + PixelStepSize, MaxPixel), ++Slice)
		{
			wq->Enqueue(StartPixel, EndPixel);
		}
		if (EndPixel - PixelStepSize < MaxPixel) wq->Enqueue(EndPixel - PixelStepSize + 1, MaxPixel); // Get the last little bit.

		// Initialize and instantiate the Worker Thread Pool
		for (int Thread = 0; Thread < Threads; ++Thread)
		{
			// Allocate the parameter structure for this thread.
			pThreadProcParameters[Thread] =
				(PTHREADPROCPARAMETERS)HeapAlloc(GetProcessHeap(),
					HEAP_ZERO_MEMORY, sizeof(THREADPROCPARAMETERS));
			if (pThreadProcParameters[Thread] == NULL) ExitProcess(2);

			// Initialize the parameters for this thread.
			pThreadProcParameters[Thread]->hcsMutex   = hcsMutex;
			pThreadProcParameters[Thread]->wq         = wq;
			pThreadProcParameters[Thread]->dxMin      = dxMin;
			pThreadProcParameters[Thread]->dxMax      = dxMax;
			pThreadProcParameters[Thread]->dyMin      = dyMin;
			pThreadProcParameters[Thread]->dyMax      = dyMax;
			pThreadProcParameters[Thread]->BitmapData = BitmapData; // Bitmap is shared among all threads.
			pThreadProcParameters[Thread]->yMaxPixel  = rect.bottom - tm.tmHeight; // Leave room for the status bar.
			pThreadProcParameters[Thread]->xMaxPixel  = rect.right;
			pThreadProcParameters[Thread]->Iterations = Iterations;
			pThreadProcParameters[Thread]->bUseTTMath = bUseTTMath;
			pThreadProcParameters[Thread]->pbAbort    = &bAbort;

			// Create and launch this thread, initially stalled waiting for the mutex.
			phThreadArray[Thread] = CreateThread
				(NULL, 0, MandelbrotWorkerThread, pThreadProcParameters[Thread], 0, NULL);
			if (phThreadArray[Thread] == NULL)
			{
				ErrorHandler((LPTSTR)_T("CreateThread"));
				ExitProcess(3);
			}
		}

		// Release the held threads.
		ReleaseMutex(hcsMutex);

		// Wait for all threads to terminate.
		for (;;)
		{
			// Wait for up to fifty milliseconds.
			if (WaitForMultipleObjects(Threads, phThreadArray, TRUE, 50) == WAIT_OBJECT_0) break;
			
			// Update the user about progress.
			int Slice = wq->getSlices();
			int iPercent = (int)((Slices - Slice) * 100.f / Slices + 0.5f);
			StringCchPrintf(szProgress, 100, _T("Slice: %d of %d (%d%%)"), Slices - Slice, Slices, iPercent);
			SetBkColor(dc, RGB(240, 240, 240));
			TextOut(dc, 16, 16, szProgress, lstrlen(szProgress));
			TextOut(dc, 16, 40, _T("Press ESC to abort"), 19);

			// Flush one message from the queue and check for abort request.
			MSG msg;
			if (!PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) continue; // Case of no message.
			if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) bAbort = true;
		}

		ReleaseDC(hWndProgress, dc);
		ShowWindow(hWndProgress, SW_HIDE);

		// Delete the critical section mutex
		CloseHandle(hcsMutex);

		// Deallocate the thread arrays and structures.
		for (int Thread = 0; Thread < Threads; ++Thread)
		{
			CloseHandle(phThreadArray[Thread]);
			HeapFree(GetProcessHeap(), 0, pThreadProcParameters[Thread]);
		}
		delete[] phThreadArray;
		delete[] pThreadProcParameters;

		// Delete the Work Queue class.
		delete wq;

		if (bAbort) break; // Case of user abort.
		
		// Refresh the image with the bitmap.
		SetDIBitsToDevice(hdc, 0, 0, rect.right, rect.bottom - tm.tmHeight,
			0, 0, 0, rect.bottom - tm.tmHeight, BitmapData, &dbmi, 0);

		// Draw the axes if requested
		if (bShowAxes)
		{
			// Create and select a gray pen, saving the old pen.
			HPEN hpenNew = CreatePen(PS_SOLID, 1, RGB(127, 127, 127));
			HPEN hpenOld = (HPEN)SelectObject(hdc, hpenNew);

			// Draw the Y axis.
			int lx0 = (int)(-rect.right * dxMin / (dxMax - dxMin) + 0.5);
			if (lx0 >= rect.left && lx0 < rect.right) // Case of Y-axis visible.
			{
				MoveToEx(hdc, lx0, rect.top, NULL);
				LineTo(hdc, lx0, rect.bottom - tm.tmHeight);
			}

			// Draw the X-axis.
			int ly0 = (int)((-(rect.bottom - tm.tmHeight)) * dyMin / (dyMax - dyMin) + 0.5);
			if (ly0 >= rect.top && ly0 < rect.bottom - tm.tmHeight) // Case of X-axis visible.
			{
				MoveToEx(hdc, rect.left, ly0, NULL);
				LineTo(hdc, rect.right, ly0);
			}

			// Restore the old pen and delete the new pen.
			SelectObject(hdc, hpenOld);
			DeleteObject(hpenNew);
		}

		// Snapshot the end time and calculate the elapsed milliseconds.
		QueryPerformanceCounter(&liEnd);
		double dEnd = (double)liEnd.QuadPart / liFrequency.QuadPart;
		UINT uMSElapsed = (UINT)((dEnd - dStart) * 1000.0);

		// Fill the status bar line with the background color for the status bar.
		RECT rectFill;
		rectFill.left   = rect.left;
		rectFill.right  = rect.right;
		rectFill.top    = rect.bottom - tm.tmHeight;
		rectFill.bottom = rect.bottom;
		LOGBRUSH LogBrush;
		LogBrush.lbColor = RGB(0, 0, 250);
		LogBrush.lbStyle = BS_SOLID;
		LogBrush.lbHatch = NULL;
		HBRUSH hbrFill = CreateBrushIndirect(&LogBrush);
		FillRect(hdc, &rectFill, hbrFill);
		DeleteObject(hbrFill);

		// Build the status bar.
		#define MAX_STATUS_LINE_LEN 200 // Actual is 90 + 4 * 13 + 2 + 4 + 4 + 1 = 153
		TCHAR szStatusLine[MAX_STATUS_LINE_LEN];
		StringCchPrintf(szStatusLine, MAX_STATUS_LINE_LEN,
			_T("xMin:  %+.6E    xMax:  %+.6E    yMin:  %+.6E i    yMax:  %+.6E i    Slices:  %d    Threads:  %d    MilliSeconds:  %d"),
			//  1234567     89012345678     90123456789     0123456789012     345678901234567  89012345678901  2345678901234567890
			dxMin, dxMax, dyMin, dyMax, Slices, Threads, uMSElapsed);

		// Paint the status bar.
		SetTextColor(hdc, RGB(250, 250, 250));
		SetBkColor(hdc, RGB(0, 0, 250));
		TextOut(hdc, 40, rect.bottom - tm.tmHeight, szStatusLine, lstrlen(szStatusLine));

		EndPaint(hWnd, &ps);

		break;
	}

	case WM_KEYDOWN:
	{
		// Hold painting if the shift key is held, such as during resizing.
		if (wParam == VK_SHIFT) bHoldPainting = true;

		// Cancel drag zoom if Esc key is pressed.
		if (wParam == VK_ESCAPE && LeftMouseButtonDown)
		{
			LeftMouseButtonDown = false;

			// Setup to refresh the image from the bitmap.
			HDC dc = GetDC(hWnd);
			TEXTMETRIC tm;
			GetTextMetrics(dc, &tm);
			RECT rect;
			GetClientRect(hWnd, &rect);
			
			// Refresh the image with the bitmap, erasing the drag rectangle left behind.
			SetDIBitsToDevice(dc, 0, 0, rect.right, rect.bottom - tm.tmHeight,
				0, 0, 0, rect.bottom - tm.tmHeight, BitmapData, &dbmi, 0);

			// Draw the axes if requested
			if (bShowAxes)
			{
				// Create and select a gray pen, saving the old pen.
				HPEN hpenNew = CreatePen(PS_SOLID, 1, RGB(127, 127, 127));
				HPEN hpenOld = (HPEN)SelectObject(dc, hpenNew);

				// Draw the Y axis.
				int lx0 = (int)(-rect.right * dxMin / (dxMax - dxMin) + 0.5);
				if (lx0 >= rect.left && lx0 < rect.right) // Case of Y-axis visible.
				{
					MoveToEx(dc, lx0, rect.top, NULL);
					LineTo(dc, lx0, rect.bottom - tm.tmHeight);
				}

				// Draw the X-axis.
				int ly0 = (int)((-(rect.bottom - tm.tmHeight)) * dyMin / (dyMax - dyMin) + 0.5);
				if (ly0 >= rect.top && ly0 < rect.bottom - tm.tmHeight) // Case of X-axis visible.
				{
					MoveToEx(dc, rect.left, ly0, NULL);
					LineTo(dc, rect.right, ly0);
				}

				// Restore the old pen and delete the new pen.
				SelectObject(dc, hpenOld);
				DeleteObject(hpenNew);
			}
		}
		break;
	}

	case WM_KEYUP:
	{
		// Release paint hold if shift key is released.
		if (wParam == VK_SHIFT)
		{
			bHoldPainting = false;

			// Case of prior hold request honored.
			if (bPaintingHeld) 
			{
				bPaintingHeld = false;

				// Repaint the window.
				InvalidateRect(hWnd, NULL, true);
			}
		}
		break;
	}

	// Mouse wheel rotated - Zoom in or out about a point.
	case WM_MOUSEWHEEL:
	{
		// Get the mouse location.
		POINT MousePoint;
		MousePoint.x = GET_X_LPARAM(lParam);
		MousePoint.y = GET_Y_LPARAM(lParam);
		ScreenToClient(hWnd, &MousePoint);
		
		// Get the mouse wheel delta.
		iMouseDelta = GET_WHEEL_DELTA_WPARAM(wParam);

		RECT rect;
		GetClientRect(hWnd, &rect);

		// Convert mouse coordinates to reaL x-axis and y-axis coordinates.
		dxMouse = (dxMax - dxMin) / (rect.right  - rect.left             ) * MousePoint.x + dxMin;
		dyMouse = (dyMax - dyMin) / (rect.bottom - tm.tmHeight - rect.top) * MousePoint.y + dyMin;
		
		// Arbitrarily chosen zoom factor is 2. We don't care that the delta increment
		// if +/- 120. We will zoom one step in either direction per wheel click.
		// Besides, testing revealed that it is difficult to get more than +/- 120.
		// Also, the system scroll parameter for the mouse wheel is ignored.
		dMouseMagnify = iMouseDelta < 0 ? 2.0 : 0.5;

		// Save the old coordinates to the recall stack.
		qds->push(hWnd, dxMin, dxMax, dyMin, dyMax);

		// Calculate and set new coordinates.
		dxMin = dxMouse - (dxMouse - dxMin) * dMouseMagnify;
		dyMin = dyMouse - (dyMouse - dyMin) * dMouseMagnify;
		dxMax = dxMouse + (dxMax - dxMouse) * dMouseMagnify;
		dyMax = dyMouse + (dyMax - dyMouse) * dMouseMagnify;

		// Repaint the window.
		InvalidateRect(hWnd, NULL, true);

		break;
	}

	// Right click. Get the last coordinates from the recall stack;
	case WM_RBUTTONDOWN:
	{
		// Recall the coordinates.
		if (qds->pop(hWnd, dxMin, dxMax, dyMin, dyMax))
		{
			// Repaint the window.
			InvalidateRect(hWnd, NULL, true);
		}
		break;
	}

	// Left button down. Start a drag zoom.
	case WM_LBUTTONDOWN:
	{
		LeftMouseButtonDown = true;
		
		// Save the starting coordinate.
		MouseDragOrigin.x = GET_X_LPARAM(lParam);
		MouseDragOrigin.y = GET_Y_LPARAM(lParam);
		
		// Capture the mouse, forcing messages, inside and outside of this window, to this window.
		SetCapture(hWnd);

		break;
	}

	// Mouse move. Ongoing drag zoom. Draw a (moving) box at the [origin - destination] coordinates.
	case WM_MOUSEMOVE:
	{
		if (!LeftMouseButtonDown) break;
		
		// Setup to draw.
		HDC dc = GetDC(hWnd);
		TEXTMETRIC tm;
		GetTextMetrics(dc, &tm);
		RECT rect;
		GetClientRect(hWnd, &rect);

		// Get the (current) destination coordinates.
		MouseDragDestination.x = min(max(GET_X_LPARAM(lParam), rect.left), rect.right                - 1);
		MouseDragDestination.y = min(max(GET_Y_LPARAM(lParam), rect.top ), rect.bottom - tm.tmHeight - 1);

		// Swap out the current pen for a white pen, saving the current pen.
		HPEN hPenNew = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
		HPEN hPenOld = (HPEN)SelectObject(dc, hPenNew);

		// Refresh the image from the bitmap, erasing the previous box.
		SetDIBitsToDevice(dc, 0, 0, rect.right, rect.bottom - tm.tmHeight,
			0, 0, 0, rect.bottom - tm.tmHeight, BitmapData, &dbmi, 0);

		// Draw the new box.
		MoveToEx(dc, MouseDragOrigin.x,      MouseDragOrigin.y, NULL);
		LineTo  (dc, MouseDragOrigin.x,      MouseDragDestination.y);
		LineTo  (dc, MouseDragDestination.x, MouseDragDestination.y);
		LineTo  (dc, MouseDragDestination.x, MouseDragOrigin.y);
		LineTo  (dc, MouseDragOrigin.x,      MouseDragOrigin.y);

		// Restore the old pen to the dc.
		SelectObject(dc, hPenOld);
		DeleteObject(hPenNew);

		// Redraw the axes if requested
		if (bShowAxes)
		{
			// Create and select a gray pen, saving the old pen.
			HPEN hpenNew = CreatePen(PS_SOLID, 1, RGB(127, 127, 127));
			HPEN hpenOld = (HPEN)SelectObject(dc, hpenNew);

			// Draw the Y axis.
			int lx0 = (int)(-rect.right * dxMin / (dxMax - dxMin) + 0.5);
			if (lx0 >= rect.left && lx0 < rect.right) // Case of Y-axis visible.
			{
				MoveToEx(dc, lx0, rect.top, NULL);
				LineTo(dc, lx0, rect.bottom - tm.tmHeight);
			}

			// Draw the X-axis.
			int ly0 = (int)((-(rect.bottom - tm.tmHeight)) * dyMin / (dyMax - dyMin) + 0.5);
			if (ly0 >= rect.top && ly0 < rect.bottom - tm.tmHeight) // Case of X-axis visible.
			{
				MoveToEx(dc, rect.left, ly0, NULL);
				LineTo(dc, rect.right, ly0);
			}

			// Restore the old pen and delete the new pen.
			SelectObject(dc, hpenOld);
			DeleteObject(hpenNew);
		}

		ReleaseDC(hWnd, dc);

		break;
	}

	// Left button up. End of a drag zoom, a malformed drag zoom, or a single click.
	case WM_LBUTTONUP:
	{
		// Case of double click on file open. When the file open dialog box closes,
		// the mouse button is still down, so when it is released, this code is hit.
		if (!LeftMouseButtonDown) break;

		LeftMouseButtonDown = false;
		ReleaseCapture();

		RECT rect;
		GetClientRect(hWnd, &rect);

		HDC dc = GetDC(hWnd);
		TEXTMETRIC tm;
		GetTextMetrics(dc, &tm);
		ReleaseDC(hWnd, dc);

		// Get the final destination coordinates.
		MouseDragDestination.x = min(max(GET_X_LPARAM(lParam), rect.left), rect.right                - 1);
		MouseDragDestination.y = min(max(GET_Y_LPARAM(lParam), rect.top ), rect.bottom - tm.tmHeight - 1);

		// Calculate the coordinates of the box, correcting for flipping in either axis.
		long lxMin = min(MouseDragOrigin.x, MouseDragDestination.x);
		long lxMax = max(MouseDragOrigin.x, MouseDragDestination.x);
		long lyMin = min(MouseDragOrigin.y, MouseDragDestination.y);
		long lyMax = max(MouseDragOrigin.y, MouseDragDestination.y);

		// If the box is a point, it is a click, instead of a drag, so move the
		// endpoints of the real axes, centering them about the click coordinates.
		if (lxMin == lxMax && lyMin == lyMax)
		{
			double dxMid = (dxMin + dxMax) / 2;
			double dxClick = (dxMax - dxMin) / (rect.right - rect.left) * lxMin + dxMin;
			double dxMinNew = dxMin - dxMid + dxClick;
			double dxMaxNew = dxMax - dxMid + dxClick;

			double dyMid = (dyMin + dyMax) / 2;
			double dyClick = (dyMax - dyMin) / (rect.bottom - rect.top) * lyMin + dyMin;
			double dyMinNew = dyMin - dyMid + dyClick;
			double dyMaxNew = dyMax - dyMid + dyClick;

			// Save the old coordinates to the recall stack.
			qds->push(hWnd, dxMin, dxMax, dyMin, dyMax);

			// Update the new coordinates.
			dxMin = dxMinNew;
			dxMax = dxMaxNew;
			dyMin = dyMinNew;
			dyMax = dyMaxNew;

			// Repaint the window.
			InvalidateRect(hWnd, NULL, true);

			break;
		}

		// Malformed drag, where the box degraded to either a
		// vertical or horizontal line, so abort the drag.
		if (lxMin == lxMax || lyMin == lyMax)
		{
			// Setup to draw.
			HDC dc = GetDC(hWnd);
			TEXTMETRIC tm;
			GetTextMetrics(dc, &tm);
			RECT rect;
			GetClientRect(hWnd, &rect);

			// Refresh the image from the bitmap, erasing the malformed box.
			SetDIBitsToDevice(dc, 0, 0, rect.right, rect.bottom - tm.tmHeight,
				0, 0, 0, rect.bottom - tm.tmHeight, BitmapData, &dbmi, 0);

			// Draw the axes if requested
			if (bShowAxes)
			{
				// Create and select a gray pen, saving the old pen.
				HPEN hpenNew = CreatePen(PS_SOLID, 1, RGB(127, 127, 127));
				HPEN hpenOld = (HPEN)SelectObject(dc, hpenNew);

				// Draw the Y axis.
				int lx0 = (int)(-rect.right * dxMin / (dxMax - dxMin) + 0.5);
				if (lx0 >= rect.left && lx0 < rect.right) // Case of Y-axis visible.
				{
					MoveToEx(dc, lx0, rect.top, NULL);
					LineTo(dc, lx0, rect.bottom - tm.tmHeight);
				}

				// Draw the X-axis.
				int ly0 = (int)((-(rect.bottom - tm.tmHeight)) * dyMin / (dyMax - dyMin) + 0.5);
				if (ly0 >= rect.top && ly0 < rect.bottom - tm.tmHeight) // Case of X-axis visible.
				{
					MoveToEx(dc, rect.left, ly0, NULL);
					LineTo(dc, rect.right, ly0);
				}

				// Restore the old pen and delete the new pen.
				SelectObject(dc, hpenOld);
				DeleteObject(hpenNew);
			}

			ReleaseDC(hWnd, dc);
			break;
		}
			
		// Successful drag. Calculate the coordinates of the dragbox.
		double dxMinNew = (dxMax - dxMin) / (rect.right - rect.left) * lxMin + dxMin;
		double dxMaxNew = (dxMax - dxMin) / (rect.right - rect.left) * lxMax + dxMin;
		double dyMinNew = (dyMax - dyMin) / (rect.bottom - tm.tmHeight - rect.top) * lyMin + dyMin;
		double dyMaxNew = (dyMax - dyMin) / (rect.bottom - tm.tmHeight - rect.top) * lyMax + dyMin;

		// Save the old coordinates to the recall stack.
		qds->push(hWnd, dxMin, dxMax, dyMin, dyMax);

		// Update the coordinates.
		dxMin = dxMinNew;
		dxMax = dxMaxNew;
		dyMin = dyMinNew;
		dyMax = dyMaxNew;

		// Repaint the window.
		InvalidateRect(hWnd, NULL, true);

		break;
	}

	// We are shutting down.
	case WM_DESTROY:
	{
		// Delete the bitmap.
		if (BitmapData != NULL)
		{
			delete[] BitmapData;
			BitmapData = NULL;
		}

		// Save the WindowPlacement to the registry
		ApplicationRegistry* pAppReg = new ApplicationRegistry;
		pAppReg->Init(hWnd);
		WINDOWPLACEMENT wp;
		ZeroMemory(&wp, sizeof(wp));
		wp.length = sizeof(wp);
		GetWindowPlacement(hWnd, &wp);
		if (pAppReg->Init(hWnd))
			pAppReg->SaveMemoryBlock(_T("WindowPlacement"), (LPBYTE)&wp, sizeof(wp));
		delete pAppReg;

		DestroyWindow(hWndProgress);

		// Send the final message.
		PostQuitMessage(0);
		break;
	}

	case WM_CREATE:
		// Create the modeless dialog box - We will display it later in the process procedure
		hWndProgress = CreateDialog(hInst, MAKEINTRESOURCE(IDD_PROGRESS), hWnd, (DLGPROC)MDBoxProc);
		if (hWndProgress == NULL)
		{
			MessageBeep(MB_ICONEXCLAMATION);
			MessageBox(hWnd, _T("CreateDialog returned NULL"),
				_T("Warning!"), MB_OK | MB_ICONEXCLAMATION);
		}
		break;

	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}

void ErrorHandler(LPTSTR pszFunction)
{
	// Retrieve the system error message for the last-error code.

	LPVOID lpMsgBuf;
	LPVOID lpDisplayBuf;
	DWORD dw = GetLastError();

	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM |
		FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL,
		dw,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf,
		0, NULL);

	// Display the error message.

	lpDisplayBuf = (LPVOID)LocalAlloc(LMEM_ZEROINIT,
		(lstrlen((LPCTSTR)lpMsgBuf) + lstrlen((LPCTSTR)pszFunction) + 40) * sizeof(TCHAR));
	StringCchPrintf((LPTSTR)lpDisplayBuf,
		LocalSize(lpDisplayBuf) / sizeof(TCHAR),
		TEXT("%s failed with error %d: %s"),
		pszFunction, dw, lpMsgBuf);
	MessageBox(NULL, (LPCTSTR)lpDisplayBuf, TEXT("Error"), MB_OK);

	// Free error-handling buffer allocations.

	LocalFree(lpMsgBuf);
	LocalFree(lpDisplayBuf);
}



// Message handler for about box.
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(lParam);
	switch (message)
	{
	case WM_INITDIALOG:
		return (INT_PTR)TRUE;

	case WM_COMMAND:
		if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
		{
			EndDialog(hDlg, LOWORD(wParam));
			return (INT_PTR)TRUE;
		}
		break;
	}
	return (INT_PTR)FALSE;
}



// Message handler for parameters box.
INT_PTR CALLBACK Parameters(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(lParam);
	switch (message)
	{
	case WM_INITDIALOG:
	{
		SetDlgItemText(hDlg, IDC_XMIN,       dTos(dxMin));
		SetDlgItemText(hDlg, IDC_XMAX,       dTos(dxMax));
		SetDlgItemText(hDlg, IDC_YMIN,       dTos(dyMin));
		SetDlgItemText(hDlg, IDC_YMAX,       dTos(dyMax));
		SetDlgItemText(hDlg, IDC_ITERATIONS, iTos(Iterations));
		SetDlgItemText(hDlg, IDC_SLICES,     iTos(Slices));
		SetDlgItemText(hDlg, IDC_THREADS,    iTos(Threads));
		CheckDlgButton(hDlg, IDC_SHOW_AXES,  bShowAxes  ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(hDlg, IDC_USEHSV,     bUseHSV    ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(hDlg, IDC_USETTMATH,  bUseTTMath ? BST_CHECKED : BST_UNCHECKED);

		return (INT_PTR)TRUE;

		break;
	}

	case WM_COMMAND:
	{
		int ctlID = LOWORD(wParam);
		switch (ctlID)
		{
		case IDC_SETDEFAULTS:
		{
			SetDlgItemText(hDlg, IDC_XMIN,       dTos(-2.00));
			SetDlgItemText(hDlg, IDC_XMAX,       dTos(+0.47));
			SetDlgItemText(hDlg, IDC_YMIN,       dTos(-1.12));
			SetDlgItemText(hDlg, IDC_YMAX,       dTos(+1.12));
			SetDlgItemText(hDlg, IDC_ITERATIONS, iTos(1000));
			SetDlgItemText(hDlg, IDC_SLICES,     iTos(5000));
			SetDlgItemText(hDlg, IDC_THREADS,    iTos(12));

			CheckDlgButton(hDlg, IDC_SHOW_AXES, false);
			CheckDlgButton(hDlg, IDC_USEHSV,    true );
			CheckDlgButton(hDlg, IDC_USETTMATH, false);

			SendMessage(hDlg, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(hDlg, IDOK), true);

			return (INT_PTR)TRUE;
		}

		case IDCANCEL:
		{
			EndDialog(hDlg, LOWORD(wParam));
			return (INT_PTR)TRUE;
		}

		case IDOK:
		{
			TCHAR sz[64];
			double dxMinTemp, dxMaxTemp, dyMinTemp, dyMaxTemp;
			int IterationsTemp, SlicesTemp, ThreadsTemp;

			if (GetDlgItemText(hDlg, IDC_XMIN, sz, 64) == 0 || swscanf_s(sz, _T("%lf"), &dxMinTemp) == 0)
			{
				MessageBeep(MB_ICONEXCLAMATION);
				MessageBox(hDlg, _T("Enter number for X Min."), _T("Error"), MB_OK | MB_ICONEXCLAMATION);
				SendMessage(hDlg, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(hDlg, IDC_XMIN), true);
				break;
			}

			if (GetDlgItemText(hDlg, IDC_XMAX, sz, 64) == 0 || swscanf_s(sz, _T("%lf"), &dxMaxTemp) == 0)
			{
				MessageBeep(MB_ICONEXCLAMATION);
				MessageBox(hDlg, _T("Enter number for X Max."), _T("Error"), MB_OK | MB_ICONEXCLAMATION);
				SendMessage(hDlg, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(hDlg, IDC_XMAX), true);
				break;
			}

			if (GetDlgItemText(hDlg, IDC_YMIN, sz, 64) == 0 || swscanf_s(sz, _T("%lf"), &dyMinTemp) == 0)
			{
				MessageBeep(MB_ICONEXCLAMATION);
				MessageBox(hDlg, _T("Enter number for Y Min."), _T("Error"), MB_OK | MB_ICONEXCLAMATION);
				SendMessage(hDlg, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(hDlg, IDC_YMIN), true);
				break;
			}

			if (GetDlgItemText(hDlg, IDC_YMAX, sz, 64) == 0 || swscanf_s(sz, _T("%lf"), &dyMaxTemp) == 0)
			{
				MessageBeep(MB_ICONEXCLAMATION);
				MessageBox(hDlg, _T("Enter number for Y Max."), _T("Error"), MB_OK | MB_ICONEXCLAMATION);
				SendMessage(hDlg, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(hDlg, IDC_YMAX), true);
				break;
			}

			if (GetDlgItemText(hDlg, IDC_ITERATIONS, sz, 64) == 0 || swscanf_s(sz, _T("%d"), &IterationsTemp) == 0)
			{
				MessageBeep(MB_ICONEXCLAMATION);
				MessageBox(hDlg, _T("Enter number for Iterations."), _T("Error"), MB_OK | MB_ICONEXCLAMATION);
				SendMessage(hDlg, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(hDlg, IDC_ITERATIONS), true);
				break;
			}

			if (GetDlgItemText(hDlg, IDC_SLICES, sz, 64) == 0 || swscanf_s(sz, _T("%d"), &SlicesTemp) == 0)
			{
				MessageBeep(MB_ICONEXCLAMATION);
				MessageBox(hDlg, _T("Enter number for Slices."), _T("Error"), MB_OK | MB_ICONEXCLAMATION);
				SendMessage(hDlg, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(hDlg, IDC_SLICES), true);
				break;
			}

			if (GetDlgItemText(hDlg, IDC_THREADS, sz, 64) == 0 || swscanf_s(sz, _T("%d"), &ThreadsTemp) == 0)
			{
				MessageBeep(MB_ICONEXCLAMATION);
				MessageBox(hDlg, _T("Enter number for Threads."), _T("Error"), MB_OK | MB_ICONEXCLAMATION);
				SendMessage(hDlg, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(hDlg, IDC_THREADS), true);
				break;
			}

			if (dxMinTemp >= dxMaxTemp)
			{
				MessageBeep(MB_ICONEXCLAMATION);
				MessageBox(hDlg, _T("X Min must be less than X Max."), _T("Error"), MB_OK | MB_ICONEXCLAMATION);
				SendMessage(hDlg, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(hDlg, IDC_XMIN), true);
				break;
			}

			if (dxMaxTemp >= dyMaxTemp)
			{
				MessageBeep(MB_ICONEXCLAMATION);
				MessageBox(hDlg, _T("Y Min must be less than Y Max."), _T("Error"), MB_OK | MB_ICONEXCLAMATION);
				SendMessage(hDlg, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(hDlg, IDC_YMIN), true);
				break;
			}

			if (IterationsTemp <= 0)
			{
				MessageBeep(MB_ICONEXCLAMATION);
				MessageBox(hDlg, _T("Iterations must be greater than zero."), _T("Error"), MB_OK | MB_ICONEXCLAMATION);
				SendMessage(hDlg, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(hDlg, IDC_ITERATIONS), true);
				break;
			}

			if (SlicesTemp <= 0)
			{
				MessageBeep(MB_ICONEXCLAMATION);
				MessageBox(hDlg, _T("Slices must be greater than zero."), _T("Error"), MB_OK | MB_ICONEXCLAMATION);
				SendMessage(hDlg, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(hDlg, IDC_SLICES), true);
				break;
			}

			if (ThreadsTemp <= 0 || ThreadsTemp >= 65)
			{
				MessageBeep(MB_ICONEXCLAMATION);
				MessageBox(hDlg, _T("Threads must be greater than zero and less than 65."), _T("Error"), MB_OK | MB_ICONEXCLAMATION);
				SendMessage(hDlg, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(hDlg, IDC_THREADS), true);
				break;
			}

			qds->push(NULL, dxMin, dxMax, dyMin, dyMax);

			dxMin = dxMinTemp;
			dxMax = dxMaxTemp;
			dyMin = dyMinTemp;
			dyMax = dyMaxTemp;

			Iterations = IterationsTemp;
			Slices = SlicesTemp;
			Threads = ThreadsTemp;

			bShowAxes  = IsDlgButtonChecked(hDlg, IDC_SHOW_AXES) == BST_CHECKED ? true : false;
			bUseHSV    = IsDlgButtonChecked(hDlg, IDC_USEHSV)    == BST_CHECKED ? true : false;
			bUseTTMath = IsDlgButtonChecked(hDlg, IDC_USETTMATH) == BST_CHECKED ? true : false;

			EndDialog(hDlg, LOWORD(wParam));
			return (INT_PTR)TRUE;
		}
		default:
			break;
		}
	break;
	}
	}
	return (INT_PTR)FALSE;
}



// Convert double to string. Helper for the parameters dialog box procedure.
TCHAR* dTos(DOUBLE d)
{
	static TCHAR sz[20];
	if (abs(d) < 0.01 || abs(d) > 100.0) StringCchPrintf(sz, 20, _T("%.9E"), d);
	else                                 StringCchPrintf(sz, 20, _T("%.9f"), d);
	return sz;
}



// Convert int to string. Helper for the parameters dialog box procedure.
TCHAR* iTos(int i)
{
	static TCHAR sz[20];
	StringCchPrintf(sz, 20, _T("%d"), i);
	return sz;
}



// Message handler for modeless dialog box.
INT_PTR CALLBACK MDBoxProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(hDlg);
	UNREFERENCED_PARAMETER(wParam);
	UNREFERENCED_PARAMETER(lParam);

	switch (message)
	{
	case WM_INITDIALOG:
		return true;
	case WM_COMMAND:
		return true;
	}
	return false;
}



// Worker thread for processing the Mandelbrot algorithm
DWORD WINAPI MandelbrotWorkerThread(LPVOID lpParam)
{
	// This is a copy of the structure from the paint procedure.
	// The address of this structure is passed with lParam.
	typedef struct ThreadProcParameters
	{
		HANDLE     hcsMutex;
		WorkQueue* wq;
		double     dxMin;
		double     dxMax;
		double     dyMin;
		double     dyMax;
		uint32_t* BitmapData;
		int        yMaxPixel;
		int        xMaxPixel;
		int        Iterations;
		BOOL       bUseTTMath;
		BOOL*      pbAbort;
	} THREADPROCPARAMETERS, * PTHREADPROCPARAMETERS;
	PTHREADPROCPARAMETERS P;
	P = (PTHREADPROCPARAMETERS)lpParam;

	// Loop until no more work to do.
	for (;;)
	{
		if (*(P->pbAbort)) return 0; // Case of user pressed ESCAPE

		// Retrieve the Work Queue slice
		int StartPixel, EndPixel;                               // Work slice.
		WaitForSingleObject(P->hcsMutex, INFINITE);             // Enter Critical Section.
		BOOL bWorkToDo = wq->Dequeue(StartPixel, EndPixel);     // Retrieve slice.
		ReleaseMutex(P->hcsMutex);                              // Leave Critical Section.
		if (!bWorkToDo) return 0;                               // No more work to do.

		// Algorithm: https://en.wikipedia.org/wiki/Plotting_algorithms_for_the_Mandelbrot_set.

		double             x0,  y0,  x,  y,  x2,  y2; // for the old FPU logic.
		ttmath::Big<1, 4> xx0, yy0, xx, yy, xx2, yy2; // for the new TTMath library.

		int iteration, Iterations = P->Iterations;

		if (!P->bUseTTMath) // Case of using the old FPU logic.
		{
			// Loop for each pixel in the slice.
			for (int Pixel = StartPixel; Pixel < EndPixel; ++Pixel)
			{
				// Calculate the x and y coordinates of the pixel.
				int xPixel = Pixel % P->xMaxPixel;
				int yPixel = Pixel / P->xMaxPixel;

				// Calculate the real and imaginary coordinates of the point.
				x0 = (P->dxMax - P->dxMin) / P->xMaxPixel * xPixel + P->dxMin;
				y0 = (P->dyMax - P->dyMin) / P->yMaxPixel * yPixel + P->dyMin;

				// Initial values.
				x = 0.0;
				y = 0.0;
				x2 = 0.0;
				y2 = 0.0;
				iteration = 0;

				// Main Mandelbrot algorithm. Determine the number of iterations
				// that it takes each point to escape the distance of 2. The black
				// areas of the image represent the points that never escape. This
				// algorithm is described as the Optimized Escape Time algorithm
				// in the WikiPedia article noted above.

				while (x2 + y2 <= 4.0 && iteration < Iterations)
				{
					y = (x + x) * y + y0;
					x = x2 - y2 + x0;
					x2 = x * x;
					y2 = y * y;
					++iteration;
				}

				// When we get here, we have a pixel and an iteration count.
				// Lookup the color in the spectrum of all colors and set the
				// pixel to that color. Note that we are only ever using 1000
				// of the 16777216 possible colors. Changing Iterations uses
				// a different pallette, but 1000 seems to be the best choice.
				// Note also that this bitmap is shared by all the threads, but
				// there is no concurrency conflict as each thread is assigned
				// a different region of the bitmap. The user has the option of
				// using the original RGB system or the new triple-Log HSV system.

				if (bUseHSV)
				{
					// The new HSV system.
					sRGB rgb;
					sHSV hsv;
					hsv = mandelbrotHSV(iteration, Iterations);
					rgb = hsv2rgb(hsv);
					P->BitmapData[Pixel] =
						(((int)(rgb.r * 255 + 0.5))      ) +
						(((int)(rgb.g * 255 + 0.5)) <<  8) +
						(((int)(rgb.b * 255 + 0.5)) << 16);
				}
				else
				{
					// The old RGB system.
					P->BitmapData[Pixel] = ReverseRGBBytes
					((COLORREF)(-16777216.0 / Iterations * iteration + 16777216.0));
				}
			}
		}
		else // Case of using the new TTMath library
		{
			// Loop for each pixel in the slice.
			for (int Pixel = StartPixel; Pixel < EndPixel; ++Pixel)
			{
				// Calculate the x and y coordinates of the pixel.
				int xPixel = Pixel % P->xMaxPixel;
				int yPixel = Pixel / P->xMaxPixel;

				// Calculate the real and imaginary coordinates of the point.
				xx0 = (P->dxMax - P->dxMin) / P->xMaxPixel * xPixel + P->dxMin;
				yy0 = (P->dyMax - P->dyMin) / P->yMaxPixel * yPixel + P->dyMin;

				// Initial values.
				xx = 0.0;
				yy = 0.0;
				xx2 = 0.0;
				yy2 = 0.0;
				iteration = 0;

				// Main Mandelbrot algorithm. Determine the number of iterations
				// that it takes each point to escape the distance of 2. The black
				// areas of the image represent the points that never escape. This
				// algorithm is described as the Optimized Escape Time algorithm
				// in the WikiPedia article noted above.

				while (xx2 + yy2 <= 4.0 && iteration < Iterations)
				{
					yy = (xx + xx) * yy + yy0;
					xx = xx2 - yy2 + xx0;
					xx2 = xx * xx;
					yy2 = yy * yy;
					++iteration;
				}

				// When we get here, we have a pixel and an iteration count.
				// Lookup the color in the spectrum of all colors and set the
				// pixel to that color. Note that we are only ever using 1000
				// of the 16777216 possible colors. Changing Iterations uses
				// a different pallette, but 1000 seems to be the best choice.
				// Note also that this bitmap is shared by all the threads, but
				// there is no concurrency conflict as each thread is assigned
				// a different region of the bitmap. The user has the option of
				// using the original RGB system or the new triple-Log HSV system.

				if (bUseHSV)
				{
					// The new HSV system.
					sRGB rgb;
					sHSV hsv;
					hsv = mandelbrotHSV(iteration, Iterations);
					rgb = hsv2rgb(hsv);
					P->BitmapData[Pixel] =
						(((int)(rgb.r * 255 + 0.5))      ) +
						(((int)(rgb.g * 255 + 0.5)) <<  8) +
						(((int)(rgb.b * 255 + 0.5)) << 16);
				}
				else
				{
					// The old RGB system.
					P->BitmapData[Pixel] = ReverseRGBBytes
					((COLORREF)(-16777216.0 / Iterations * iteration + 16777216.0));
				}
			}
		}
	}
}