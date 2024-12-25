#include "../PrinterManager.hpp"

#include <windows.h>
#include <Winspool.h>
#include <Wingdi.h>
#pragma comment(lib, "Winspool.lib")

#include <string>
#include <map>
#include <utility>
#include <sstream>
#include <iostream>

struct PrinterHandle
{
    PrinterHandle(LPWSTR iPrinterName)
    {
        _ok = OpenPrinterW(iPrinterName, &_printer, NULL);
    }

    ~PrinterHandle()
    {
        if (_ok)
        {
            ClosePrinter(_printer);
        }
    }

    operator HANDLE() { return _printer; }
    operator bool() { return (!!_ok); }
    HANDLE &operator*() { return _printer; }
    HANDLE *operator->() { return &_printer; }
    const HANDLE &operator->() const { return _printer; }

    HANDLE _printer;
    BOOL _ok;
};

std::string LPWSTRToString(const wchar_t *wstr)
{
    if (wstr == NULL)
    {
        return std::string("");
    }
    if (wstr[0] == L'\0')
    {
        return std::string("");
    }

    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

std::string LPWSTRToString(const LPWSTR wstr)
{
    if (wstr == NULL)
    {
        return std::string("");
    }
    if (wstr[0] == L'\0')
    {
        return std::string("");
    }

    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

PrinterName PrinterManager::getDefaultPrinterName()
{

    DWORD cSize = 0;
    GetDefaultPrinterW(NULL, &cSize);

    if (cSize == 0)
    {
        return NULL;
    }

    MemValue<uint16_t> bPrinterName(cSize * sizeof(uint16_t));

    if (bPrinterName == NULL)
    {
        return NULL;
    }

    BOOL res = GetDefaultPrinterW((LPWSTR)(bPrinterName.get()), &cSize);

    if (!res)
    {
        return NULL;
    }

    std::string result = LPWSTRToString(reinterpret_cast<const wchar_t *>(bPrinterName.get()));
    std::wstring wide = std::wstring(result.begin(), result.end());

    return wide;
}

ErrorMessage *PrinterManager::getOneJob(PrinterName name, int jobId, JobInfo &jobInfo)
{

    PrinterHandle printerHandle((LPWSTR)name.c_str());

    if (!printerHandle)
    {
        static ErrorMessage errorMsg = "Could not open printer ";
        return &errorMsg;
    }

    DWORD sizeBytes = 0, dummyBytes = 0;
    GetJobW(*printerHandle, static_cast<DWORD>(jobId), 2, NULL, sizeBytes, &sizeBytes);
    MemValue<JOB_INFO_2W> job(sizeBytes);

    if (!job)
    {
        static ErrorMessage errorMsg = "Error on allocating memory for printers";
        return &errorMsg;
    }

    BOOL bOK = GetJobW(*printerHandle, static_cast<DWORD>(jobId), 2, (LPBYTE)job.get(), sizeBytes, &dummyBytes);
    if (!bOK)
    {
        static ErrorMessage errorMsg = "Error on GetJob. Wrong job id or it was deleted";
        return &errorMsg;
    }

    jobInfo.id = job->JobId;
    jobInfo.name = LPWSTRToString(job->pPrinterName);
    jobInfo.user = LPWSTRToString(job->pUserName);
    jobInfo.priority = job->Priority;
    jobInfo.size = job->Size;
    jobInfo.status = LPWSTRToString(job->pStatus);
    jobInfo.position = job->Position;
    jobInfo.totalPages = job->TotalPages;
    jobInfo.pagesPrinted = job->PagesPrinted;

    return NULL;
}

ErrorMessage *PrinterManager::getOnePrinter(PrinterName name, PrinterInfo &printerInfo)
{

    PrinterHandle printerHandle((LPWSTR)name.c_str());

    if (!printerHandle)
    {
        static ErrorMessage errorMsg = "Could not open printer";
        return &errorMsg;
    }

    DWORD sizeBytes = 0, dummyBytes = 0;
    GetPrinterW(printerHandle, 2, NULL, 0, &sizeBytes);

    MemValue<PRINTER_INFO_2W> printer(sizeBytes);
    if (!printer)
    {
        ClosePrinter(printerHandle);
        static ErrorMessage errorMsg = "Error on allocating memory for printer info";
        return &errorMsg;
    }

    BOOL bOK = GetPrinterW(printerHandle, 2, (LPBYTE)printer.get(), sizeBytes, &dummyBytes);
    if (!bOK)
    {
        ClosePrinter(printerHandle);
        static ErrorMessage errorMsg = "Error on GetPrinter. Wrong printer name or it was deleted";
        return &errorMsg;
    }

    printerInfo.name = LPWSTRToString(printer->pPrinterName);
    printerInfo.server = LPWSTRToString(printer->pServerName);
    printerInfo.shareName = LPWSTRToString(printer->pShareName);
    printerInfo.portName = LPWSTRToString(printer->pPortName);
    printerInfo.driverName = LPWSTRToString(printer->pDriverName);
    printerInfo.location = LPWSTRToString(printer->pLocation);
    printerInfo.comment = LPWSTRToString(printer->pComment);
    printerInfo.status = printer->Status;
    printerInfo.attributes = printer->Attributes;

    return NULL;
}
