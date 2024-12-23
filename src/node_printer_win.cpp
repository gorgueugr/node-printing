#include "node_printer.hpp"

#include <napi.h>
#include <windows.h>
#include <Winspool.h>
#include <Wingdi.h>
#pragma comment(lib, "Winspool.lib")

#include <string>
#include <map>
#include <utility>
#include <sstream>

namespace
{
    typedef std::map<std::string, DWORD> StatusMapType;

    // Memory value class management to avoid memory leak
    template <typename Type>
    class MemValue
    {
    public:
        MemValue(const DWORD iSizeKbytes)
        {
            _value = (Type *)malloc(iSizeKbytes);
        }

        ~MemValue()
        {
            free();
        }

        Type *get() { return _value; }
        operator bool() { return (_value != NULL); }

    protected:
        Type *_value;
        virtual void free()
        {
            if (_value != NULL)
            {
                ::free(_value);
                _value = NULL;
            }
        }
    };

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
        STATUS_PRINTER_ADD("NO-TONER", PRINTER_STATUS_NO_TONER);
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

Napi::String WideToNapiString(Napi::Env env, const wchar_t* wstr) {
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &strTo[0], size_needed, NULL, NULL);
    return Napi::String::New(env, strTo);
}

inline std::wstring GetWStringFromNapiValue(const Napi::Value& value) {
    if (!value.IsString()) {
        throw Napi::TypeError::New(value.Env(), "String expected");
    }
    std::u16string temp = value.As<Napi::String>().Utf16Value();
    return std::wstring(temp.begin(), temp.end());
}


// N-API function implementations
Napi::Value GetPrinter(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    // Check arguments
    if (info.Length() < 1) {
        Napi::TypeError::New(env, "Wrong number of arguments")
            .ThrowAsJavaScriptException();
        return env.Null();
    }

    if (!info[0].IsString()) {
        Napi::TypeError::New(env, "String expected")
            .ThrowAsJavaScriptException();
        return env.Null();
    }

    // Convert JS string to wide string
    std::wstring printername = GetWStringFromNapiValue(info[0]);

    // Open printer handle
    HANDLE printerHandle;
    if (!OpenPrinterW((LPWSTR)printername.c_str(), &printerHandle, NULL)) {
        std::string error_str = "error on OpenPrinter: " + std::to_string(GetLastError());
        Napi::Error::New(env, error_str).ThrowAsJavaScriptException();
        return env.Null();
    }

    // Get required buffer size
    DWORD printers_size_bytes = 0;
    GetPrinterW(printerHandle, 2, NULL, 0, &printers_size_bytes);
    
    // Allocate memory for printer info
    PRINTER_INFO_2W* printer = (PRINTER_INFO_2W*)malloc(printers_size_bytes);
    if (!printer) {
        ClosePrinter(printerHandle);
        Napi::Error::New(env, "Error allocating memory for printer info")
            .ThrowAsJavaScriptException();
        return env.Null();
    }

    // Get printer info
    BOOL bOK = GetPrinterW(printerHandle, 2, (LPBYTE)printer, printers_size_bytes, &printers_size_bytes);
    if (!bOK) {
        free(printer);
        ClosePrinter(printerHandle);
        std::string error_str = "Error on GetPrinter: " + std::to_string(GetLastError());
        Napi::Error::New(env, error_str).ThrowAsJavaScriptException();
        return env.Null();
    }

    // Create result object
    Napi::Object result = Napi::Object::New(env);

    // Parse printer info
    result.Set("name", WideToNapiString(env, printer->pPrinterName));
    result.Set("server", WideToNapiString(env, printer->pServerName));
    result.Set("shareName", WideToNapiString(env, printer->pShareName));
    result.Set("portName", WideToNapiString(env, printer->pPortName));
    result.Set("driverName", WideToNapiString(env, printer->pDriverName));
    result.Set("location", WideToNapiString(env, printer->pLocation));
    result.Set("comment", WideToNapiString(env, printer->pComment));
    result.Set("status", Napi::Number::New(env, printer->Status));
    result.Set("attributes", Napi::Number::New(env, printer->Attributes));

    // Clean up
    free(printer);
    ClosePrinter(printerHandle);

    return result;
}

Napi::Value GetPrinters(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();

    DWORD printers_size = 0;
    DWORD printers_size_bytes = 0, dummyBytes = 0;
    DWORD flags = PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS;

    // Get required buffer size
    EnumPrintersW(flags, NULL, 2, NULL, 0, &printers_size_bytes, &printers_size);

    MemValue<PRINTER_INFO_2W> printers(printers_size_bytes);
    if (!printers)
    {
        throw Napi::Error::New(env, "Failed to allocate memory for printers");
    }

    BOOL bError = EnumPrintersW(flags, NULL, 2, (LPBYTE)(printers.get()),
                                printers_size_bytes, &dummyBytes, &printers_size);

    if (!bError)
    {
        std::string error = "EnumPrinters error: " + GetLastErrorMessage();
        throw Napi::Error::New(env, error);
    }

    Napi::Array result = Napi::Array::New(env, printers_size);
    PRINTER_INFO_2W *printer = printers.get();

    for (DWORD i = 0; i < printers_size; ++i, ++printer)
    {
        Napi::Object printerObj = Napi::Object::New(env);

        if (printer->pPrinterName && *printer->pPrinterName != L'\0')
        {
            printerObj.Set("name", Napi::String::New(env, (char16_t *)printer->pPrinterName));
        }

        if (printer->pServerName && *printer->pServerName != L'\0')
        {
            printerObj.Set("serverName", Napi::String::New(env, (char16_t *)printer->pServerName));
        }

        // Status array
        Napi::Array statusArray = Napi::Array::New(env);
        int statusIndex = 0;

        for (const auto &status : getStatusMap())
        {
            if (printer->Status & status.second)
            {
                statusArray.Set(statusIndex++, Napi::String::New(env, status.first));
            }
        }

        printerObj.Set("status", statusArray);
        printerObj.Set("priority", Napi::Number::New(env, printer->Priority));

        result.Set(i, printerObj);
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

    std::wstring printerName = GetWStringFromNapiValue(info[1]);
    std::wstring docName = GetWStringFromNapiValue(info[2]);
    std::wstring type = GetWStringFromNapiValue(info[3]);
    
    PrinterHandle printerHandle((LPWSTR)printerName.c_str());

    if (!printerHandle)
    {
        std::string error = "PrinterHandle error: " + GetLastErrorMessage();
        throw Napi::Error::New(env, error);
    }

    DOC_INFO_1W DocInfo;
    DocInfo.pDocName = (LPWSTR)docName.c_str();
    DocInfo.pOutputFile = NULL;
    DocInfo.pDatatype = (LPWSTR)type.c_str();

    DWORD jobId = StartDocPrinterW(*printerHandle, 1, (LPBYTE)&DocInfo);
    if (jobId == 0)
    {
        std::string error = "StartDocPrinter error: " + GetLastErrorMessage();
        throw Napi::Error::New(env, error);
    }

    if (!StartPagePrinter(*printerHandle))
    {
        std::string error = "StartPagePrinter error: " + GetLastErrorMessage();
        throw Napi::Error::New(env, error);
    }

    DWORD bytesWritten = 0;
    BOOL success = WritePrinter(*printerHandle, (LPVOID)data.c_str(),
                                (DWORD)data.size(), &bytesWritten);

    EndPagePrinter(*printerHandle);
    EndDocPrinter(*printerHandle);

    if (!success || bytesWritten != data.size())
    {
        throw Napi::Error::New(env, "Failed to write all data to printer");
    }

    return Napi::Number::New(env, jobId);
}



Napi::Object Init(Napi::Env env, Napi::Object exports) {
    // Set methods

    
    exports.Set("getPrinters", Napi::Function::New(env, GetPrinters));
    exports.Set("getDefaultPrinterName", Napi::Function::New(env, GetDefaultPrinterName));
    exports.Set("getPrinter", Napi::Function::New(env, GetPrinter));
    // exports.Set("getPrinterDriverOptions", Napi::Function::New(env, GetPrinterDriverOptions));
    // exports.Set("getJob", Napi::Function::New(env, GetJob));
    // exports.Set("setJob", Napi::Function::New(env, SetJob));
    // exports.Set("printDirect", Napi::Function::New(env, PrintDirect));
    // exports.Set("printFile", Napi::Function::New(env, PrintFile));
    // exports.Set("getSupportedPrintFormats", Napi::Function::New(env, GetSupportedPrintFormats));
    // exports.Set("getSupportedJobCommands", Napi::Function::New(env, GetSupportedJobCommands));
    
    return exports;
}


NODE_API_MODULE(node_printer, Init)



