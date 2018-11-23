#include "pch.h"
//#include <mach/mach_time.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>

HANDLE hWnd;
LPTSTR szTitle;

//static NSMutableDictionary *gEventLockDict;
static HANDLE gEventId;


DWORD GetCurrentDirectory(DWORD nBufferLength, LPTSTR lpBuffer) {
    if(getcwd(lpBuffer, nBufferLength)) {
        return nBufferLength;
    }
    return 0;
}

BOOL SetCurrentDirectory(LPCTSTR path)
{
    if(path == NULL)
        return FALSE;

    return chdir(path);
}

HANDLE CreateFile(LPCTSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPVOID lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, LPVOID hTemplateFile)
{
    int flags = O_RDWR;
    int fd = -1;
    struct flock lock;
    mode_t perm = S_IRUSR | S_IWUSR;

    if (GENERIC_READ == dwDesiredAccess)
        flags = O_RDONLY;
    else
    {
        if (GENERIC_WRITE == dwDesiredAccess)
            flags = O_WRONLY;
        else if (0 != ((GENERIC_READ|GENERIC_WRITE) & dwDesiredAccess))
            flags = O_RDWR;

        if (CREATE_ALWAYS == dwCreationDisposition)
            flags |= O_CREAT;
    }

    fd = open(lpFileName, flags, perm);
    if (-1 != fd && 0 != dwShareMode)
    {
        // Not specifiying shared write means non-shared (exclusive) write
        if (0 == (dwShareMode & FILE_SHARE_WRITE))
            lock.l_type = F_WRLCK;
        else if (0 != (dwShareMode & FILE_SHARE_READ))
            lock.l_type = F_RDLCK;

        // Lock entire file
        lock.l_len = lock.l_start = 0;
        lock.l_whence = SEEK_SET;

        if (-1 == fcntl(fd, F_SETLK, &lock) && (EACCES == errno || EAGAIN == errno))
        {
            close(fd);
            return -1;
        }
    }

    HANDLE result = malloc(sizeof(_HANDLE));
    result->handleType = HANDLE_TYPE_FILE;
    result->fileDescriptor = fd;
    return result;
}

BOOL ReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped) {
    DWORD readByteCount = read(hFile->fileDescriptor, lpBuffer, nNumberOfBytesToRead);
    if(lpNumberOfBytesRead)
        *lpNumberOfBytesRead = readByteCount;
    return readByteCount >= 0;
}

BOOL WriteFile(HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite,LPDWORD lpNumberOfBytesWritten, LPOVERLAPPED lpOverlapped) {
    size_t writenByteCount = write(hFile->fileDescriptor, lpBuffer, nNumberOfBytesToWrite);
    if(lpNumberOfBytesWritten)
        *lpNumberOfBytesWritten = writenByteCount;
    return writenByteCount >= 0;
}

DWORD SetFilePointer(HANDLE hFile, LONG lDistanceToMove, PLONG lpDistanceToMoveHigh, DWORD dwMoveMethod) {
    int moveMode;
    if(dwMoveMethod == FILE_BEGIN)
        moveMode = SEEK_SET;
    else if(dwMoveMethod == FILE_CURRENT)
        moveMode = SEEK_CUR;
    else if(dwMoveMethod == FILE_END)
        moveMode = SEEK_END;
    int seekResult = lseek(hFile->fileDescriptor, lDistanceToMove, moveMode);
    return seekResult < 0 ? INVALID_SET_FILE_POINTER : seekResult;
}

BOOL SetEndOfFile(HANDLE hFile) {
    off_t currentPosition = lseek(hFile->fileDescriptor, 0, SEEK_CUR);
    int truncateResult = ftruncate(hFile->fileDescriptor, currentPosition);
    return truncateResult == 0;
}

DWORD GetFileSize(HANDLE hFile, LPDWORD lpFileSizeHigh) {
    off_t currentPosition = lseek(hFile->fileDescriptor, 0, SEEK_CUR);
    off_t fileLength = lseek(hFile->fileDescriptor, 0, SEEK_END) + 1;
    lseek(hFile->fileDescriptor, currentPosition, SEEK_SET);
    return fileLength;
}

//** https://www.ibm.com/developerworks/systems/library/es-MigratingWin32toLinux.html
//https://www.ibm.com/developerworks/systems/library/es-win32linux.html
//https://www.ibm.com/developerworks/systems/library/es-win32linux-sem.html
HANDLE CreateFileMapping(HANDLE hFile, LPSECURITY_ATTRIBUTES lpFileMappingAttributes, DWORD flProtect, DWORD dwMaximumSizeHigh, DWORD dwMaximumSizeLow, LPCSTR lpName) {
    HANDLE result = malloc(sizeof(_HANDLE));
    result->handleType = HANDLE_TYPE_FILE_MAPPING;
    result->fileDescriptor = hFile->fileDescriptor;
    result->fileMappingSize = (dwMaximumSizeHigh << 32) & dwMaximumSizeLow;
    result->fileMappingAddress = NULL;
    return result;
}

//https://msdn.microsoft.com/en-us/library/Aa366761(v=VS.85).aspx
LPVOID MapViewOfFile(HANDLE hFileMappingObject, DWORD dwDesiredAccess, DWORD dwFileOffsetHigh, DWORD dwFileOffsetLow, SIZE_T dwNumberOfBytesToMap) {
    int prot = PROT_NONE;
    if(dwDesiredAccess & FILE_MAP_READ)
        prot |= PROT_READ;
    if(dwDesiredAccess & FILE_MAP_WRITE)
        prot |= PROT_WRITE;
    off_t __offset = (dwFileOffsetHigh << 32) & dwFileOffsetLow;
    hFileMappingObject->fileMappingAddress = mmap(NULL, hFileMappingObject->fileMappingSize, prot, MAP_PRIVATE, hFileMappingObject->fileDescriptor, __offset);
    return hFileMappingObject->fileMappingAddress;
}

// https://msdn.microsoft.com/en-us/library/aa366882(v=vs.85).aspx
BOOL UnmapViewOfFile(LPCVOID lpBaseAddress) {
    int result = munmap(lpBaseAddress, -1);
    return result == 0;
}

// https://github.com/NeoSmart/PEvents
HANDLE CreateEvent(WORD attr, BOOL is_manual_reset, BOOL is_signaled, WORD name)
{
//    if (nil == gEventLockDict)
//    {
//        gEventLockDict = [[NSMutableDictionary alloc] init];
//    }
//    ++gEventId;
//    NSNumber *key = [[NSNumber alloc] initWithInt: gEventId];
//    NSConditionLock *lock = [[NSConditionLock alloc] initWithCondition: 0];
//    [gEventLockDict setObject:lock forKey:key];
//    [lock release];
//    [key release];
////    if (NULL == gEventLock)
////    {
////        gEventLock = [[NSConditionLock alloc] initWithCondition: 0];
////    }

    return gEventId;
}

void SetEvent(HANDLE eventId)
{
//    NSNumber *key = [[NSNumber alloc] initWithInt: eventId];
//    NSConditionLock *lock = [gEventLockDict objectForKey: key];
//    [key release];
//    if (lock)
//    {
//        [lock lock];
//        [lock unlockWithCondition: eventId];
//    }
}

BOOL ResetEvent(HANDLE eventId)
{
//    NSNumber *key = [[NSNumber alloc] initWithInt: eventId];
//    NSConditionLock *lock = [gEventLockDict objectForKey: key];
//    [key release];
//    if (lock)
//    {
//        [lock lock];
//        [lock unlockWithCondition: 0];
//        return TRUE;
//    }
    return FALSE;
}

void DestroyEvent(HANDLE eventId)
{
//    NSNumber *key = [[NSNumber alloc] initWithInt: eventId];
//    NSConditionLock *lock = [gEventLockDict objectForKey: key];
//    if (lock)
//    {
//        [gEventLockDict removeObjectForKey: key];
//    }
//    [key release];
}

DWORD WaitForSingleObject(HANDLE eventId, int timeout)
{
    DWORD result = WAIT_OBJECT_0;
//    NSNumber *key = [[NSNumber alloc] initWithInt: eventId];
//    NSConditionLock *lock = [gEventLockDict objectForKey: key];
//    [key release];
//
//    if (nil == lock)
//        return WAIT_FAILED;
//
//    if (timeout > 0)
//    {
//        NSDate *timeoutDate = [[NSDate alloc] initWithTimeIntervalSinceNow: (timeout/1000.0)];
//        if (![lock lockWhenCondition:eventId beforeDate:timeoutDate])
//        result = WAIT_TIMEOUT;
//        [timeoutDate release];
//    }
//    else
//    {
//        [lock lockWhenCondition: eventId];
//        [lock unlockWithCondition: 0];
//    }
    return result;
}

#include <pthread.h>
HANDLE WINAPI CreateThread(LPSECURITY_ATTRIBUTES lpThreadAttributes, SIZE_T dwStackSize, LPTHREAD_START_ROUTINE lpStartAddress, LPVOID lpParameter, DWORD dwCreationFlags, LPDWORD lpThreadId) {
    pthread_t   *threadId;
    pthread_attr_t  attr;
    pthread_attr_init(&attr);
    if(dwStackSize)
        pthread_attr_setstacksize(&attr, dwStackSize);
    //Suspended
    //https://stackoverflow.com/questions/3140867/suspend-pthreads-without-using-condition
    //https://stackoverflow.com/questions/7953917/create-thread-in-suspended-mode-using-pthreads
    //http://man7.org/linux/man-pages/man3/pthread_create.3.html
    int pthreadResult = pthread_create(threadId, &attr, /*(void*(*)(void*))*/lpStartAddress, lpParameter);
    if(pthreadResult == 0) {
        HANDLE result = malloc(sizeof(_HANDLE));
        result->handleType = HANDLE_TYPE_THREAD;
        result->threadId = *threadId;
        return result;
    }
    return NULL;
}

DWORD ResumeThread(HANDLE hThread) {
    //TODO
    return 0;
}

BOOL WINAPI CloseHandle(HANDLE hObject) {
    //https://msdn.microsoft.com/en-us/9b84891d-62ca-4ddc-97b7-c4c79482abd9
    // Can be a thread/event/file handle!
    switch(hObject->handleType) {
        case HANDLE_TYPE_FILE: {
            int closeResult = close(hObject->fileDescriptor);
            if(closeResult >= 0) {
                hObject->fileDescriptor = 0;
                free(hObject);
                return TRUE;
            }
            break;
        }
        case HANDLE_TYPE_FILE_MAPPING: {
            int closeResult = UnmapViewOfFile(hObject->fileMappingAddress);
            if(closeResult == TRUE) {
                hObject->fileDescriptor = 0;
                hObject->fileMappingSize = 0;
                hObject->fileMappingAddress = NULL;
                free(hObject);
                return TRUE;
            }
            break;
        }
        case HANDLE_TYPE_EVENT:
            //free(hObject);
            return TRUE;
        case HANDLE_TYPE_THREAD:
            hObject->threadId = 0;
            free(hObject);
            return TRUE;
    }
    return FALSE;
}

void Sleep(int ms)
{
    struct timespec timeOut,remains;
    timeOut.tv_sec = 0;
    timeOut.tv_nsec = ms * 1000000; /* 50 milliseconds */
    nanosleep(&timeOut, &remains);
}

BOOL QueryPerformanceFrequency(PLARGE_INTEGER l) {
    //https://msdn.microsoft.com/en-us/library/windows/desktop/ms644904(v=vs.85).aspx
//    static struct mach_timebase_info timebase = { 0, 0 };
//    if (0 == timebase.denom)
//        mach_timebase_info(&timebase);
////    l->LowPart  = 1e9 * timebase.denom / timebase.numer;
    l->QuadPart = 1000000;
    return TRUE;
}

BOOL QueryPerformanceCounter(PLARGE_INTEGER l)
{
    struct timespec {
        time_t   tv_sec;        /* seconds */
        long     tv_nsec;       /* nanoseconds */
    } time;
    int clock_gettime(CLOCK_MONOTONIC, &time);
    l->QuadPart = time.tv_nsec;
    return TRUE;
}
void EnterCriticalSection(CRITICAL_SECTION *lock)
{
    pthread_mutex_lock(lock);
}

void LeaveCriticalSection(CRITICAL_SECTION *lock)
{
    pthread_mutex_unlock(lock);
}

DWORD GetPrivateProfileString(LPCTSTR lpAppName, LPCTSTR lpKeyName, LPCTSTR lpDefault, LPTSTR lpReturnedString, DWORD nSize, LPCTSTR lpFileName) {
    //TODO
#ifdef UNICODE
    wcsncpy(lpReturnedString, lpDefault, nSize);
#else
    strncpy(lpReturnedString, lpDefault, nSize);
#endif
    return 0;
}
UINT GetPrivateProfileInt(LPCTSTR lpAppName, LPCTSTR lpKeyName, INT nDefault, LPCTSTR lpFileName) {
    //TODO
    return nDefault;
}
BOOL WritePrivateProfileString(LPCTSTR lpAppName, LPCTSTR lpKeyName, LPCTSTR lpString, LPCTSTR lpFileName) {
    //TODO
    return 0;
}

/*
void SetTimer(void *, TimerType id, int msec, void *)
{
  switch(id) {
  case TIME_SHOW:
    doc_manager->getCurrent()->SetShowTimer(msec);
    break;
  case TIME_NEXT:
    doc_manager->getCurrent()->SetNextTimer(msec);
    break;
  }
}
*/


HGLOBAL WINAPI GlobalAlloc(UINT uFlags, SIZE_T dwBytes) {
    //TODO
    return NULL;
}
LPVOID WINAPI GlobalLock (HGLOBAL hMem) {
    //TODO
    return NULL;
}

BOOL WINAPI GlobalUnlock(HGLOBAL hMem) {
    //TODO
    return 0;
}

HGLOBAL WINAPI GlobalFree(HGLOBAL hMem) {
    //TODO
    return NULL;
}

BOOL GetOpenFileName(LPOPENFILENAME openFilename) {
    //TODO
    return FALSE;
}
BOOL GetSaveFileName(LPOPENFILENAME openFilename) {
    //TODO
    return FALSE;
}

HANDLE LoadImage(HINSTANCE hInst, LPCSTR name, UINT type, int cx, int cy, UINT fuLoad) {
    //TODO
    return NULL;
}

LRESULT SendMessage(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
    //TODO
    return NULL;
}


int MessageBox(HANDLE h, LPCTSTR szMessage, LPCTSTR title, int flags)
{
    int result = IDOK;
//#if !TARGET_OS_IPHONE
//    NSAlert *alert = [[NSAlert alloc] init];
//    [alert setMessageText: NSLocalizedString([NSString stringWithUTF8String: szMessage],@"")];
//    if (0 != (flags & MB_OK))
//    {
//        [alert addButtonWithTitle: NSLocalizedString(@"OK",@"")];
//    }
//    else if (0 != (flags & MB_YESNO))
//    {
//        [alert addButtonWithTitle: NSLocalizedString(@"Yes",@"")];
//        [alert addButtonWithTitle: NSLocalizedString(@"No",@"")];
//    }
//    else if (0 != (flags & MB_YESNOCANCEL))
//    {
//        [alert addButtonWithTitle: NSLocalizedString(@"Yes",@"")];
//        [alert addButtonWithTitle: NSLocalizedString(@"Cancel",@"")];
//        [alert addButtonWithTitle: NSLocalizedString(@"No",@"")];
//    }
//
//    if (0 != (flags & MB_ICONSTOP))
//    [alert setAlertStyle: NSAlertStyleCritical];
//    else if (0 != (flags & MB_ICONINFORMATION))
//    [alert setAlertStyle: NSAlertStyleInformational];
//
//    result = (int)[alert runModal];
//    [alert release];
//
//    if (0 != (flags & MB_OK))
//        result = IDOK;
//    else if (0 != (flags & MB_YESNO))
//        result = NSAlertFirstButtonReturn ? IDYES : IDNO;
//    else if (0 != (flags & MB_YESNOCANCEL))
//        result = NSAlertFirstButtonReturn ? IDYES :
//                 NSAlertSecondButtonReturn ? IDCANCEL : IDNO;
//#endif
    return result;
}

DWORD timeGetTime(void)
{
    time_t t = time(NULL);
    return (DWORD)(t * 1000);
}

BOOL GetSystemPowerStatus(LPSYSTEM_POWER_STATUS status)
{
    status->ACLineStatus = AC_LINE_ONLINE;
    return TRUE;
}


BOOL DestroyWindow(HWND hWnd) {
    //TODO
    return 0;
}

BOOL GetWindowPlacement(HWND hWnd, WINDOWPLACEMENT *lpwndpl) { return 0; }
BOOL SetWindowPlacement(HWND hWnd, CONST WINDOWPLACEMENT *lpwndpl) { return 0; }
BOOL InvalidateRect(HWND hWnd, CONST RECT *lpRect, BOOL bErase) { return 0; }

int GetObject(HANDLE h, int c, LPVOID pv) {
    //TODO
    return 0;
}
HGDIOBJ GetCurrentObject(HDC hdc, UINT type) {
    //TODO
    return NULL;
}
int SetStretchBltMode(HDC hdc, int mode) {
    //TODO
    return 0;
}
BOOL StretchBlt(HDC hdcDest, int xDest, int yDest, int wDest, int hDest, HDC hdcSrc, int xSrc, int ySrc, int wSrc, int hSrc, DWORD rop) {
    //TODO
    return 0;
}
HPALETTE CreatePalette(CONST LOGPALETTE * plpal) {
    //TODO
    return NULL;
}
HPALETTE SelectPalette(HDC hdc, HPALETTE hPal, BOOL bForceBkgd) {
    //TODO
    return NULL;
}
UINT RealizePalette(HDC hdc) {
    //TODO
    return 0;
}
HDC CreateCompatibleDC(HDC hdc) {
    //TODO
    return NULL;
}
BOOL DeleteDC(HDC hdc) {
    //TODO
    return 0;
}
HGDIOBJ GetStockObject(int i) {
    //TODO
    return NULL;
}
HGDIOBJ SelectObject(HDC hdc, HGDIOBJ h) {
    //TODO
    return NULL;
}
BOOL MoveToEx(HDC hdc, int x, int y, LPPOINT lppt) {
    //TODO
    return 0;
}
BOOL LineTo(HDC hdc, int x, int y) {
    //TODO
    return 0;
}
BOOL PatBlt(HDC hdc, int x, int y, int w, int h, DWORD rop) {
    //TODO
    return 0;
}
BOOL BitBlt(HDC hdc, int x, int y, int cx, int cy, HDC hdcSrc, int x1, int y1, DWORD rop) {
    //TODO
    return 0;
}
/* constants for CreateDIBitmap */
#define CBM_INIT        0x04L   /* initialize bitmap */
/* DIB color table identifiers */
#define DIB_RGB_COLORS      0 /* color table in RGBs */
#define DIB_PAL_COLORS      1 /* color table in palette indices */
HBITMAP CreateDIBitmap( HDC hdc, CONST BITMAPINFOHEADER *pbmih, DWORD flInit, CONST VOID *pjBits, CONST BITMAPINFO *pbmi, UINT iUsage) {
    //TODO
    return NULL;
}
HBITMAP CreateDIBSection(HDC hdc, CONST BITMAPINFO *pbmi, UINT usage, VOID **ppvBits, HANDLE hSection, DWORD offset) {
    //TODO
    return NULL;
}
HBITMAP CreateCompatibleBitmap( HDC hdc, int cx, int cy) {
    //TODO
    return NULL;
}
BOOL DeleteObject(HGDIOBJ ho) {
    //TODO
    return 0;
}
int GetDIBits(HDC hdc, HBITMAP hbm, UINT start, UINT cLines, LPVOID lpvBits, LPBITMAPINFO lpbmi, UINT usage) {
    //TODO
    return 0;
}
BOOL SetRect(LPRECT lprc, int xLeft, int yTop, int xRight, int yBottom) {
    //TODO
    return 0;
}
int SetWindowRgn(HWND hWnd, HRGN hRgn, BOOL bRedraw) {
    //TODO
    return 0;
}
HRGN ExtCreateRegion(CONST XFORM * lpx, DWORD nCount, CONST RGNDATA * lpData) {
    //TODO
    return NULL;
}
BOOL GdiFlush(void) {
    //TODO
    return 0;
}

BOOL WINAPI MessageBeep(UINT uType) {
    //TODO System beep
    return 1;
}

BOOL WINAPI OpenClipboard(HWND hWndNewOwner) {
    //TODO
    return 0;
}
BOOL WINAPI CloseClipboard(VOID) {
    //TODO
    return 0;
}

BOOL WINAPI EmptyClipboard(VOID) {
    //TODO
    return 0;
}

HANDLE WINAPI SetClipboardData(UINT uFormat,HANDLE hMem) {
    //TODO
    return NULL;
}

BOOL WINAPI IsClipboardFormatAvailable(UINT format) {
    //TODO
    return 0;
}

HANDLE WINAPI GetClipboardData(UINT uFormat) {
    //TODO
    return NULL;
}

MMRESULT timeSetEvent(UINT uDelay, UINT uResolution, LPTIMECALLBACK fptc, DWORD_PTR dwUser, UINT fuEvent) {
    //TODO
    return 0; //No error
}
MMRESULT timeKillEvent(UINT uTimerID) {
    //TODO
    return 0; //No error
}
MMRESULT timeGetDevCaps(LPTIMECAPS ptc, UINT cbtc) {
    //TODO
    return 0; //No error
}
MMRESULT timeBeginPeriod(UINT uPeriod) {
    //TODO
    return 0; //No error
}
MMRESULT timeEndPeriod(UINT uPeriod) {
    //TODO
    return 0; //No error
}
VOID GetLocalTime(LPSYSTEMTIME lpSystemTime) {
    //TODO
    return;
}

BOOL EnableWindow(HWND hWnd, BOOL bEnable) {
    //TODO
    return 0;
}
HWND GetDlgItem(HWND hDlg, int nIDDlgItem) {
    //TODO
    return NULL;
}
UINT GetDlgItemTextA(HWND hDlg, int nIDDlgItem, LPSTR lpString,int cchMax) {
    //TODO
    return 0;
}
BOOL SetDlgItemText(HWND hDlg, int nIDDlgItem, LPCSTR lpString) {
    //TODO
    return 0;
}
BOOL CheckDlgButton(HWND hDlg, int nIDButton, UINT uCheck) {
    //TODO
    return 0;
}
UINT IsDlgButtonChecked(HWND hDlg, int nIDButton) {
    //TODO
    return 0;
}
BOOL EndDialog(HWND hDlg, INT_PTR nResult) {
    //TODO
    return 0;
}
INT_PTR DialogBoxParam(HINSTANCE hInstance, LPCSTR lpTemplateName, HWND hWndParent, DLGPROC lpDialogFunc, LPARAM dwInitParam) {
    //TODO
    return NULL;
}
HANDLE  FindFirstFileA(LPCSTR lpFileName, LPWIN32_FIND_DATAA lpFindFileData) {
    //TODO
    return NULL;
}
BOOL FindNextFileA(HANDLE hFindFile, LPWIN32_FIND_DATAA lpFindFileData) {
    //TODO
    return 0;
}
BOOL FindClose(HANDLE hFindFile) {
    //TODO
    return 0;
}
BOOL SHGetPathFromIDListA(PCIDLIST_ABSOLUTE pidl, LPSTR pszPath) {
    //TODO
    return 0;
}
HRESULT SHGetMalloc(IMalloc **ppMalloc) {
    //TODO
    return 0;
}

INT_PTR DialogBoxParamA(HINSTANCE hInstance, LPCSTR lpTemplateName, HWND hWndParent, DLGPROC lpDialogFunc, LPARAM dwInitParam) {
    //TODO
    return NULL;
}
HCURSOR SetCursor(HCURSOR hCursor) {
    //TODO
    return NULL;
}
int MulDiv(int nNumber, int nNumerator, int nDenominator) {
    //TODO
    return 0;
}

BOOL GetKeyboardLayoutName(LPSTR pwszKLID) {
    //TODO
    return 0;
}

void DragAcceptFiles(HWND hWnd, BOOL fAccept) {
    //TODO
}



#ifdef UNICODE


int WINAPI wvsprintf(LPWSTR, LPCWSTR, va_list arglist) {
    return vswprintf(arg1, MAX_PATH, arg2, arglist);
}
DWORD GetFullPathName(LPCWSTR lpFileName, DWORD nBufferLength, LPWSTR lpBuffer, LPWSTR* lpFilePart) { return 0; }
LPWSTR lstrcpyn(LPWSTR lpString1, LPCWSTR lpString2,int iMaxLength) {
    return strcpy(lpString1, lpString2);
}
LPWSTR lstrcat(LPWSTR lpString1, LPCWSTR lpString2) {
    return NULL;
}
void __cdecl _wsplitpath(wchar_t const* _FullPath, wchar_t* _Drive, wchar_t* _Dir, wchar_t* _Filename, wchar_t* _Ext) {}
int WINAPI lstrcmp(LPCWSTR lpString1, LPCWSTR lpString2) {
    return wcscmp(lpString1, lpString2);
}
int lstrcmpi(LPCWSTR lpString1, LPCWSTR lpString2) {
    return wcscasecmp(lpString1, lpString2);
}
void _wmakepath(wchar_t _Buffer, wchar_t const* _Drive, wchar_t const* _Dir, wchar_t const* _Filename, wchar_t const* _Ext)
{
}

#else

int WINAPI wvsprintf(LPSTR arg1, LPCSTR arg2, va_list arglist) {
    return vsprintf(arg1, arg2, arglist);
}
DWORD GetFullPathName(LPCSTR lpFileName, DWORD nBufferLength, LPSTR lpBuffer, LPSTR* lpFilePart) { return 0; }
LPSTR lstrcpyn(LPSTR lpString1, LPCSTR lpString2,int iMaxLength) {
    return strcpy(lpString1, lpString2);
}
LPSTR lstrcat(LPSTR lpString1, LPCSTR lpString2) {
    return NULL;
}
void __cdecl _splitpath(char const* _FullPath, char* _Drive, char* _Dir, char* _Filename, char* _Ext) {}
int WINAPI lstrcmp(LPCSTR lpString1, LPCSTR lpString2) {
    return strcmp(lpString1, lpString2);
}
int lstrcmpi(LPCSTR lpString1, LPCSTR lpString2) {
    return strcasecmp(lpString1, lpString2);
}

void _makepath(char _Buffer, char const* _Drive, char const* _Dir, char const* _Filename, char const* _Ext)
{
}

#endif // !UNICODE




BOOL GetClientRect(HWND hWnd, LPRECT lpRect)
{
    return 0;
}