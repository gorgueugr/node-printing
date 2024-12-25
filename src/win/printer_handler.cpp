
#include "printer_handler.hpp"

#include <windows.h>
#include <Winspool.h>
#include <Wingdi.h>
#pragma comment(lib, "Winspool.lib")

std::string GetDefaultPrinterName()
{

    DWORD cSize = 0;
    GetDefaultPrinterW(NULL, &cSize);
    if (cSize == 0)
    {
        return std::string();
    }

    uint16_t *bPrinterName = (uint16_t *)malloc(cSize * sizeof(uint16_t));
    BOOL res = GetDefaultPrinterW((LPWSTR)(bPrinterName), &cSize);

    if (!res)
    {
        return std::string();
    }

    std::string result = std::string(reinterpret_cast<const char *>(bPrinterName));

    free(bPrinterName);

    return result;
}
