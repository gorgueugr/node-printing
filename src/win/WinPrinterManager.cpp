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

typedef std::map<std::string, DWORD> StatusMapType;

// Status maps implementation
const StatusMapType &getStatusMap()
{
    static StatusMapType result;
    if (!result.empty())
    {
        return result;
    }

#define STATUS_PRINTER_ADD(value, type) result.insert(std::make_pair(value, type))
    STATUS_PRINTER_ADD("BUSY", PRINTER_STATUS_BUSY);
    STATUS_PRINTER_ADD("DOOR-OPEN", PRINTER_STATUS_DOOR_OPEN);
    STATUS_PRINTER_ADD("ERROR", PRINTER_STATUS_ERROR);
    STATUS_PRINTER_ADD("INITIALIZING", PRINTER_STATUS_INITIALIZING);
    STATUS_PRINTER_ADD("IO-ACTIVE", PRINTER_STATUS_IO_ACTIVE);
    STATUS_PRINTER_ADD("MANUAL-FEED", PRINTER_STATUS_MANUAL_FEED);
    STATUS_PRINTER_ADD("NO-TObPrinterNameNER", PRINTER_STATUS_NO_TONER);
    STATUS_PRINTER_ADD("NOT-AVAILABLE", PRINTER_STATUS_NOT_AVAILABLE);
    STATUS_PRINTER_ADD("OFFLINE", PRINTER_STATUS_OFFLINE);
    STATUS_PRINTER_ADD("OUT-OF-MEMORY", PRINTER_STATUS_OUT_OF_MEMORY);
    STATUS_PRINTER_ADD("OUTPUT-BIN-FULL", PRINTER_STATUS_OUTPUT_BIN_FULL);
    STATUS_PRINTER_ADD("PAGE-PUNT", PRINTER_STATUS_PAGE_PUNT);
    STATUS_PRINTER_ADD("PAPER-JAM", PRINTER_STATUS_PAPER_JAM);
    STATUS_PRINTER_ADD("PAPER-OUT", PRINTER_STATUS_PAPER_OUT);
    STATUS_PRINTER_ADD("PAPER-PROBLEM", PRINTER_STATUS_PAPER_PROBLEM);
    STATUS_PRINTER_ADD("PAUSED", PRINTER_STATUS_PAUSED);
    STATUS_PRINTER_ADD("PENDING-DELETION", PRINTER_STATUS_PENDING_DELETION);
    STATUS_PRINTER_ADD("POWER-SAVE", PRINTER_STATUS_POWER_SAVE);
    STATUS_PRINTER_ADD("PRINTING", PRINTER_STATUS_PRINTING);
    STATUS_PRINTER_ADD("PROCESSING", PRINTER_STATUS_PROCESSING);
    STATUS_PRINTER_ADD("SERVER-UNKNOWN", PRINTER_STATUS_SERVER_UNKNOWN);
    STATUS_PRINTER_ADD("TONER-LOW", PRINTER_STATUS_TONER_LOW);
    STATUS_PRINTER_ADD("USER-INTERVENTION", PRINTER_STATUS_USER_INTERVENTION);
    STATUS_PRINTER_ADD("WAITING", PRINTER_STATUS_WAITING);
    STATUS_PRINTER_ADD("WARMING-UP", PRINTER_STATUS_WARMING_UP);
#undef STATUS_PRINTER_ADD
    return result;
}

const StatusMapType &getAttributeMap()
{
    static StatusMapType result;
    if (!result.empty())
    {
        return result;
    }
    // add only first time
#define ATTRIBUTE_PRINTER_ADD(value, type) result.insert(std::make_pair(value, type))
    ATTRIBUTE_PRINTER_ADD("DIRECT", PRINTER_ATTRIBUTE_DIRECT);
    ATTRIBUTE_PRINTER_ADD("DO-COMPLETE-FIRST", PRINTER_ATTRIBUTE_DO_COMPLETE_FIRST);
    ATTRIBUTE_PRINTER_ADD("ENABLE-DEVQ", PRINTER_ATTRIBUTE_ENABLE_DEVQ);
    ATTRIBUTE_PRINTER_ADD("HIDDEN", PRINTER_ATTRIBUTE_HIDDEN);
    ATTRIBUTE_PRINTER_ADD("KEEPPRINTEDJOBS", PRINTER_ATTRIBUTE_KEEPPRINTEDJOBS);
    ATTRIBUTE_PRINTER_ADD("LOCAL", PRINTER_ATTRIBUTE_LOCAL);
    ATTRIBUTE_PRINTER_ADD("NETWORK", PRINTER_ATTRIBUTE_NETWORK);
    ATTRIBUTE_PRINTER_ADD("PUBLISHED", PRINTER_ATTRIBUTE_PUBLISHED);
    ATTRIBUTE_PRINTER_ADD("QUEUED", PRINTER_ATTRIBUTE_QUEUED);
    ATTRIBUTE_PRINTER_ADD("RAW-ONLY", PRINTER_ATTRIBUTE_RAW_ONLY);
    ATTRIBUTE_PRINTER_ADD("SHARED", PRINTER_ATTRIBUTE_SHARED);
    ATTRIBUTE_PRINTER_ADD("OFFLINE", PRINTER_ATTRIBUTE_WORK_OFFLINE);
    // XP
#ifdef PRINTER_ATTRIBUTE_FAX
    ATTRIBUTE_PRINTER_ADD("FAX", PRINTER_ATTRIBUTE_FAX);
#endif
    // vista
#ifdef PRINTER_ATTRIBUTE_FRIENDLY_NAME
    ATTRIBUTE_PRINTER_ADD("FRIENDLY-NAME", PRINTER_ATTRIBUTE_FRIENDLY_NAME);
    ATTRIBUTE_PRINTER_ADD("MACHINE", PRINTER_ATTRIBUTE_MACHINE);
    ATTRIBUTE_PRINTER_ADD("PUSHED-USER", PRINTER_ATTRIBUTE_PUSHED_USER);
    ATTRIBUTE_PRINTER_ADD("PUSHED-MACHINE", PRINTER_ATTRIBUTE_PUSHED_MACHINE);
#endif
    // server 2003
#ifdef PRINTER_ATTRIBUTE_TS
    ATTRIBUTE_PRINTER_ADD("TS", PRINTER_ATTRIBUTE_TS);
#endif
#undef ATTRIBUTE_PRINTER_ADD
    return result;
}

PrinterName PrinterManager::getDefaultPrinterName()
{

    DWORD cSize = 0;
    GetDefaultPrinterW(NULL, &cSize);

    if (cSize == 0)
    {
        throw std::runtime_error("Error could not get default printer name");
    }

    MemValue<uint16_t> bPrinterName(cSize * sizeof(uint16_t));

    if (bPrinterName == NULL)
    {
        throw std::runtime_error("Error on allocating memory for printer name");
    }

    BOOL res = GetDefaultPrinterW((LPWSTR)(bPrinterName.get()), &cSize);

    if (!res)
    {
        throw std::runtime_error("Error on GetDefaultPrinterW");
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

std::vector<std::string> getStatusArray(DWORD status)
{
    std::vector<std::string> result;

    for (const auto &statusMap : getStatusMap())
    {
        if (status & statusMap.second)
        {
            result.push_back(statusMap.first);
        }
    }

    return result;
}

std::vector<std::string> getAttributeArray(DWORD attributes)
{
    std::vector<std::string> result;

    for (const auto &attributeMap : getAttributeMap())
    {
        if (attributes & attributeMap.second)
        {
            result.push_back(attributeMap.first);
        }
    }

    return result;
}

void ParsePrinterObject(PRINTER_INFO_2W *printer, PrinterInfo &printerInfo)
{
    printerInfo.name = LPWSTRToString(printer->pPrinterName);
    printerInfo.server = LPWSTRToString(printer->pServerName);
    printerInfo.shareName = LPWSTRToString(printer->pShareName);
    printerInfo.portName = LPWSTRToString(printer->pPortName);
    printerInfo.driverName = LPWSTRToString(printer->pDriverName);
    printerInfo.location = LPWSTRToString(printer->pLocation);
    printerInfo.comment = LPWSTRToString(printer->pComment);
    printerInfo.status = printer->Status;
    printerInfo.statusArray = getStatusArray(printer->Status);
    printerInfo.attributes = printer->Attributes;
    printerInfo.attributeArray = getAttributeArray(printer->Attributes);
    printerInfo.averagePPM = printer->AveragePPM;
    printerInfo.cJobs = printer->cJobs;
    printerInfo.defaultPriority = printer->DefaultPriority;
    printerInfo.startTime = printer->StartTime;
    printerInfo.untilTime = printer->UntilTime;
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

    ParsePrinterObject(printer.get(), printerInfo);

    return NULL;
}

ErrorMessage *PrinterManager::getPrinters(std::vector<PrinterInfo> &printersInfo)
{

    DWORD printers_size = 0;
    DWORD printers_size_bytes = 0, dummyBytes = 0;
    DWORD flags = PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS;

    // Get required buffer size
    EnumPrintersW(flags, NULL, 2, NULL, 0, &printers_size_bytes, &printers_size);

    MemValue<PRINTER_INFO_2W> printers(printers_size_bytes);
    if (!printers)
    {
        static ErrorMessage errorMsg = "Failed to allocate memory for printers";
        return &errorMsg;
    }

    BOOL bError = EnumPrintersW(flags, NULL, 2, (LPBYTE)(printers.get()),
                                printers_size_bytes, &dummyBytes, &printers_size);

    if (!bError)
    {
        static ErrorMessage errorMsg = "EnumPrinters Error ";
        return &errorMsg;
    }

    PRINTER_INFO_2W *printer = printers.get();

    for (DWORD i = 0; i < printers_size; ++i, ++printer)
    {
        PrinterInfo printerInfo;
        ParsePrinterObject(printer, printerInfo);
        printersInfo.push_back(printerInfo);
    }

    return NULL;
}
