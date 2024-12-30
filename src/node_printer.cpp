#include "node_printer.hpp"

#include "PrinterManager.hpp"

#include <napi.h>
#include <windows.h>
#include <Winspool.h>
#include <Wingdi.h>
#pragma comment(lib, "Winspool.lib")

#include <string>
#include <map>
#include <utility>
#include <sstream>
#include <iostream>

namespace
{
    typedef std::map<std::string, DWORD> StatusMapType;

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

    const StatusMapType &getJobCommandMap()
    {
        static StatusMapType result;
        if (!result.empty())
        {
            return result;
        }
#define COMMAND_JOB_ADD(value, type) result.insert(std::make_pair(value, type))
        COMMAND_JOB_ADD("CANCEL", JOB_CONTROL_CANCEL);
        COMMAND_JOB_ADD("PAUSE", JOB_CONTROL_PAUSE);
        COMMAND_JOB_ADD("RESTART", JOB_CONTROL_RESTART);
        COMMAND_JOB_ADD("RESUME", JOB_CONTROL_RESUME);
        COMMAND_JOB_ADD("DELETE", JOB_CONTROL_DELETE);
        COMMAND_JOB_ADD("SENT-TO-PRINTER", JOB_CONTROL_SENT_TO_PRINTER);
        COMMAND_JOB_ADD("LAST-PAGE-EJECTED", JOB_CONTROL_LAST_PAGE_EJECTED);
#ifdef JOB_CONTROL_RETAIN
        COMMAND_JOB_ADD("RETAIN", JOB_CONTROL_RETAIN);
#endif
#ifdef JOB_CONTROL_RELEASE
        COMMAND_JOB_ADD("RELEASE", JOB_CONTROL_RELEASE);
#endif
#undef COMMAND_JOB_ADD
        return result;
    }

    void ParseJobObject(JOB_INFO_2W *job, Napi::Object &result)
    {
        Napi::Env env = result.Env();

        result.Set("id", Napi::Number::New(env, job->JobId));

        if (job->pPrinterName && *job->pPrinterName != L'\0')
        {
            result.Set("name", Napi::String::New(env, (char16_t *)job->pPrinterName));
        }

        if (job->pUserName && *job->pUserName != L'\0')
        {
            result.Set("user", Napi::String::New(env, (char16_t *)job->pUserName));
        }

        result.Set("priority", Napi::Number::New(env, job->Priority));
        result.Set("size", Napi::Number::New(env, job->Size));

        // Status handling
        Napi::Array statusArray = Napi::Array::New(env);
        int statusIndex = 0;

        for (const auto &status : getStatusMap())
        {
            if (job->Status & status.second)
            {
                statusArray.Set(statusIndex++, Napi::String::New(env, status.first));
            }
        }

        if (job->pStatus && *job->pStatus != L'\0')
        {
            statusArray.Set(statusIndex, Napi::String::New(env, (char16_t *)job->pStatus));
        }

        result.Set("status", statusArray);
        result.Set("position", Napi::Number::New(env, job->Position));
        result.Set("totalPages", Napi::Number::New(env, job->TotalPages));
        result.Set("pagesPrinted", Napi::Number::New(env, job->PagesPrinted));
    }

    std::string GetLastErrorMessage()
    {
        std::ostringstream ss;
        DWORD errorCode = GetLastError();
        ss << "code: " << errorCode;

        DWORD retSize;
        LPTSTR pTemp = NULL;

        retSize = FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                                    FORMAT_MESSAGE_FROM_SYSTEM |
                                    FORMAT_MESSAGE_ARGUMENT_ARRAY,
                                NULL,
                                errorCode,
                                LANG_NEUTRAL,
                                (LPTSTR)&pTemp,
                                0,
                                NULL);

        if (retSize && pTemp != NULL)
        {
            ss << ", message: " << std::string(pTemp);
            LocalFree((HLOCAL)pTemp);
        }

        return ss.str();
    }
}

Napi::String StdStringToNapiString(Napi::Env env, std::string str)
{
    return Napi::String::New(env, str.c_str());
}

Napi::String StdStringToNapiString(Napi::Env env, std::wstring str)
{
    std::string strTo(str.begin(), str.end());
    return Napi::String::New(env, strTo.c_str());
}

std::wstring GetWStringFromNapiValue(const Napi::Value &value)
{
    if (!value.IsString())
    {
        throw Napi::TypeError::New(value.Env(), "String expected");
    }
    std::u16string temp = value.As<Napi::String>().Utf16Value();
    return std::wstring(temp.begin(), temp.end());
}

void AddResultStringArray(Napi::Env env, Napi::Object &result, const std::vector<std::string> &array, const std::string &name)
{
    Napi::Array arrayResult = Napi::Array::New(env, array.size());
    for (int i = 0; i < array.size(); ++i)
    {
        arrayResult[i] = StdStringToNapiString(env, array[i]);
    }
    result.Set(name, arrayResult);
}

void ParsePrinterObject(PrinterInfo &printerInfo, Napi::Object &resultPrinter)
{

    Napi::Env env = resultPrinter.Env();

    resultPrinter.Set("name", StdStringToNapiString(env, printerInfo.name));
    resultPrinter.Set("server", StdStringToNapiString(env, printerInfo.server));
    resultPrinter.Set("shareName", StdStringToNapiString(env, printerInfo.shareName));
    resultPrinter.Set("portName", StdStringToNapiString(env, printerInfo.portName));
    resultPrinter.Set("driverName", StdStringToNapiString(env, printerInfo.driverName));
    resultPrinter.Set("location", StdStringToNapiString(env, printerInfo.location));
    resultPrinter.Set("comment", StdStringToNapiString(env, printerInfo.comment));
    resultPrinter.Set("status", Napi::Number::New(env, printerInfo.status));

    AddResultStringArray(env, resultPrinter, printerInfo.statusArray, std::string("statusArray"));
    AddResultStringArray(env, resultPrinter, printerInfo.attributeArray, std::string("attributeArray"));

    resultPrinter.Set("averagePPM", Napi::Number::New(env, printerInfo.averagePPM));
    resultPrinter.Set("cJobs", Napi::Number::New(env, printerInfo.cJobs));
    resultPrinter.Set("defaultPriority", Napi::Number::New(env, printerInfo.defaultPriority));
    resultPrinter.Set("startTime", Napi::Number::New(env, printerInfo.startTime));
    resultPrinter.Set("untilTime", Napi::Number::New(env, printerInfo.untilTime));
}

// N-API function implementations
Napi::Value GetOnePrinter(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();

    // Check arguments
    if (info.Length() < 1)
    {
        Napi::TypeError::New(env, "Wrong number of arguments")
            .ThrowAsJavaScriptException();
        return env.Null();
    }

    if (!info[0].IsString())
    {
        Napi::TypeError::New(env, "String expected")
            .ThrowAsJavaScriptException();
        return env.Null();
    }

    // Convert JS string to wide string
    std::wstring printerName = GetWStringFromNapiValue(info[0]);

    PrinterManager *printerManager = new PrinterManager();
    PrinterInfo printerInfo;

    ErrorMessage *errorMessage = printerManager->getOnePrinter(printerName, printerInfo);
    if (errorMessage != NULL)
    {
        Napi::Error::New(env, (std::string)*errorMessage).ThrowAsJavaScriptException();
        return env.Null();
    }

    Napi::Object resultPrinter = Napi::Object::New(env);
    ParsePrinterObject(printerInfo, resultPrinter);

    return resultPrinter;
}

Napi::String GetDefaultPrinterName(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();

    PrinterManager *printerManager = new PrinterManager();
    PrinterName defaultPrinterName;

    ErrorMessage *errorMessage = printerManager->getDefaultPrinterName(defaultPrinterName);
    if (errorMessage != NULL)
    {
        Napi::Error::New(env, (std::string)*errorMessage).ThrowAsJavaScriptException();
        return Napi::String::New(env, "");
    }

    return StdStringToNapiString(env, defaultPrinterName);
}

Napi::Array GetPrinters(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();

    PrinterManager *printerManager = new PrinterManager();
    std::vector<PrinterInfo> printersInfo;
    ErrorMessage *errorMessage = printerManager->getPrinters(printersInfo);
    if (errorMessage != NULL)
    {
        Napi::Error::New(env, (std::string)*errorMessage).ThrowAsJavaScriptException();
        return Napi::Array::New(env, 0);
    }

    Napi::Array result = Napi::Array::New(env, printersInfo.size());
    for (int i = 0; i < printersInfo.size(); ++i)
    {
        Napi::Object printerObj = Napi::Object::New(env);
        ParsePrinterObject(printersInfo[i], printerObj);

        result[i] = printerObj;
    }

    return result;
}

Napi::Value PrintDirect(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();

    if (info.Length() < 4)
    {
        throw Napi::Error::New(env, "Wrong number of arguments");
    }

    std::string data;
    if (info[0].IsString())
    {
        data = info[0].As<Napi::String>().Utf8Value();
    }
    else if (info[0].IsBuffer())
    {
        Napi::Buffer<char> buffer = info[0].As<Napi::Buffer<char>>();
        data = std::string(buffer.Data(), buffer.Length());
    }
    else
    {
        throw Napi::Error::New(env, "First argument must be a string or Buffer");
    }

    std::wstring printerNameWide = GetWStringFromNapiValue(info[1]);
    std::wstring docNameWide = GetWStringFromNapiValue(info[2]);
    std::wstring typeWide = GetWStringFromNapiValue(info[3]);

    std::string docName = std::string(docNameWide.begin(), docNameWide.end());
    std::string type = std::string(typeWide.begin(), typeWide.end());

    int jobId = 0;

    PrinterManager *printerManager = new PrinterManager();
    ErrorMessage *errorMessage = printerManager->printDirect(printerNameWide, docName, type, data, jobId);
    if (errorMessage != NULL)
    {
        Napi::Error::New(env, (std::string)*errorMessage).ThrowAsJavaScriptException();
        return env.Null();
    }
    // std::wstring printerName = GetWStringFromNapiValue(info[1]);
    // std::wstring docName = GetWStringFromNapiValue(info[2]);
    // std::wstring type = GetWStringFromNapiValue(info[3]);

    // PrinterHandle printerHandle((LPWSTR)printerName.c_str());

    // if (!printerHandle)
    // {
    //     std::string error = "PrinterHandle error: " + GetLastErrorMessage();
    //     throw Napi::Error::New(env, error);
    // }

    // DOC_INFO_1W DocInfo;
    // DocInfo.pDocName = (LPWSTR)docName.c_str();
    // DocInfo.pOutputFile = NULL;
    // DocInfo.pDatatype = (LPWSTR)type.c_str();

    // DWORD jobId = StartDocPrinterW(*printerHandle, 1, (LPBYTE)&DocInfo);
    // if (jobId == 0)
    // {
    //     std::string error = "StartDocPrinter error: " + GetLastErrorMessage();
    //     throw Napi::Error::New(env, error);
    // }

    // if (!StartPagePrinter(*printerHandle))
    // {
    //     std::string error = "StartPagePrinter error: " + GetLastErrorMessage();
    //     throw Napi::Error::New(env, error);
    // }

    // DWORD bytesWritten = 0;
    // BOOL success = WritePrinter(*printerHandle, (LPVOID)data.c_str(),
    //                             (DWORD)data.size(), &bytesWritten);

    // EndPagePrinter(*printerHandle);
    // EndDocPrinter(*printerHandle);

    // if (!success || bytesWritten != data.size())
    // {
    //     throw Napi::Error::New(env, "Failed to write all data to printer");
    // }

    return Napi::Number::New(env, jobId);
}

Napi::Value GetSupportedJobCommands(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();

    StatusMapType jobCommandMap = getJobCommandMap();
    size_t mapSize = jobCommandMap.size();
    Napi::Array resultArray = Napi::Array::New(env, mapSize);

    size_t index = 0;
    for (const auto &command : jobCommandMap)
    {
        resultArray.Set(index++, Napi::String::New(env, command.first));
    }

    return resultArray;
}

Napi::Value GetOneJob(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();

    // Check arguments
    if (info.Length() < 2)
    {
        Napi::TypeError::New(env, "Expected two arguments").ThrowAsJavaScriptException();
        return env.Null();
    }
    if (!info[0].IsString() || !info[1].IsNumber())
    {
        Napi::TypeError::New(env, "Expected a string and a number").ThrowAsJavaScriptException();
        return env.Null();
    }

    std::wstring printerNameWide = GetWStringFromNapiValue(info[0]);

    int jobId = info[1].As<Napi::Number>().Int32Value();
    if (jobId < 0)
    {
        Napi::Error::New(env, "Wrong job number").ThrowAsJavaScriptException();
        return env.Null();
    }

    PrinterManager *printerManager = new PrinterManager();

    JobInfo jobInfo;

    ErrorMessage *errorMessage = printerManager->getOneJob((PrinterName)printerNameWide, jobId, jobInfo);
    if (errorMessage != NULL)
    {
        Napi::Error::New(env, (std::string)*errorMessage).ThrowAsJavaScriptException();
        return env.Null();
    }

    Napi::Object resultPrinterJob = Napi::Object::New(env);
    resultPrinterJob.Set("id", Napi::Number::New(env, jobInfo.id));
    resultPrinterJob.Set("name", StdStringToNapiString(env, jobInfo.name));
    resultPrinterJob.Set("user", StdStringToNapiString(env, jobInfo.user));
    resultPrinterJob.Set("priority", Napi::Number::New(env, jobInfo.priority));
    resultPrinterJob.Set("size", Napi::Number::New(env, jobInfo.size));
    resultPrinterJob.Set("status", StdStringToNapiString(env, jobInfo.status));
    resultPrinterJob.Set("position", Napi::Number::New(env, jobInfo.position));
    resultPrinterJob.Set("totalPages", Napi::Number::New(env, jobInfo.totalPages));
    resultPrinterJob.Set("pagesPrinted", Napi::Number::New(env, jobInfo.pagesPrinted));
    return resultPrinterJob;

    // // Open a handle to the printer
    // PrinterHandle printerHandle((LPWSTR)printerName.c_str());
    // if (!printerHandle)
    // {
    //     // std::string errorStr = "error on PrinterHandle: " + GetLastErrorMessage();
    //     // Napi::Error::New(env, errorStr).ThrowAsJavaScriptException();
    //     return env.Null();
    // }

    // // Determine size of job info buffer
    // DWORD sizeBytes = 0, dummyBytes = 0;
    // GetJobW(*printerHandle, static_cast<DWORD>(jobId), 2, NULL, sizeBytes, &sizeBytes);
    // MemValue<JOB_INFO_2W> job(sizeBytes);
    // if (!job)
    // {
    //     Napi::Error::New(env, "Error on allocating memory for printers").ThrowAsJavaScriptException();
    //     return env.Null();
    // }

    // // Get the job info
    // BOOL bOK = GetJobW(*printerHandle, static_cast<DWORD>(jobId), 2, (LPBYTE)job.get(), sizeBytes, &dummyBytes);
    // if (!bOK)
    // {
    //     // std::string errorStr = "Error on GetJob. Wrong job id or it was deleted: " + GetLastErrorMessage();
    //     // Napi::Error::New(env, errorStr).ThrowAsJavaScriptException();
    //     return env.Null();
    // }

    // // Create a result object and parse job info into it
    // // Napi::Object resultPrinterJob = Napi::Object::New(env);
    // ParseJobObject(job.get(), resultPrinterJob);

    // return resultPrinterJob;
}

Napi::Value SetOneJob(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();

    // Check arguments
    if (info.Length() < 3)
    {
        Napi::TypeError::New(env, "Expected three arguments").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    if (!info[0].IsString() || !info[1].IsNumber() || !info[2].IsString())
    {
        Napi::TypeError::New(env, "Expected a string, a number, and a string").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    std::wstring printerName = GetWStringFromNapiValue(info[0]);
    int jobId = info[1].As<Napi::Number>().Int32Value();
    if (jobId < 0)
    {
        Napi::Error::New(env, "Wrong job number").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    std::string jobCommandStr = info[2].As<Napi::String>().Utf8Value();
    StatusMapType::const_iterator itJobCommand = getJobCommandMap().find(jobCommandStr);
    if (itJobCommand == getJobCommandMap().end())
    {
        Napi::Error::New(env, "Wrong job command. Use getSupportedJobCommands to see the possible commands").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    DWORD jobCommand = itJobCommand->second;

    // Open a handle to the printer
    PrinterHandle printerHandle((LPWSTR)printerName.c_str());
    if (!printerHandle)
    {
        std::string errorStr = "Error on PrinterHandle: " + GetLastErrorMessage();
        Napi::Error::New(env, errorStr).ThrowAsJavaScriptException();
        return env.Undefined();
    }

    // Set the job command
    BOOL ok = SetJobW(*printerHandle, static_cast<DWORD>(jobId), 0, NULL, jobCommand);

    return Napi::Boolean::New(env, ok == TRUE);
}

Napi::Value GetSupportedPrintFormats(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();

    PrinterManager *printerManager = new PrinterManager();
    std::vector<std::string> dataTypes;
    ErrorMessage *errorMessage = printerManager->getSupportedPrintFormats(dataTypes);

    if (errorMessage != NULL)
    {
        Napi::Error::New(env, (std::string)*errorMessage).ThrowAsJavaScriptException();
        return env.Null();
    }

    Napi::Array result = Napi::Array::New(env, dataTypes.size());
    for (int i = 0; i < dataTypes.size(); ++i)
    {
        result[i] = Napi::String::New(env, dataTypes[i].c_str());
    }

    return result;
}

Napi::Value GetPrinterDevMode(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();

    // Check arguments
    if (info.Length() < 1)
    {
        Napi::TypeError::New(env, "Expected one arguments").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    if (!info[0].IsString())
    {
        Napi::TypeError::New(env, "Expected a string").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    std::wstring printerNameWide = GetWStringFromNapiValue(info[0]);

    PrinterManager *printerManager = new PrinterManager();
    PrinterDevMode printerDevMode;
    ErrorMessage *errorMessage = printerManager->getPrinterDevMode(printerNameWide, printerDevMode);

    if (errorMessage != NULL)
    {
        Napi::Error::New(env, (std::string)*errorMessage).ThrowAsJavaScriptException();
        return env.Null();
    }

    Napi::Object result = Napi::Object::New(env);
    result.Set("deviceName", StdStringToNapiString(env, printerDevMode.deviceName));
    result.Set("paperSize", StdStringToNapiString(env, printerDevMode.paperSize));
    result.Set("orientation", StdStringToNapiString(env, orientation_str[printerDevMode.orientation]));
    result.Set("duplex", StdStringToNapiString(env, duplex_str[printerDevMode.duplex]));
    result.Set("copies", Napi::Number::New(env, printerDevMode.copies));
    result.Set("color", StdStringToNapiString(env, color_str[printerDevMode.color]));
    result.Set("defaultSource", StdStringToNapiString(env, printerDevMode.defaultSource));
    result.Set("printQuality", StdStringToNapiString(env, printQuality_str[printerDevMode.printQuality]));
    result.Set("scale", Napi::Number::New(env, printerDevMode.scale));
    result.Set("collate", Napi::Boolean::New(env, printerDevMode.collate));

    return result;
}

Napi::Object Init(Napi::Env env, Napi::Object exports)
{
    // Set methods

    exports.Set("getPrinters", Napi::Function::New(env, GetPrinters));
    exports.Set("getDefaultPrinterName", Napi::Function::New(env, GetDefaultPrinterName));
    exports.Set("getPrinter", Napi::Function::New(env, GetOnePrinter));
    //  exports.Set("getPrinterDriverOptions", Napi::Function::New(env, GetPrinterDriverOptions));
    exports.Set("getJob", Napi::Function::New(env, GetOneJob));
    // exports.Set("setJob", Napi::Function::New(env, SetOneJob));
    exports.Set("printDirect", Napi::Function::New(env, PrintDirect));
    // exports.Set("printFile", Napi::Function::New(env, PrintFile));
    exports.Set("getPrinterDevMode", Napi::Function::New(env, GetPrinterDevMode));
    exports.Set("getSupportedPrintFormats", Napi::Function::New(env, GetSupportedPrintFormats));
    exports.Set("getSupportedJobCommands", Napi::Function::New(env, GetSupportedJobCommands));

    return exports;
}

NODE_API_MODULE(nodeprinting, Init)
