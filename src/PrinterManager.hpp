#include <string>

class PrinterManager
{
public:
    virtual std::string getDefaultPrinterName() = 0;
};