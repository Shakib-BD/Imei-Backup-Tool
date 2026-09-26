#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0601
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <richedit.h>
#include <commdlg.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <sstream>
#include <thread>
#include <mutex>
#include <atomic>
#include <direct.h>
#include <ctime>
#include <iomanip>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")

#ifndef FR_DOWN
#define FR_DOWN 0x00000001
#endif

#define IDI_APP_ICON        101
#define IDC_BTN_START       201
#define IDC_BTN_CHECK       202
#define IDC_BTN_CLEANUP     203
#define IDC_PROGRESS        204
#define IDC_EDIT_LOG        205
#define IDC_STATUS_TEXT     206
#define IDC_TAB_QCOM        207
#define IDC_TAB_MTK         208

const wchar_t* TEMP_TARGET_DIR = L"C:\\temp\\Imei-Backup";
const wchar_t* TEMP_ROOT_DIR   = L"C:\\temp";

// 64-byte rolling XOR encryption key
static const unsigned char ENCRYPTION_KEY[64] = {
    0x9E, 0x37, 0x79, 0xB9, 0x4F, 0x82, 0x1A, 0x7C,
    0xD3, 0x01, 0x55, 0xE8, 0x2A, 0xB4, 0x77, 0x19,
    0x88, 0x6C, 0xA3, 0x4D, 0x11, 0x90, 0x5F, 0x3E,
    0x7A, 0xC5, 0x23, 0x6B, 0x91, 0x0D, 0xE4, 0x38,
    0x54, 0x29, 0x8F, 0x13, 0xA7, 0xD0, 0x62, 0xEB,
    0x36, 0x71, 0x95, 0x48, 0x8E, 0x2B, 0xC9, 0x1F,
    0x63, 0x3D, 0x72, 0x50, 0xBF, 0x81, 0x0A, 0xDC,
    0x44, 0x99, 0x61, 0x17, 0xE2, 0x5D, 0x3C, 0x78
};

struct EmbeddedFileEntry {
    const char* filename;
    size_t original_size;
    std::vector<unsigned char> data;
};

HWND g_hWnd = NULL;
HWND g_hBtnStart = NULL;
HWND g_hBtnCheck = NULL;
HWND g_hBtnCleanup = NULL;
HWND g_hTabQcom = NULL;
HWND g_hTabMtk = NULL;
HWND g_hProgressBar = NULL;
HWND g_hEditLog = NULL;
HWND g_hStatusText = NULL;

HFONT g_hFontTitle = NULL;
HFONT g_hFontSub = NULL;
HFONT g_hFontBtn = NULL;
HFONT g_hFontSmall = NULL;
HFONT g_hFontConsole = NULL;

HBRUSH g_hBrushWindowBg = NULL;
HBRUSH g_hBrushCardBg = NULL;
HBRUSH g_hBrushConsoleBg = NULL;

std::atomic<bool> g_isWorkerRunning(false);
std::atomic<bool> g_isQcomSelected(true);
std::mutex g_logMutex;

struct PartitionTarget {
    const wchar_t* name;
    const wchar_t* aliasFallback;
    const wchar_t* description;
};

// Qualcomm IMEI & Radio Partitions
static const PartitionTarget QCOM_PARTITIONS[] = {
    { L"modemst1", L"",          L"Modem NV Data 1 (IMEI 1)" },
    { L"modemst2", L"",          L"Modem NV Data 2 (IMEI 2)" },
    { L"fsg",      L"",          L"Golden Copy NV" },
    { L"fsc",      L"",          L"Modem NV Configuration" },
    { L"persist",  L"",          L"Persistent Calibration" }
};

// MediaTek (MTK) IMEI, NVRAM & Radio Partitions (Standard on Eng ROMs)
static const PartitionTarget MTK_PARTITIONS[] = {
    { L"nvram",    L"",          L"NVRAM (Hardware IMEI & RF Calibration)" },
    { L"nvdata",   L"",          L"NVDATA (Dynamic IMEI & Carrier Info)" },
    { L"nvcfg",    L"",          L"NVCFG (NVRAM Configuration)" },
    { L"protect1", L"protect_f", L"Protect 1 / Factory SIM Lock" },
    { L"protect2", L"protect_s", L"Protect 2 / Storage SIM Lock" },
    { L"proinfo",  L"pro_info",  L"Product Info & Serial Data" },
    { L"persist",  L"",          L"Persistent Settings" },
    { L"seccfg",   L"sec1",      L"Security Configuration" }
};

void EncryptDecryptBuffer(unsigned char* buffer, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        unsigned char k = ENCRYPTION_KEY[i % sizeof(ENCRYPTION_KEY)];
        buffer[i] ^= (unsigned char)(k + (i & 0xFF));
    }
}

std::wstring GetAppDirectory() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    PathRemoveFileSpecW(exePath);
    return std::wstring(exePath);
}

bool DeleteDirectoryRecursively(const std::wstring& refPath) {
    std::wstring searchPattern = refPath + L"\\*.*";
    WIN32_FIND_DATAW findData;
    HANDLE hFind = FindFirstFileW(searchPattern.c_str(), &findData);

    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(findData.cFileName, L".") != 0 && wcscmp(findData.cFileName, L"..") != 0) {
                std::wstring filePath = refPath + L"\\" + findData.cFileName;
                if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                    DeleteDirectoryRecursively(filePath);
                } else {
                    SetFileAttributesW(filePath.c_str(), FILE_ATTRIBUTE_NORMAL);
                    DeleteFileW(filePath.c_str());
                }
            }
        } while (FindNextFileW(hFind, &findData));
        FindClose(hFind);
    }
    return RemoveDirectoryW(refPath.c_str()) != FALSE;
}

void KillAdbAndCleanup() {
    STARTUPINFOA si = { sizeof(STARTUPINFOA) };
    PROCESS_INFORMATION pi = { 0 };
    si.dwFlags |= STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    char killCmd[] = "taskkill.exe /F /IM adb.exe /T";
    if (CreateProcessA(NULL, killCmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 3000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    Sleep(400);

    if (PathFileExistsW(TEMP_TARGET_DIR)) {
        DeleteDirectoryRecursively(TEMP_TARGET_DIR);
    }
    RemoveDirectoryW(TEMP_ROOT_DIR);
}

void AppendLog(const std::wstring& message) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (!g_hEditLog || !IsWindow(g_hEditLog)) return;

    int length = GetWindowTextLengthW(g_hEditLog);
    SendMessageW(g_hEditLog, EM_SETSEL, (WPARAM)length, (LPARAM)length);
    SendMessageW(g_hEditLog, EM_REPLACESEL, FALSE, (LPARAM)message.c_str());
    SendMessageW(g_hEditLog, EM_SCROLLCARET, 0, 0);
}

void SetStatus(const std::wstring& status, int progressValue = -1) {
    if (g_hStatusText && IsWindow(g_hStatusText)) {
        SetWindowTextW(g_hStatusText, status.c_str());
    }
    if (progressValue >= 0 && g_hProgressBar && IsWindow(g_hProgressBar)) {
        SendMessageW(g_hProgressBar, PBM_SETPOS, (WPARAM)progressValue, 0);
    }
}

bool ExecuteProcessCapture(const std::wstring& commandLine, std::wstring& output, bool streamToLog = true) {
    HANDLE hReadPipe, hWritePipe;
    SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };

    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
        return false;
    }
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si = { sizeof(STARTUPINFOW) };
    PROCESS_INFORMATION pi = { 0 };
    si.cb = sizeof(STARTUPINFOW);
    si.dwFlags |= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdInput = NULL;
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;
    si.wShowWindow = SW_HIDE;

    std::vector<wchar_t> cmdBuffer(commandLine.begin(), commandLine.end());
    cmdBuffer.push_back(L'\0');

    BOOL created = CreateProcessW(
        NULL,
        cmdBuffer.data(),
        NULL,
        NULL,
        TRUE,
        CREATE_NO_WINDOW,
        NULL,
        TEMP_TARGET_DIR,
        &si,
        &pi
    );

    CloseHandle(hWritePipe);

    if (!created) {
        CloseHandle(hReadPipe);
        return false;
    }

    char buffer[512];
    DWORD bytesRead = 0;
    std::string sOutput;

    while (ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
        buffer[bytesRead] = '\0';
        sOutput += buffer;

        if (streamToLog) {
            int wlen = MultiByteToWideChar(CP_OEMCP, 0, buffer, (int)bytesRead, NULL, 0);
            if (wlen > 0) {
                std::wstring wChunk(wlen, 0);
                MultiByteToWideChar(CP_OEMCP, 0, buffer, (int)bytesRead, &wChunk[0], wlen);
                AppendLog(wChunk);
            }
        }
    }

    WaitForSingleObject(pi.hProcess, 30000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hReadPipe);

    int wtotal = MultiByteToWideChar(CP_OEMCP, 0, sOutput.c_str(), (int)sOutput.size(), NULL, 0);
    if (wtotal > 0) {
        output.resize(wtotal);
        MultiByteToWideChar(CP_OEMCP, 0, sOutput.c_str(), (int)sOutput.size(), &output[0], wtotal);
    } else {
        output.clear();
    }

    return true;
}

std::wstring GetDeviceCodename() {
    std::wstring adbBinary = std::wstring(TEMP_TARGET_DIR) + L"\\adb.exe";
    std::wstring codenameCmd = L"\"" + adbBinary + L"\" shell \"getprop ro.product.device || getprop ro.build.product || getprop ro.product.model\"";
    std::wstring out;
    ExecuteProcessCapture(codenameCmd, out, false);

    // Clean whitespace and sanitize
    std::wstring cleanName;
    for (wchar_t c : out) {
        if (c >= 32 && c <= 126 && c != L' ' && c != L'/' && c != L'\\' && c != L':' && c != L'*' && c != L'?' && c != L'\"' && c != L'<' && c != L'>' && c != L'|') {
            cleanName += c;
        }
    }

    if (cleanName.empty() || cleanName == L"null") {
        cleanName = L"Device";
    }
    return cleanName;
}

std::wstring GetCurrentTimestamp() {
    std::time_t t = std::time(nullptr);
    std::tm tm;
    localtime_s(&tm, &t);
    std::wstringstream ss;
    ss << std::put_time(&tm, L"%Y%m%d_%H%M%S");
    return ss.str();
}

bool CompressFolderToZip(const std::wstring& sourceDir, const std::wstring& zipFilePath) {
    // Uses native Windows PowerShell Compress-Archive (supported on Windows 7 SP1+, 8, 10, 11)
    std::wstring psCmd = L"powershell.exe -NoProfile -NonInteractive -Command \""
                         L"Compress-Archive -Path '" + sourceDir + L"\\*' -DestinationPath '" + zipFilePath + L"' -Force\"";

    STARTUPINFOW si = { sizeof(STARTUPINFOW) };
    PROCESS_INFORMATION pi = { 0 };
    si.dwFlags |= STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    std::vector<wchar_t> cmdBuffer(psCmd.begin(), psCmd.end());
    cmdBuffer.push_back(L'\0');

    if (CreateProcessW(NULL, cmdBuffer.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 60000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        WIN32_FILE_ATTRIBUTE_DATA fad;
        if (GetFileAttributesExW(zipFilePath.c_str(), GetFileExInfoStandard, &fad)) {
            return (fad.nFileSizeLow > 0 || fad.nFileSizeHigh > 0);
        }
    }
    return false;
}

std::wstring FindPartitionNode(const std::wstring& partitionName, const std::wstring& aliasName) {
    std::wstring adbBinary = std::wstring(TEMP_TARGET_DIR) + L"\\adb.exe";

    std::wstring upperPart = partitionName;
    for (auto& c : upperPart) c = towupper(c);

    std::wstring upperAlias = aliasName;
    for (auto& c : upperAlias) c = towupper(c);

    // Build the prioritized test names list (original case, UPPERCASE, alias, UPPERCASE alias)
    std::wstring testNames = partitionName + L" " + upperPart;
    if (!aliasName.empty()) {
        testNames += L" " + aliasName + L" " + upperAlias;
    }

    // Universal multi-tier probe script executed on Android:
    std::wstring probeCmd = L"\"" + adbBinary + L"\" shell \""
        L"for N in " + testNames + L"; do "
        L"for P in "
        L"/dev/block/by-name/$N "
        L"/dev/block/bootdevice/by-name/$N "
        L"/dev/block/platform/bootdevice/by-name/$N "
        L"/dev/block/platform/soc/*/by-name/$N "
        L"/dev/block/platform/soc/*/*/by-name/$N "
        L"/dev/block/platform/*/by-name/$N "
        L"/dev/block/platform/*/*/by-name/$N "
        L"/dev/block/mapper/$N; do "
        L"if [ -b $P ] || [ -e $P ]; then "
        L"R=$(readlink -f $P 2>/dev/null || echo $P); "
        L"echo $R; exit 0; "
        L"fi; done; "
        L"F=$(find /dev/block -iname $N 2>/dev/null | head -n 1); "
        L"if [ -b \\\"$F\\\" ] || [ -e \\\"$F\\\" ]; then "
        L"R=$(readlink -f $F 2>/dev/null || echo $F); "
        L"echo $R; exit 0; "
        L"fi; done\"";

    std::wstring out;
    ExecuteProcessCapture(probeCmd, out, false);

    std::wstringstream ss(out);
    std::wstring line;
    while (std::getline(ss, line)) {
        size_t first = line.find_first_not_of(L" \t\r\n");
        size_t last = line.find_last_not_of(L" \t\r\n");
        if (first != std::wstring::npos && last != std::wstring::npos) {
            std::wstring node = line.substr(first, last - first + 1);
            if (node.find(L"/dev/block") != std::wstring::npos) {
                return node;
            }
        }
    }

    // Modern Android fallback if probe yields no match
    return L"/dev/block/by-name/" + partitionName;
}

bool ExecuteDumpPartitionUniversal(const PartitionTarget& target, const std::wstring& destinationFilePath) {
    std::wstring adbBinary = std::wstring(TEMP_TARGET_DIR) + L"\\adb.exe";
    std::wstring partNode = FindPartitionNode(target.name, target.aliasFallback);

    std::wstring checkCmd = L"\"" + adbBinary + L"\" shell \"[ -e " + partNode + L" ] || [ -b " + partNode + L" ] && echo OK\"";
    std::wstring checkOut;
    ExecuteProcessCapture(checkCmd, checkOut, false);
    if (checkOut.find(L"OK") == std::wstring::npos) {
        return false;
    }

    DeleteFileW(destinationFilePath.c_str());

    // Method 1: Stream directly via cmd redirection
    std::wstring streamCmd = L"cmd.exe /c \"\"" + adbBinary + L"\" exec-out dd if=" + partNode + L" bs=4M > \"" + destinationFilePath + L"\"\"";
    std::vector<wchar_t> cmdBuffer(streamCmd.begin(), streamCmd.end());
    cmdBuffer.push_back(L'\0');

    STARTUPINFOW si = { sizeof(STARTUPINFOW) };
    PROCESS_INFORMATION pi = { 0 };
    si.dwFlags |= STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    if (CreateProcessW(NULL, cmdBuffer.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, TEMP_TARGET_DIR, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 35000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (GetFileAttributesExW(destinationFilePath.c_str(), GetFileExInfoStandard, &fad)) {
        if (fad.nFileSizeLow > 0 || fad.nFileSizeHigh > 0) {
            return true;
        }
    }

    // Clean up empty file before Method 2
    DeleteFileW(destinationFilePath.c_str());

    // Method 2: Staged pull via /tmp/ (Reliable in Engineering Recovery)
    std::wstring tempDeviceFile = L"/tmp/dump_" + std::wstring(target.name) + L".img";
    std::wstring ddStageCmd = L"\"" + adbBinary + L"\" shell \"dd if=" + partNode + L" of=" + tempDeviceFile + L" bs=4M\"";
    std::wstring ddOut;
    ExecuteProcessCapture(ddStageCmd, ddOut, false);

    std::wstring pullCmd = L"\"" + adbBinary + L"\" pull " + tempDeviceFile + L" \"" + destinationFilePath + L"\"";
    std::wstring pullOut;
    ExecuteProcessCapture(pullCmd, pullOut, false);

    std::wstring rmCmd = L"\"" + adbBinary + L"\" shell \"rm -f " + tempDeviceFile + L"\"";
    std::wstring rmOut;
    ExecuteProcessCapture(rmCmd, rmOut, false);

    if (GetFileAttributesExW(destinationFilePath.c_str(), GetFileExInfoStandard, &fad)) {
        if (fad.nFileSizeLow > 0 || fad.nFileSizeHigh > 0) {
            return true;
        }
    }

    DeleteFileW(destinationFilePath.c_str());
    return false;
}

bool DecryptAndExtractPayload(const std::wstring& targetDirectory) {
    CreateDirectoryW(TEMP_ROOT_DIR, NULL);
    CreateDirectoryW(targetDirectory.c_str(), NULL);

    const wchar_t* expectedFiles[] = {
        L"adb.exe",
        L"AdbWinApi.dll",
        L"AdbWinUsbApi.dll",
        L"libwinpthread-1.dll"
    };

    bool allExist = true;
    for (const wchar_t* fileName : expectedFiles) {
        std::wstring checkPath = targetDirectory + L"\\" + fileName;
        WIN32_FILE_ATTRIBUTE_DATA fad;
        if (!GetFileAttributesExW(checkPath.c_str(), GetFileExInfoStandard, &fad) || (fad.nFileSizeLow == 0 && fad.nFileSizeHigh == 0)) {
            allExist = false;
            break;
        }
    }

    if (allExist) return true;

    HRSRC hRes = FindResourceW(NULL, MAKEINTRESOURCEW(2001), (LPCWSTR)RT_RCDATA);
    if (!hRes) {
        std::wstring appDir = GetAppDirectory();
        bool foundAny = false;
        for (const wchar_t* fileName : expectedFiles) {
            std::wstring srcFile = appDir + L"\\" + fileName;
            std::wstring destFile = targetDirectory + L"\\" + fileName;
            if (PathFileExistsW(srcFile.c_str())) {
                CopyFileW(srcFile.c_str(), destFile.c_str(), FALSE);
                foundAny = true;
            }
        }
        if (foundAny) {
            AppendLog(L"[INFO] Deployed ADB drivers from local folder to C:\\temp\\Imei-Backup\r\n");
            return true;
        }
        AppendLog(L"[ERROR] Embedded ADB payload resource not found!\r\n");
        return false;
    }

    HGLOBAL hLoaded = LoadResource(NULL, hRes);
    if (!hLoaded) return false;

    DWORD resSize = SizeofResource(NULL, hRes);
    const unsigned char* pData = (const unsigned char*)LockResource(hLoaded);
    if (!pData || resSize < 4) return false;

    size_t offset = 0;
    DWORD fileCount = 0;
    memcpy(&fileCount, pData + offset, sizeof(DWORD));
    offset += sizeof(DWORD);

    for (DWORD i = 0; i < fileCount && offset < resSize; ++i) {
        DWORD nameLen = 0;
        memcpy(&nameLen, pData + offset, sizeof(DWORD));
        offset += sizeof(DWORD);

        std::string filename((const char*)(pData + offset), nameLen);
        offset += nameLen;

        DWORD dataLen = 0;
        memcpy(&dataLen, pData + offset, sizeof(DWORD));
        offset += sizeof(DWORD);

        std::vector<unsigned char> fileBuffer(dataLen);
        memcpy(fileBuffer.data(), pData + offset, dataLen);
        offset += dataLen;

        EncryptDecryptBuffer(fileBuffer.data(), fileBuffer.size());

        std::wstring outPath = targetDirectory + L"\\" + std::wstring(filename.begin(), filename.end());
        HANDLE hOut = CreateFileW(outPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hOut != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            WriteFile(hOut, fileBuffer.data(), (DWORD)fileBuffer.size(), &written, NULL);
            CloseHandle(hOut);
        } else {
            DWORD err = GetLastError();
            if (err == ERROR_SHARING_VIOLATION && PathFileExistsW(outPath.c_str())) {
                continue;
            }
            AppendLog(L"[ERROR] Could not write decrypted file: " + outPath + L"\r\n");
            return false;
        }
    }

    AppendLog(L"[STATUS] Encrypted ADB engine mounted at C:\\temp\\Imei-Backup\r\n");
    return true;
}

void BackupWorkerThread() {
    g_isWorkerRunning = true;
    EnableWindow(g_hBtnStart, FALSE);
    EnableWindow(g_hBtnCheck, FALSE);

    bool isQcom = g_isQcomSelected.load();
    std::wstring platformName = isQcom ? L"Qualcomm (Snapdragon)" : L"MediaTek (MTK)";

    SetStatus(L"Mounting ADB driver suite...", 5);
    AppendLog(L"\r\n------------------------------------------\r\n");
    AppendLog(L"[*] TARGET CHIPSET : " + platformName + L"\r\n");
    AppendLog(L"[*] INITIATING IMEI EXTRACTION SEQUENCE\r\n");
    AppendLog(L"------------------------------------------\r\n");

    if (!DecryptAndExtractPayload(TEMP_TARGET_DIR)) {
        SetStatus(L"Initialization error. Aborted.", 0);
        g_isWorkerRunning = false;
        EnableWindow(g_hBtnStart, TRUE);
        EnableWindow(g_hBtnCheck, TRUE);
        return;
    }

    SetStatus(L"Querying device codename...", 8);
    std::wstring codename = GetDeviceCodename();
    std::wstring timestamp = GetCurrentTimestamp();
    std::wstring zipBaseName = L"IMEI-Backup-" + codename + L"-" + timestamp;

    wchar_t desktopPath[MAX_PATH];
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desktopPath))) {
        wcscpy_s(desktopPath, L"C:\\Imei-Backup");
    }

    // Temporary staging directory for partition images before ZIP archive creation
    std::wstring stagingDir = std::wstring(TEMP_TARGET_DIR) + L"\\" + zipBaseName;
    std::wstring finalZipPath = std::wstring(desktopPath) + L"\\" + zipBaseName + L".zip";

    CreateDirectoryW(stagingDir.c_str(), NULL);

    AppendLog(L"[INFO] Device Detected: " + codename + L"\r\n");
    AppendLog(L"[TARGET ZIP] " + finalZipPath + L"\r\n");
    SetStatus(L"Checking device connection and root...", 12);

    std::wstring adbBinary = std::wstring(TEMP_TARGET_DIR) + L"\\adb.exe";

    std::wstring rootCmd = L"\"" + adbBinary + L"\" root";
    std::wstring rootOut;
    ExecuteProcessCapture(rootCmd, rootOut, false);
    if (!rootOut.empty()) {
        AppendLog(L"[ROOT] " + rootOut);
    }
    Sleep(800);

    std::wstring seCmd = L"\"" + adbBinary + L"\" shell \"setenforce 0 2>/dev/null\"";
    std::wstring seOut;
    ExecuteProcessCapture(seCmd, seOut, false);

    std::wstring adbCheckCmd = L"\"" + adbBinary + L"\" get-state";
    std::wstring checkOut;
    ExecuteProcessCapture(adbCheckCmd, checkOut, false);

    bool isConnected = (checkOut.find(L"device") != std::wstring::npos || checkOut.find(L"recovery") != std::wstring::npos);

    if (!isConnected) {
        std::wstring devListCmd = L"\"" + adbBinary + L"\" devices";
        std::wstring devListOut;
        ExecuteProcessCapture(devListCmd, devListOut, false);
        if (devListOut.find(L"\tdevice") != std::wstring::npos || devListOut.find(L"\trecovery") != std::wstring::npos) {
            isConnected = true;
        }
    }

    if (!isConnected) {
        AppendLog(L"[!] ERROR: No authorized device detected!\r\n");
        AppendLog(L"    Boot phone into Recovery Mode or enable USB Debugging.\r\n");
        SetStatus(L"Device not found. Reconnect phone.", 0);
        g_isWorkerRunning = false;
        EnableWindow(g_hBtnStart, TRUE);
        EnableWindow(g_hBtnCheck, TRUE);
        return;
    }

    const PartitionTarget* targetList = isQcom ? QCOM_PARTITIONS : MTK_PARTITIONS;
    size_t targetCount = isQcom ? (sizeof(QCOM_PARTITIONS) / sizeof(QCOM_PARTITIONS[0])) 
                                : (sizeof(MTK_PARTITIONS) / sizeof(MTK_PARTITIONS[0]));

    int successfulBackups = 0;

    for (size_t i = 0; i < targetCount; ++i) {
        const auto& part = targetList[i];
        std::wstring destFile = stagingDir + L"\\" + part.name + L".img";

        int progress = 20 + (int)(((double)(i + 1) / targetCount) * 65);

        std::wstringstream ss;
        ss << L"[" << (i + 1) << L"/" << targetCount << L"] Dumping " << part.name << L" (" << part.description << L")...";
        SetStatus(ss.str(), progress);
        AppendLog(ss.str() + L"\r\n");

        if (ExecuteDumpPartitionUniversal(part, destFile)) {
            WIN32_FILE_ATTRIBUTE_DATA fi;
            long long fileSize = 0;
            if (GetFileAttributesExW(destFile.c_str(), GetFileExInfoStandard, &fi)) {
                fileSize = ((long long)fi.nFileSizeHigh << 32) | fi.nFileSizeLow;
            }

            std::wstringstream okLog;
            okLog << L"    ✔ [SUCCESS] " << part.name << L".img -> ";
            if (fileSize >= 1024 * 1024) {
                okLog << (fileSize / (1024 * 1024)) << L" MB\r\n";
            } else if (fileSize >= 1024) {
                okLog << (fileSize / 1024) << L" KB\r\n";
            } else {
                okLog << fileSize << L" Bytes\r\n";
            }
            AppendLog(okLog.str());
            successfulBackups++;
        } else {
            AppendLog(L"    ✖ [SKIPPED] " + std::wstring(part.name) + L" node not present on device\r\n");
        }
    }

    if (successfulBackups > 0) {
        SetStatus(L"Compressing backup into ZIP archive...", 90);
        AppendLog(L"\r\n[*] Compressing partitions into direct ZIP archive...\r\n");

        if (CompressFolderToZip(stagingDir, finalZipPath)) {
            SetStatus(L"ZIP Archive created successfully!", 100);
            AppendLog(L"[DONE] Archive successfully built:\r\n  " + finalZipPath + L"\r\n");
            
            // Clean up temporary uncompressed partition staging directory
            DeleteDirectoryRecursively(stagingDir);

            // Highlight the zip file in Windows File Explorer
            std::wstring selectCmd = L"/select,\"" + finalZipPath + L"\"";
            ShellExecuteW(NULL, L"open", L"explorer.exe", selectCmd.c_str(), NULL, SW_SHOWNORMAL);
        } else {
            SetStatus(L"ZIP creation failed. Raw files kept in folder.", 100);
            AppendLog(L"[!] Could not build ZIP archive. Partition dumps preserved in staging directory.\r\n");
            ShellExecuteW(NULL, L"open", stagingDir.c_str(), NULL, NULL, SW_SHOWNORMAL);
        }
    } else {
        SetStatus(L"Backup failed. No partitions could be read.", 0);
        AppendLog(L"[!] WARNING: Partitions could not be read. Ensure Eng ROM or Rooted Recovery.\r\n");
        DeleteDirectoryRecursively(stagingDir);
    }

    AppendLog(L"------------------------------------------\r\n");

    g_isWorkerRunning = false;
    EnableWindow(g_hBtnStart, TRUE);
    EnableWindow(g_hBtnCheck, TRUE);
}

void CheckDeviceThread() {
    g_isWorkerRunning = true;
    EnableWindow(g_hBtnStart, FALSE);
    EnableWindow(g_hBtnCheck, FALSE);

    SetStatus(L"Mounting ADB driver suite...", 25);
    if (!DecryptAndExtractPayload(TEMP_TARGET_DIR)) {
        SetStatus(L"Extraction failed.", 0);
        g_isWorkerRunning = false;
        EnableWindow(g_hBtnStart, TRUE);
        EnableWindow(g_hBtnCheck, TRUE);
        return;
    }

    SetStatus(L"Querying connected hardware...", 60);
    AppendLog(L"\r\n--- ADB HARDWARE PROBE ---\r\n");

    std::wstring adbBinary = std::wstring(TEMP_TARGET_DIR) + L"\\adb.exe";
    std::wstring cmdLine = L"\"" + adbBinary + L"\" devices -l";
    std::wstring output;
    ExecuteProcessCapture(cmdLine, output, true);

    SetStatus(L"Hardware probe completed.", 100);
    g_isWorkerRunning = false;
    EnableWindow(g_hBtnStart, TRUE);
    EnableWindow(g_hBtnCheck, TRUE);
}

int RunPackerMode() {
    std::cout << "========================================================\n";
    std::cout << "  IMEI Backup Tool - Encrypted Payload Generator        \n";
    std::cout << "========================================================\n";
    std::cout << "[*] Scanning for ADB runtime files in current folder...\n";

    const char* targetFiles[] = {
        "adb.exe",
        "AdbWinApi.dll",
        "AdbWinUsbApi.dll",
        "libwinpthread-1.dll"
    };

    std::vector<EmbeddedFileEntry> entries;
    for (const char* fName : targetFiles) {
        std::ifstream file(fName, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            std::cout << "[-] Missing file: " << fName << " (Ensure it exists in this folder!)\n";
            return 1;
        }
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        std::vector<unsigned char> data((size_t)size);
        if (!file.read((char*)data.data(), size)) {
            std::cout << "[-] Failed to read: " << fName << "\n";
            return 1;
        }

        std::cout << "[+] Found " << fName << " (" << size << " bytes). Encrypting...\n";
        EncryptDecryptBuffer(data.data(), data.size());

        EmbeddedFileEntry entry;
        entry.filename = fName;
        entry.original_size = (size_t)size;
        entry.data = std::move(data);
        entries.push_back(entry);
    }

    std::ofstream outBin("payload.bin", std::ios::binary);
    if (!outBin.is_open()) {
        std::cout << "[-] Failed to create output file: payload.bin\n";
        return 1;
    }

    DWORD fileCount = (DWORD)entries.size();
    outBin.write((const char*)&fileCount, sizeof(DWORD));

    for (const auto& entry : entries) {
        DWORD nameLen = (DWORD)strlen(entry.filename);
        outBin.write((const char*)&nameLen, sizeof(DWORD));
        outBin.write(entry.filename, nameLen);

        DWORD dataLen = (DWORD)entry.data.size();
        outBin.write((const char*)&dataLen, sizeof(DWORD));
        outBin.write((const char*)entry.data.data(), dataLen);
    }

    outBin.close();
    std::cout << "\n[SUCCESS] 'payload.bin' created successfully!\n";
    std::cout << "[i] All files protected with rolling cipher against 7-Zip/WinRAR inspection.\n";
    return 0;
}

void EnableImmersiveDarkMode(HWND hWnd) {
    HMODULE hDwm = LoadLibraryW(L"dwmapi.dll");
    if (hDwm) {
        typedef HRESULT (WINAPI *pfnDwmSetWindowAttribute)(HWND, DWORD, LPCVOID, DWORD);
        pfnDwmSetWindowAttribute setAttr = (pfnDwmSetWindowAttribute)GetProcAddress(hDwm, "DwmSetWindowAttribute");
        if (setAttr) {
            BOOL darkMode = TRUE;
            if (FAILED(setAttr(hWnd, 20, &darkMode, sizeof(darkMode)))) {
                setAttr(hWnd, 19, &darkMode, sizeof(darkMode));
            }
        }
        FreeLibrary(hDwm);
    }
}

void DrawModernPill(LPDRAWITEMSTRUCT pDIS, const wchar_t* label, bool isSelected, COLORREF activeAccent) {
    HDC hdc = pDIS->hDC;
    RECT rc = pDIS->rcItem;
    bool isDown = (pDIS->itemState & ODS_SELECTED);

    // Erase the full background with the window's dark canvas to completely eliminate corner dots
    FillRect(hdc, &rc, g_hBrushWindowBg);

    // Minimalist surfaces: Subtle slate container when inactive, crisp accent outline when active
    COLORREF bgColor = isSelected ? RGB(18, 26, 40) : RGB(20, 23, 31);
    COLORREF borderColor = isSelected ? activeAccent : RGB(38, 44, 57);
    COLORREF textColor = isSelected ? RGB(255, 255, 255) : RGB(148, 163, 184);

    if (isDown) {
        bgColor = RGB(13, 17, 26);
    }

    HBRUSH hBr = CreateSolidBrush(bgColor);
    HPEN hPen = CreatePen(PS_SOLID, 1, borderColor);

    HGDIOBJ oldBr = SelectObject(hdc, hBr);
    HGDIOBJ oldPen = SelectObject(hdc, hPen);

    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 6, 6);

    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldPen);
    DeleteObject(hBr);
    DeleteObject(hPen);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, textColor);
    SelectObject(hdc, g_hFontBtn);

    RECT textRc = rc;
    if (isDown) {
        OffsetRect(&textRc, 0, 1);
    }
    DrawTextW(hdc, label, -1, &textRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void DrawModernButton(LPDRAWITEMSTRUCT pDIS, COLORREF bgNormal, COLORREF borderCol, COLORREF textCol, const wchar_t* text) {
    HDC hdc = pDIS->hDC;
    RECT rc = pDIS->rcItem;
    bool isDown = (pDIS->itemState & ODS_SELECTED);
    bool isDisabled = (pDIS->itemState & ODS_DISABLED);

    // Erase the full background with the window's dark canvas to completely eliminate corner dots
    FillRect(hdc, &rc, g_hBrushWindowBg);

    COLORREF finalBg = isDown ? RGB(GetRValue(bgNormal) * 3 / 4, GetGValue(bgNormal) * 3 / 4, GetBValue(bgNormal) * 3 / 4) : bgNormal;
    if (isDisabled) {
        finalBg = RGB(19, 22, 29);
        borderCol = RGB(30, 35, 45);
        textCol = RGB(75, 85, 99);
    }

    HBRUSH hBr = CreateSolidBrush(finalBg);
    HPEN hPen = CreatePen(PS_SOLID, 1, borderCol);

    HGDIOBJ oldBr = SelectObject(hdc, hBr);
    HGDIOBJ oldPen = SelectObject(hdc, hPen);

    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 6, 6);

    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldPen);
    DeleteObject(hBr);
    DeleteObject(hPen);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, textCol);
    SelectObject(hdc, g_hFontBtn);

    if (isDown) {
        OffsetRect(&rc, 0, 1);
    }
    DrawTextW(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        EnableImmersiveDarkMode(hWnd);

        // Permanently suppress Windows default dashed focus rectangles
        SendMessageW(hWnd, WM_CHANGEUISTATE, MAKEWPARAM(UIS_SET, UISF_HIDEFOCUS), 0);

        INITCOMMONCONTROLSEX icex = { sizeof(INITCOMMONCONTROLSEX), ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES };
        InitCommonControlsEx(&icex);

        // Harmonized typography scale
        g_hFontTitle   = CreateFontW(15, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        g_hFontSub     = CreateFontW(11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        g_hFontBtn     = CreateFontW(12, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        g_hFontSmall   = CreateFontW(10, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        g_hFontConsole = CreateFontW(12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");

        // Canvas Palette
        g_hBrushWindowBg  = CreateSolidBrush(RGB(13, 15, 20));     // #0D0F14 Sleek Obsidian
        g_hBrushCardBg    = CreateSolidBrush(RGB(20, 23, 31));     // #14171F
        g_hBrushConsoleBg = CreateSolidBrush(RGB(10, 11, 15));     // #0A0B0F

        // Minimalist Dual Chipset Pills (Height: 30px)
        g_hTabQcom = CreateWindowW(L"BUTTON", L"Qualcomm Snapdragon",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            16, 72, 218, 30, hWnd, (HMENU)IDC_TAB_QCOM, GetModuleHandle(NULL), NULL);

        g_hTabMtk = CreateWindowW(L"BUTTON", L"MediaTek (MTK)",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            242, 72, 218, 30, hWnd, (HMENU)IDC_TAB_MTK, GetModuleHandle(NULL), NULL);

        // Minimalist Action Row (Height: 32px)
        g_hBtnStart = CreateWindowW(L"BUTTON", L"⚡ Backup Partitions",
            WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
            16, 110, 172, 32, hWnd, (HMENU)IDC_BTN_START, GetModuleHandle(NULL), NULL);

        g_hBtnCheck = CreateWindowW(L"BUTTON", L"Detect Device",
            WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
            196, 110, 134, 32, hWnd, (HMENU)IDC_BTN_CHECK, GetModuleHandle(NULL), NULL);

        g_hBtnCleanup = CreateWindowW(L"BUTTON", L"Clean & Exit",
            WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
            338, 110, 122, 32, hWnd, (HMENU)IDC_BTN_CLEANUP, GetModuleHandle(NULL), NULL);

        // Status Line
        g_hStatusText = CreateWindowW(L"STATIC", L"Ready. Connect phone in Eng ROM or Recovery mode.",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            18, 150, 440, 16, hWnd, (HMENU)IDC_STATUS_TEXT, GetModuleHandle(NULL), NULL);
        SendMessageW(g_hStatusText, WM_SETFONT, (WPARAM)g_hFontSub, TRUE);

        // Slim 3px Accent Progress Track
        g_hProgressBar = CreateWindowExW(0, PROGRESS_CLASSW, NULL,
            WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
            16, 168, 444, 3, hWnd, (HMENU)IDC_PROGRESS, GetModuleHandle(NULL), NULL);
        SendMessageW(g_hProgressBar, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
        SendMessageW(g_hProgressBar, PBM_SETPOS, 0, 0);

        // Terminal Console
        g_hEditLog = CreateWindowExW(0, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
            16, 204, 444, 268, hWnd, (HMENU)IDC_EDIT_LOG, GetModuleHandle(NULL), NULL);
        SendMessageW(g_hEditLog, WM_SETFONT, (WPARAM)g_hFontConsole, TRUE);

        AppendLog(L"[SYSTEM] IMEI & Radio Backuper [Qcom/MTK] Ready.\r\n");
        AppendLog(L"[TARGET] Selected: Qualcomm Snapdragon\r\n\r\n");

        SetStatus(L"Mounting internal ADB engine...", 10);
        if (DecryptAndExtractPayload(TEMP_TARGET_DIR)) {
            SetStatus(L"Engine ready. Connect device in Eng ROM / Recovery.", 0);
        } else {
            SetStatus(L"Warning: Encrypted payload initialization incomplete.", 0);
        }

        return 0;
    }

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT pDIS = (LPDRAWITEMSTRUCT)lParam;
        if (pDIS->CtlType == ODT_BUTTON) {
            switch (pDIS->CtlID) {
            case IDC_TAB_QCOM: {
                bool sel = g_isQcomSelected.load();
                DrawModernPill(pDIS, L"Qualcomm Snapdragon", sel, RGB(56, 189, 248)); // Sky Cyan
                return TRUE;
            }
            case IDC_TAB_MTK: {
                bool sel = !g_isQcomSelected.load();
                DrawModernPill(pDIS, L"MediaTek (MTK)", sel, RGB(249, 115, 22)); // Amber Orange
                return TRUE;
            }
            case IDC_BTN_START: {
                DrawModernButton(pDIS, RGB(14, 116, 144), RGB(56, 189, 248), RGB(255, 255, 255), L"⚡ Backup Partitions");
                return TRUE;
            }
            case IDC_BTN_CHECK: {
                DrawModernButton(pDIS, RGB(22, 26, 35), RGB(43, 50, 66), RGB(226, 232, 240), L"Detect Device");
                return TRUE;
            }
            case IDC_BTN_CLEANUP: {
                DrawModernButton(pDIS, RGB(28, 20, 24), RGB(60, 32, 38), RGB(248, 113, 113), L"Clean & Exit");
                return TRUE;
            }
            }
        }
        break;
    }

    case WM_COMMAND: {
        int wmId = LOWORD(wParam);
        switch (wmId) {
        case IDC_TAB_QCOM:
            if (!g_isWorkerRunning) {
                g_isQcomSelected = true;
                InvalidateRect(g_hTabQcom, NULL, TRUE);
                InvalidateRect(g_hTabMtk, NULL, TRUE);
                AppendLog(L"[MODE] Target set to Qualcomm Snapdragon.\r\n");
            }
            break;

        case IDC_TAB_MTK:
            if (!g_isWorkerRunning) {
                g_isQcomSelected = false;
                InvalidateRect(g_hTabQcom, NULL, TRUE);
                InvalidateRect(g_hTabMtk, NULL, TRUE);
                AppendLog(L"[MODE] Target set to MediaTek (MTK).\r\n");
            }
            break;

        case IDC_BTN_START:
            if (!g_isWorkerRunning) {
                std::thread(BackupWorkerThread).detach();
            }
            break;

        case IDC_BTN_CHECK:
            if (!g_isWorkerRunning) {
                std::thread(CheckDeviceThread).detach();
            }
            break;

        case IDC_BTN_CLEANUP:
            if (!g_isWorkerRunning) {
                KillAdbAndCleanup();
                DestroyWindow(hWnd);
            }
            break;
        }
        return 0;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        HWND hwndCtl = (HWND)lParam;

        if (hwndCtl == g_hEditLog) {
            SetTextColor(hdc, RGB(52, 211, 153)); // Neon Emerald Green
            SetBkColor(hdc, RGB(10, 11, 15));
            SetBkMode(hdc, OPAQUE);
            return (INT_PTR)g_hBrushConsoleBg;
        }
        if (hwndCtl == g_hStatusText) {
            SetTextColor(hdc, RGB(56, 189, 248)); // Cyan Accent
            SetBkMode(hdc, TRANSPARENT);
            return (INT_PTR)g_hBrushWindowBg;
        }
        SetTextColor(hdc, RGB(203, 213, 225));
        SetBkMode(hdc, TRANSPARENT);
        return (INT_PTR)g_hBrushWindowBg;
    }

    case WM_CTLCOLOREDIT: {
        HDC hdcEdit = (HDC)wParam;
        SetTextColor(hdcEdit, RGB(52, 211, 153));
        SetBkColor(hdcEdit, RGB(10, 11, 15));
        SetBkMode(hdcEdit, OPAQUE);
        return (INT_PTR)g_hBrushConsoleBg;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        RECT clientRc;
        GetClientRect(hWnd, &clientRc);
        FillRect(hdc, &clientRc, g_hBrushWindowBg);

        // Header Title with custom branding
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(248, 250, 252));
        SelectObject(hdc, g_hFontTitle);
        std::wstring titleStr = L"IMEI & Radio Backup Tool By - @Shakib_BD [TG]";
        TextOutW(hdc, 18, 14, titleStr.c_str(), (int)titleStr.length());

        SetTextColor(hdc, RGB(100, 116, 139));
        SelectObject(hdc, g_hFontSub);
        TextOutW(hdc, 19, 34, L"ENGINEERING ROM & RECOVERY UTILITY", 34);

        // Chipset Selection Label
        SetTextColor(hdc, RGB(115, 128, 148));
        SelectObject(hdc, g_hFontSmall);
        TextOutW(hdc, 18, 54, L"CHIPSET ARCHITECTURE", 20);

        // Terminal Console Header Card (y: 180 to 204)
        RECT consoleHeaderRc = { 16, 180, 460, 204 };
        HBRUSH hHeaderBr = CreateSolidBrush(RGB(18, 21, 28));
        FillRect(hdc, &consoleHeaderRc, hHeaderBr);
        DeleteObject(hHeaderBr);

        // macOS / Dev Style Colored Window Dots
        HBRUSH hRedDot = CreateSolidBrush(RGB(239, 68, 68));
        HBRUSH hYelDot = CreateSolidBrush(RGB(245, 158, 11));
        HBRUSH hGrnDot = CreateSolidBrush(RGB(34, 197, 94));

        SelectObject(hdc, GetStockObject(NULL_PEN));
        SelectObject(hdc, hRedDot);
        Ellipse(hdc, 26, 189, 34, 197);

        SelectObject(hdc, hYelDot);
        Ellipse(hdc, 38, 189, 46, 197);

        SelectObject(hdc, hGrnDot);
        Ellipse(hdc, 50, 189, 58, 197);

        DeleteObject(hRedDot);
        DeleteObject(hYelDot);
        DeleteObject(hGrnDot);

        // Terminal Title
        SetTextColor(hdc, RGB(148, 163, 184));
        SelectObject(hdc, g_hFontSmall);
        TextOutW(hdc, 66, 186, L"DEVICE OUTPUT STREAM", 20);

        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_CLOSE: {
        KillAdbAndCleanup();
        DestroyWindow(hWnd);
        return 0;
    }

    case WM_DESTROY: {
        if (g_hFontTitle)   DeleteObject(g_hFontTitle);
        if (g_hFontSub)     DeleteObject(g_hFontSub);
        if (g_hFontBtn)     DeleteObject(g_hFontBtn);
        if (g_hFontSmall)   DeleteObject(g_hFontSmall);
        if (g_hFontConsole) DeleteObject(g_hFontConsole);

        if (g_hBrushWindowBg)  DeleteObject(g_hBrushWindowBg);
        if (g_hBrushCardBg)    DeleteObject(g_hBrushCardBg);
        if (g_hBrushConsoleBg) DeleteObject(g_hBrushConsoleBg);

        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

#define IDC_NOTES_BTN_OK 301
#define IDC_NOTES_EDIT   302

static HWND g_hNotesDialog = NULL;
static HFONT g_hFontNotesHeader = NULL;
static HFONT g_hFontNotesBody = NULL;
static HBRUSH g_hBrushNotesBg = NULL;
static HBRUSH g_hBrushNotesEdit = NULL;

// Unified window dimension constants to fit any laptop/desktop resolution (>= 720p)
const int APP_WINDOW_WIDTH  = 496;
const int APP_WINDOW_HEIGHT = 520;

const wchar_t* NOTES_TEXT =
    L"Q. How to use this tool / In which mode the device should be?\r\n"
    L"A. Your device must be in recovery mode.\r\n\r\n"
    L"Why is an IMEI backup important for your device?\r\n"
    L"1. Root access unlocks raw read/write control over deep system storage, making critical hardware blocks vulnerable to accidental overwrites.\r\n"
    L"2. IMEI and baseband data are stored in specialized non-volatile partitions (modemst1/2, fsg on Qualcomm; nvram, nvdata on MediaTek).\r\n"
    L"3. These partitions contain unique radio-frequency (RF) calibration values and hardware identifiers specific to your physical motherboard.\r\n"
    L"4. Flashing custom ROMs, kernels, or scripts can corrupt or zero out these blocks, causing \"Null/Unknown Baseband\" and permanently disabling all SIM network functions.\r\n"
    L"5. Having a backup ensures you can instantly restore complete cellular connectivity via fastboot without needing complex diagnostic tools or engineering ROMs.\r\n\r\n"
    L"IMEI Backup Note:\r\n"
    L"Backing up IMEI partitions is generally not supported in stock Mi-Recovery. To back them up successfully, boot into custom rom (aosp) or a compatible custom recovery (such as TWRP/OrangeFox) or in engineering rom & recovery.\r\n\r\n"
    L"Important Flashing Warning:\r\n"
    L"Flashing full engineering firmware can erase or overwrite existing baseband/IMEI data. Before flashing, modify the flash script or uncheck the following critical partitions to preserve them:\r\n"
    L"fsc, fsg, modemst1, modemst2, persist, nvram, nvdata, protect1, protect2, and proinfo.\r\n\r\n"
    L"WARNING: Never flash another phone's EFS/IMEI Backup to your device, as this can permanently break your radio and cellular connection.\r\n\r\n"
    L"How to Restore IMEI Partitions\r\n"
    L"Stock firmware blocks flashing modem and security partitions via Fastboot. To restore your IMEI backups, you must first flash an Engineering (ENG) ROM.\r\n\r\n"
    L"Restoration Steps:\r\n"
    L"1. Flash the engineering firmware while excluding the security partitions listed above.\r\n"
    L"2. Boot the device into Fastboot mode.\r\n"
    L"3. Flash each backed-up image manually using Fastboot.\r\n\r\n"
    L"Example:\r\n"
    L"fastboot flash fsg fsg.img\r\n\r\n"
    L"— @Shakib_BD [TG]\r\n";

LRESULT CALLBACK NotesDialogProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        EnableImmersiveDarkMode(hWnd);
        SendMessageW(hWnd, WM_CHANGEUISTATE, MAKEWPARAM(UIS_SET, UISF_HIDEFOCUS), 0);

        g_hFontNotesHeader = CreateFontW(17, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

        g_hBrushNotesBg   = CreateSolidBrush(RGB(13, 15, 20));
        g_hBrushNotesEdit = CreateSolidBrush(RGB(18, 21, 28));

        // RichEdit control enabling mixed styles (bold, italic, custom colors)
        HWND hNotesEdit = CreateWindowExW(
            WS_EX_CLIENTEDGE, RICHEDIT_CLASS, L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            16, 56, 448, 350, hWnd, (HMENU)IDC_NOTES_EDIT, GetModuleHandle(NULL), NULL);

        // Enable ENM_LINK notifications for interactive hyperlinks
        SendMessageW(hNotesEdit, EM_SETEVENTMASK, 0, (LPARAM)(SendMessageW(hNotesEdit, EM_GETEVENTMASK, 0, 0) | ENM_LINK));

        // Dark background and interior padding
        SendMessageW(hNotesEdit, EM_SETBKGNDCOLOR, 0, (LPARAM)RGB(18, 21, 28));
        SendMessageW(hNotesEdit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(10, 10));

        // Set text content
        SetWindowTextW(hNotesEdit, NOTES_TEXT);

        // Base text formatting: 10.5pt Segoe UI Regular in crisp light silver
        CHARFORMAT2W cfBase = { sizeof(CHARFORMAT2W) };
        cfBase.dwMask = CFM_COLOR | CFM_FACE | CFM_SIZE | CFM_WEIGHT;
        cfBase.crTextColor = RGB(226, 232, 240);
        cfBase.yHeight = 210; // 10.5pt (20 twips per pt)
        cfBase.wWeight = FW_NORMAL;
        wcscpy_s(cfBase.szFaceName, L"Segoe UI");
        SendMessageW(hNotesEdit, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cfBase);

        // Accurate styling helper using RichEdit's internal EM_FINDTEXTEXW to eliminate CRLF offset shift
        auto ApplyStyle = [&](const wchar_t* targetText, DWORD mask, DWORD effects, COLORREF color = 0) {
            FINDTEXTEXW ft;
            ft.chrg.cpMin = 0;
            ft.chrg.cpMax = -1;
            ft.lpstrText = targetText;

            LRESULT pos = SendMessageW(hNotesEdit, EM_FINDTEXTEXW, FR_DOWN, (LPARAM)&ft);
            if (pos != -1) {
                SendMessageW(hNotesEdit, EM_SETSEL, (WPARAM)ft.chrgText.cpMin, (LPARAM)ft.chrgText.cpMax);
                CHARFORMAT2W cf = { sizeof(CHARFORMAT2W) };
                cf.dwMask = mask;
                cf.dwEffects = effects;
                if (mask & CFM_COLOR) {
                    cf.crTextColor = color;
                }
                SendMessageW(hNotesEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
            }
        };

        // 0. Top Section Q&A Header
        ApplyStyle(L"Q. How to use this tool / In which mode the device should be?", CFM_BOLD | CFM_COLOR, CFE_BOLD, RGB(56, 189, 248));

        // 1. Top Section Header
        ApplyStyle(L"Why is an IMEI backup important for your device?", CFM_BOLD | CFM_COLOR, CFE_BOLD, RGB(56, 189, 248));

        // 2. Bold Header: "IMEI Backup Note:"
        ApplyStyle(L"IMEI Backup Note:", CFM_BOLD | CFM_COLOR, CFE_BOLD, RGB(255, 255, 255));

        // 3. Bold Header: "How to Restore IMEI Partitions"
        ApplyStyle(L"How to Restore IMEI Partitions", CFM_BOLD | CFM_COLOR, CFE_BOLD, RGB(255, 255, 255));

        // 4. Bold Warning Header: "Important Flashing Warning:"
        ApplyStyle(L"Important Flashing Warning:", CFM_BOLD | CFM_COLOR, CFE_BOLD, RGB(251, 146, 60));

        // 5. Bold Partitions list to skip
        ApplyStyle(L"fsc, fsg, modemst1, modemst2, persist, nvram, nvdata, protect1, protect2, and proinfo.", CFM_BOLD | CFM_COLOR, CFE_BOLD, RGB(255, 255, 255));

        // 6. Bold Red Flashing Warning
        ApplyStyle(L"WARNING: Never flash another phone's EFS/IMEI Backup to your device, as this can permanently break your radio and cellular connection.", CFM_BOLD | CFM_COLOR, CFE_BOLD, RGB(248, 113, 113));

        // 7. Bold Steps Header: "Restoration Steps:"
        ApplyStyle(L"Restoration Steps:", CFM_BOLD | CFM_COLOR, CFE_BOLD, RGB(255, 255, 255));

        // 8. Bold Example Header: "Example:"
        ApplyStyle(L"Example:", CFM_BOLD | CFM_COLOR, CFE_BOLD, RGB(255, 255, 255));

        // 9. Italic + Cyan Accent: fastboot command
        ApplyStyle(L"fastboot flash fsg fsg.img", CFM_ITALIC | CFM_BOLD | CFM_COLOR, CFE_ITALIC | CFE_BOLD, RGB(56, 189, 248));

        // 10. Clickable Telegram Link: @Shakib_BD
        ApplyStyle(L"@Shakib_BD", CFM_LINK | CFM_COLOR | CFM_BOLD, CFE_LINK | CFE_BOLD, RGB(56, 189, 248));

        // Reset scroll position and clear selection
        SendMessageW(hNotesEdit, EM_SETSEL, 0, 0);
        SendMessageW(hNotesEdit, EM_SCROLLCARET, 0, 0);

        HWND hBtnOk = CreateWindowW(
            L"BUTTON", L"OK, I Understand",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            148, 422, 184, 36, hWnd, (HMENU)IDC_NOTES_BTN_OK, GetModuleHandle(NULL), NULL);

        return 0;
    }

    case WM_NOTIFY: {
        NMHDR* pNmhdr = (NMHDR*)lParam;
        if (pNmhdr->idFrom == IDC_NOTES_EDIT && pNmhdr->code == EN_LINK) {
            ENLINK* pLink = (ENLINK*)lParam;
            if (pLink->msg == WM_LBUTTONUP) {
                ShellExecuteW(NULL, L"open", L"https://t.me/Shakib_BD", NULL, NULL, SW_SHOWNORMAL);
                return 0;
            }
        }
        break;
    }

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT pDIS = (LPDRAWITEMSTRUCT)lParam;
        if (pDIS->CtlType == ODT_BUTTON && pDIS->CtlID == IDC_NOTES_BTN_OK) {
            DrawModernButton(pDIS, RGB(14, 116, 144), RGB(56, 189, 248), RGB(255, 255, 255), L"OK, I Understand");
            return TRUE;
        }
        break;
    }

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wParam;
        HWND hwndCtl = (HWND)lParam;
        if (hwndCtl == GetDlgItem(hWnd, IDC_NOTES_EDIT)) {
            SetTextColor(hdc, RGB(226, 232, 240));
            SetBkColor(hdc, RGB(18, 21, 28));
            SetBkMode(hdc, OPAQUE);
            return (INT_PTR)g_hBrushNotesEdit;
        }
        SetTextColor(hdc, RGB(203, 213, 225));
        SetBkMode(hdc, TRANSPARENT);
        return (INT_PTR)g_hBrushNotesBg;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);
        FillRect(hdc, &rc, g_hBrushNotesBg);

        SetBkMode(hdc, TRANSPARENT);

        // Header Title in Orange, Centered
        SetTextColor(hdc, RGB(251, 146, 60)); // Vibrant Orange Accent
        SelectObject(hdc, g_hFontNotesHeader);
        RECT rcHeader = { 0, 14, rc.right, 34 };
        DrawTextW(hdc, L"Important Notes + Backup & Restoration Instructions", -1, &rcHeader, DT_CENTER | DT_SINGLELINE);

        // Subtitle Centered
        SetTextColor(hdc, RGB(148, 163, 184)); // Muted Silver
        SelectObject(hdc, g_hFontSub);
        RECT rcSub = { 0, 35, rc.right, 52 };
        DrawTextW(hdc, L"Please read carefully before proceeding with partition operations:", -1, &rcSub, DT_CENTER | DT_SINGLELINE);

        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_COMMAND: {
        if (LOWORD(wParam) == IDC_NOTES_BTN_OK) {
            DestroyWindow(hWnd);
        }
        return 0;
    }

    case WM_CLOSE: {
        DestroyWindow(hWnd);
        return 0;
    }

    case WM_DESTROY: {
        if (g_hFontNotesHeader) DeleteObject(g_hFontNotesHeader);
        if (g_hFontNotesBody)   DeleteObject(g_hFontNotesBody);
        if (g_hBrushNotesBg)    DeleteObject(g_hBrushNotesBg);
        if (g_hBrushNotesEdit)  DeleteObject(g_hBrushNotesEdit);
        g_hNotesDialog = NULL;
        return 0;
    }
    }
    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

void ShowStartupNotesDialog(HINSTANCE hInstance, HICON hIcon, int posX, int posY) {
    LoadLibraryW(L"riched20.dll");

    WNDCLASSEXW nwc = { 0 };
    nwc.cbSize        = sizeof(WNDCLASSEXW);
    nwc.style         = CS_HREDRAW | CS_VREDRAW;
    nwc.lpfnWndProc   = NotesDialogProc;
    nwc.hInstance     = hInstance;
    nwc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    nwc.hbrBackground = NULL;
    nwc.lpszClassName = L"ImeiBackupNotesDialogClass";
    nwc.hIcon         = hIcon;
    nwc.hIconSm       = hIcon;

    RegisterClassExW(&nwc);

    g_hNotesDialog = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        L"ImeiBackupNotesDialogClass",
        L"Important Notes - IMEI & Radio Backuper",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
        posX, posY, APP_WINDOW_WIDTH, APP_WINDOW_HEIGHT,
        NULL, NULL, hInstance, NULL
    );

    if (!g_hNotesDialog) return;

    ShowWindow(g_hNotesDialog, SW_SHOW);
    UpdateWindow(g_hNotesDialog);

    MSG msg = { 0 };
    while (IsWindow(g_hNotesDialog) && GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (wcscmp(argv[i], L"--pack") == 0) {
                LocalFree(argv);
                AllocConsole();
                FILE* fDummy;
                freopen_s(&fDummy, "CONOUT$", "w", stdout);
                int res = RunPackerMode();
                std::cout << "\nPress Enter to exit...";
                std::cin.get();
                return res;
            }
        }
        LocalFree(argv);
    }

    atexit(KillAdbAndCleanup);

    HICON hIcon = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED);
    if (!hIcon) {
        hIcon = (HICON)LoadImageW(NULL, L"icon.ico", IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE | LR_SHARED);
    }

    // Centered coordinates for both dialog and main application window
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int posX    = (screenW - APP_WINDOW_WIDTH) / 2;
    int posY    = (screenH - APP_WINDOW_HEIGHT) / 2;

    // Display the advisory notes dialog prior to entering the main tool
    ShowStartupNotesDialog(hInstance, hIcon, posX, posY);

    const wchar_t CLASS_NAME[] = L"ImeiBackupStudioWindowClass";
    WNDCLASSEXW wc = { 0 };
    wc.cbSize        = sizeof(WNDCLASSEXW);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WindowProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = CLASS_NAME;
    wc.hIcon         = hIcon;
    wc.hIconSm       = hIcon;

    RegisterClassExW(&wc);

    g_hWnd = CreateWindowExW(
        0,
        CLASS_NAME,
        L"IMEI & Radio Backuper [Qcom/MTK]",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
        posX, posY, APP_WINDOW_WIDTH, APP_WINDOW_HEIGHT,
        NULL,
        NULL,
        hInstance,
        NULL
    );

    if (!g_hWnd) return 0;

    ShowWindow(g_hWnd, nCmdShow);
    UpdateWindow(g_hWnd);

    MSG msg = { 0 };
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return (int)msg.wParam;
}