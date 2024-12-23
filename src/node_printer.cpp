#include "node_printer.hpp"

#include <napi.h>

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

// Helpers
bool GetStringOrBufferFromNapiValue(Napi::Value napiValue, std::string& outputData) {
    if (napiValue.IsString()) {
        outputData = napiValue.As<Napi::String>().Utf8Value();
        return true;
    }
    if (napiValue.IsBuffer()) {
        Napi::Buffer<char> buffer = napiValue.As<Napi::Buffer<char>>();
        outputData.assign(buffer.Data(), buffer.Length());
        return true;
    }
    return false;
}
