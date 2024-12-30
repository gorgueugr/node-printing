#include "../PrinterManager.hpp"
#include <cups/cups.h>
#include <cups/ppd.h>
#include <stdexcept>
#include <sstream>
#include <string>

// Helper function to convert const char* to std::wstring
std::wstring charToWString(const char *str)
{
    std::string s(str);
    return std::wstring(s.begin(), s.end());
}

ErrorMessage *PrinterManager::getDefaultPrinterName(PrinterName &printerName)
{
    const char *defaultDest = cupsGetDefault();

    if (defaultDest == NULL)
    {
        static ErrorMessage errorMsg = "Error could not get default printer name";
        return &errorMsg;
    }

    printerName = charToWString(defaultDest);

    return NULL;
}

ErrorMessage *PrinterManager::getOnePrinter(PrinterName name, PrinterInfo &printerInfo)
{

    const char *printerName = std::string(name.begin(), name.end()).c_str();
    cups_dest_t *printer = NULL;
    printer = cupsGetNamedDest(NULL, printerName, NULL);

    if (printer == NULL)
    {
        static ErrorMessage errorMsg = "Error could not get printer info";
        return &errorMsg;
    }

    printerInfo.name = std::string(printer->name);

    if (printer->instance != NULL)
    {
        printerInfo.name = printerInfo.name + " " + std::string(printer->instance);
    }

    cups_option_t *option = NULL;

    for (int i = 0; i < printer->num_options; i++)
    {
        option = &(printer->options[i]);
        if (strcmp(option->name, "printer-info") == 0)
        {
            printerInfo.server = std::string(option->value);
        }
        else if (strcmp(option->name, "printer-location") == 0)
        {
            printerInfo.location = std::string(option->value);
        }
        else if (strcmp(option->name, "printer-make-and-model") == 0)
        {
            printerInfo.portName = std::string(option->value);
        }
        else if (strcmp(option->name, "printer-state") == 0)
        {
            printerInfo.status = atoi(option->value);
            if (atoi(option->value) == 3)
            {
                printerInfo.statusArray.push_back("idle");
            }
            else if (atoi(option->value) == 4)
            {
                printerInfo.statusArray.push_back("printing");
            }
            else if (atoi(option->value) == 5)
            {
                printerInfo.statusArray.push_back("stopped");
            }
        }
    }

    cupsFreeDests(1, printer);

    return NULL;
}