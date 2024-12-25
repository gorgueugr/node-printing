#include "WinPrinterManager.hpp"
#include "../node_printer.hpp"

#include <windows.h>
#include <Winspool.h>
#include <Wingdi.h>
#pragma comment(lib, "Winspool.lib")

#include <string>
#include <map>
#include <utility>
#include <sstream>
#include <iostream>

std::string LPWSTRToString(const wchar_t *wstr)
{
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

std::string LPWSTRToString(const LPWSTR wstr)
{
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

std::string WinPrinterManager::getDefaultPrinterName()
{

    DWORD cSize = 0;
    GetDefaultPrinterW(NULL, &cSize);
    if (cSize == 0)
    {
        return std::string();
    }

    // LPWSTR *bPrinterName = new LPWSTR[cSize];
    MemValue<uint16_t> bPrinterName(cSize * sizeof(uint16_t));

    if (bPrinterName == NULL)
    {
        return std::string();
    }

    BOOL res = GetDefaultPrinterW((LPWSTR)(bPrinterName.get()), &cSize);

    if (!res)
    {
        return std::string();
    }

    return LPWSTRToString(reinterpret_cast<const wchar_t *>(bPrinterName.get()));
}