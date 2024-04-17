#pragma once
#include "framework.h"

#define MAX_KEYLEN 100
#define MAX_QUERY_COMPANYNAME_LEN 50
#define MAX_QUERY_PRODUCTNAME_LEN 50
#define MAX_QUERY_PRODUCTVERSION_LEN 50
#define MAX_ERROR_MESSAGE_LEN 100

class ApplicationRegistry
{
private:
	HWND     _hWnd;
	wstring* _psRegistrySubKey;
	BOOL     _isOK;
	UINT     _LastAPICallLine;
	DWORD    _LastErrorNumber;
public:
	ApplicationRegistry();
	~ApplicationRegistry() { delete _psRegistrySubKey; }
	BOOL Init(HWND hWnd);
	BOOL LoadMemoryBlock(const wstring& sEntry,       BYTE *lpMemoryBlock, DWORD cbMemoryBlock);
	BOOL SaveMemoryBlock(const wstring& sEntry, const BYTE *lpMemoryBlock, DWORD cbMemoryBlock);
	BOOL isOK() { return _isOK; }
	void DisplayAPIError();
};
