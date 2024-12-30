#include "../PrinterManager.hpp"
#include <cups/cups.h>
#include <cups/ppd.h>
#include <stdexcept>
#include <sstream>

// Helper function to convert const char* to std::wstring
std::wstring charToWString(const char *str)
{
    std::string s(str);
    return std::wstring(s.begin(), s.end());
}

ErrorMessage *PrinterManager::getDefaultPrinterName(PrinterName &printerName)
{
    const char *defaultDest = cupsGetDefault2(HTTP_);

    if (defaultDest == NULL)
    {
        static ErrorMessage errorMsg = "Error could not get default printer name";
        return &errorMsg;
    }

    printerName = charToWString(defaultDest);

    return NULL;
}
