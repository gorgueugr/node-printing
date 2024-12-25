#ifndef PRINTER_HANDLER_H
#define PRINTER_HANDLER_H

#include <windows.h>
#include <string>

struct PrinterHandler
{
public:
    PrinterHandler(LPWSTR iPrinterName)
    {
        _ok = OpenPrinterW(iPrinterName, &_printer, NULL);
    };
    ~PrinterHandler()
    {
        if (_ok)
        {
            ClosePrinter(_printer);
        }
    };

    operator HANDLE() { return _printer; }
    operator bool() { return (!!_ok); }
    HANDLE &operator*() { return _printer; }
    HANDLE *operator->() { return &_printer; }
    const HANDLE &operator->() const { return _printer; }

    HANDLE _printer;
    BOOL _ok;
};

#endif // PRINTER_HANDLER_H