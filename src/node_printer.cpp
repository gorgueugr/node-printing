/**
 * N-API layer of node-printing.
 *
 * This file is platform independent: it only talks to PrinterManager
 * (src/PrinterManager.hpp), which has one implementation per platform
 * (Winspool on Windows, CUPS on Linux and macOS).
 *
 * A successful call returns data. A failing call throws a JS Error whose
 * message comes straight from the spooler, so the caller always knows why a
 * print, a cancel or a listing failed.
 */

#include "node_printer.hpp"

#include "PrinterManager.hpp"

#include <napi.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace
{

/* ------------------------------------------------------------------ */
/* argument helpers                                                   */
/* ------------------------------------------------------------------ */

/** Reads a string argument as UTF-8 (mandatory or optional). */
bool readString(const Napi::CallbackInfo &info, size_t index, const char *name,
                std::string &out, bool mandatory)
{
    Napi::Env env = info.Env();
    if (info.Length() <= index || info[index].IsUndefined() || info[index].IsNull())
    {
        if (!mandatory)
        {
            out.clear();
            return true;
        }
        Napi::TypeError::New(env, std::string(name) + " is required and must be a string")
            .ThrowAsJavaScriptException();
        return false;
    }
    if (!info[index].IsString())
    {
        Napi::TypeError::New(env, std::string(name) + " must be a string")
            .ThrowAsJavaScriptException();
        return false;
    }
    out = info[index].As<Napi::String>().Utf8Value();
    return true;
}

/** Reads a mandatory integer argument. */
bool readInt(const Napi::CallbackInfo &info, size_t index, const char *name, int &out)
{
    Napi::Env env = info.Env();
    if (info.Length() <= index || !info[index].IsNumber())
    {
        Napi::TypeError::New(env, std::string(name) + " is required and must be a number")
            .ThrowAsJavaScriptException();
        return false;
    }
    out = info[index].As<Napi::Number>().Int32Value();
    return true;
}

/** Reads an optional boolean argument. */
bool readBool(const Napi::CallbackInfo &info, size_t index, bool fallback, bool &out)
{
    if (info.Length() <= index || info[index].IsUndefined() || info[index].IsNull())
    {
        out = fallback;
        return true;
    }
    if (!info[index].IsBoolean())
    {
        Napi::TypeError::New(info.Env(), "argument must be a boolean")
            .ThrowAsJavaScriptException();
        return false;
    }
    out = info[index].As<Napi::Boolean>().Value();
    return true;
}

/** Reads a string, a Buffer or a TypedArray as raw bytes. */
bool readData(const Napi::CallbackInfo &info, size_t index, const char *name, std::string &out)
{
    Napi::Env env = info.Env();
    if (info.Length() <= index)
    {
        Napi::TypeError::New(env, std::string(name) + " is required").ThrowAsJavaScriptException();
        return false;
    }
    if (info[index].IsString())
    {
        out = info[index].As<Napi::String>().Utf8Value();
        return true;
    }
    if (info[index].IsBuffer())
    {
        Napi::Buffer<char> buffer = info[index].As<Napi::Buffer<char> >();
        out.assign(buffer.Data(), buffer.Length());
        return true;
    }
    if (info[index].IsTypedArray())
    {
        Napi::TypedArray array = info[index].As<Napi::TypedArray>();
        Napi::ArrayBuffer buffer = array.ArrayBuffer();
        out.assign(static_cast<const char *>(buffer.Data()) + array.ByteOffset(),
                   array.ByteLength());
        return true;
    }
    Napi::TypeError::New(env, std::string(name) + " must be a string, a Buffer or a TypedArray")
        .ThrowAsJavaScriptException();
    return false;
}

/* ------------------------------------------------------------------ */
/* JS object builders                                                 */
/* ------------------------------------------------------------------ */

Napi::Value toDate(Napi::Env env, double milliseconds)
{
    if (milliseconds < 0)
    {
        return env.Null();
    }
    return Napi::Date::New(env, milliseconds);
}

Napi::Array toArray(Napi::Env env, const std::vector<std::string> &values)
{
    Napi::Array result = Napi::Array::New(env, values.size());
    for (size_t i = 0; i < values.size(); ++i)
    {
        result.Set(static_cast<uint32_t>(i), Napi::String::New(env, values[i]));
    }
    return result;
}

Napi::Object printerToObject(Napi::Env env, const PrinterInfo &printer)
{
    Napi::Object result = Napi::Object::New(env);
    result.Set("name", Napi::String::New(env, printer.name));
    result.Set("instance", printer.instance.empty()
                               ? Napi::Value(env.Null())
                               : Napi::Value(Napi::String::New(env, printer.instance)));
    result.Set("server", Napi::String::New(env, printer.server));
    result.Set("shareName", Napi::String::New(env, printer.shareName));
    result.Set("portName", Napi::String::New(env, printer.portName));
    result.Set("driverName", Napi::String::New(env, printer.driverName));
    result.Set("location", Napi::String::New(env, printer.location));
    result.Set("comment", Napi::String::New(env, printer.comment));
    result.Set("uri", Napi::String::New(env, printer.uri));
    result.Set("state", Napi::String::New(env, printer.state));
    result.Set("isDefault", Napi::Boolean::New(env, printer.isDefault));
    result.Set("isShared", Napi::Boolean::New(env, printer.isShared));
    result.Set("isLocal", Napi::Boolean::New(env, printer.isLocal));
    result.Set("acceptsJobs", Napi::Boolean::New(env, printer.acceptsJobs));
    result.Set("status", Napi::Number::New(env, printer.status));
    result.Set("statusArray", toArray(env, printer.statusArray));
    result.Set("attributes", Napi::Number::New(env, printer.attributes));
    result.Set("attributeArray", toArray(env, printer.attributeArray));
    result.Set("averagePPM", Napi::Number::New(env, printer.averagePPM));
    result.Set("cJobs", Napi::Number::New(env, printer.cJobs));
    result.Set("defaultPriority", Napi::Number::New(env, printer.defaultPriority));
    result.Set("startTime", Napi::Number::New(env, printer.startTime));
    result.Set("untilTime", Napi::Number::New(env, printer.untilTime));

    Napi::Object options = Napi::Object::New(env);
    for (std::map<std::string, std::string>::const_iterator it = printer.options.begin();
         it != printer.options.end(); ++it)
    {
        options.Set(it->first, Napi::String::New(env, it->second));
    }
    result.Set("options", options);
    return result;
}

Napi::Object jobToObject(Napi::Env env, const JobInfo &job)
{
    Napi::Object result = Napi::Object::New(env);
    result.Set("id", Napi::Number::New(env, job.id));
    result.Set("name", Napi::String::New(env, job.name));
    result.Set("printerName", Napi::String::New(env, job.printerName));
    result.Set("user", Napi::String::New(env, job.user));
    result.Set("format", Napi::String::New(env, job.format));
    result.Set("state", Napi::String::New(env, job.state));
    result.Set("status", Napi::String::New(env, job.status));
    result.Set("statusArray", toArray(env, job.statusArray));
    result.Set("priority", Napi::Number::New(env, job.priority));
    result.Set("size", Napi::Number::New(env, job.size));
    result.Set("position", Napi::Number::New(env, job.position));
    result.Set("totalPages", Napi::Number::New(env, job.totalPages));
    result.Set("pagesPrinted", Napi::Number::New(env, job.pagesPrinted));
    result.Set("submittedTime", toDate(env, job.submittedTime));
    result.Set("completedTime", toDate(env, job.completedTime));
    result.Set("processingTime", toDate(env, job.processingTime));
    return result;
}

Napi::Object devModeToObject(Napi::Env env, const PrinterDevMode &devMode)
{
    Napi::Object result = Napi::Object::New(env);
    result.Set("deviceName", Napi::String::New(env, devMode.deviceName));
    result.Set("paperSize", Napi::String::New(env, devMode.paperSize));
    result.Set("paperSizeCode", Napi::Number::New(env, devMode.paperSizeCode));
    result.Set("mediaType", Napi::String::New(env, devMode.mediaType));
    result.Set("orientation", Napi::String::New(env, orientationToString(devMode.orientation)));
    result.Set("duplex", Napi::String::New(env, duplexToString(devMode.duplex)));
    result.Set("color", Napi::String::New(env, colorToString(devMode.color)));
    result.Set("printQuality", Napi::String::New(env, printQualityToString(devMode.printQuality)));
    result.Set("copies", Napi::Number::New(env, devMode.copies));
    result.Set("collate", Napi::Boolean::New(env, devMode.collate));
    result.Set("scale", Napi::Number::New(env, devMode.scale));
    result.Set("defaultSource", Napi::String::New(env, devMode.defaultSource));
    result.Set("defaultSourceCode", Napi::Number::New(env, devMode.defaultSourceCode));
    result.Set("resolutionX", Napi::Number::New(env, devMode.resolutionX));
    result.Set("resolutionY", Napi::Number::New(env, devMode.resolutionY));
    return result;
}

/* ------------------------------------------------------------------ */
/* error plumbing                                                     */
/* ------------------------------------------------------------------ */

std::unique_ptr<PrinterManager> newManager()
{
    return std::unique_ptr<PrinterManager>(createPrinterManager());
}

/** Throws a JS Error carrying the spooler message. */
Napi::Value fail(Napi::Env env, const std::string &message)
{
    Napi::Error::New(env, message).ThrowAsJavaScriptException();
    return env.Undefined();
}

/* ------------------------------------------------------------------ */
/* asynchronous printing                                              */
/* ------------------------------------------------------------------ */

/** Runs a print on the libuv thread pool so the event loop keeps running. */
class PrintWorker : public Napi::AsyncWorker
{
public:
    PrintWorker(Napi::Env env, const Napi::Promise::Deferred &deferred, bool fromFile,
                const std::string &printer, const std::string &docName,
                const std::string &type, const std::string &payload)
        : Napi::AsyncWorker(env),
          _deferred(deferred),
          _fromFile(fromFile),
          _printer(printer),
          _docName(docName),
          _type(type),
          _payload(payload),
          _jobId(0)
    {
    }

    void Execute() override
    {
        std::unique_ptr<PrinterManager> manager = newManager();
        std::string error = _fromFile
                                ? manager->printFile(_printer, _docName, _type, _payload, _jobId)
                                : manager->printDirect(_printer, _docName, _type, _payload, _jobId);
        if (!error.empty())
        {
            SetError(error);
        }
    }

    void OnOK() override
    {
        _deferred.Resolve(Napi::Number::New(Env(), _jobId));
    }

    void OnError(const Napi::Error &error) override
    {
        _deferred.Reject(error.Value());
    }

private:
    Napi::Promise::Deferred _deferred;
    bool _fromFile;
    std::string _printer;
    std::string _docName;
    std::string _type;
    std::string _payload;
    int _jobId;
};

} // namespace

/* ------------------------------------------------------------------ */
/* exported functions                                                 */
/* ------------------------------------------------------------------ */

Napi::Value GetPrinters(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    std::unique_ptr<PrinterManager> manager = newManager();

    std::vector<PrinterInfo> printers;
    std::string error = manager->getPrinters(printers);
    if (!error.empty())
    {
        return fail(env, error);
    }

    Napi::Array result = Napi::Array::New(env, printers.size());
    for (size_t i = 0; i < printers.size(); ++i)
    {
        result.Set(static_cast<uint32_t>(i), printerToObject(env, printers[i]));
    }
    return result;
}

Napi::Value GetDefaultPrinterName(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    std::unique_ptr<PrinterManager> manager = newManager();

    PrinterName name;
    std::string error = manager->getDefaultPrinterName(name);
    if (!error.empty())
    {
        return fail(env, error);
    }
    return Napi::String::New(env, name);
}

Napi::Value GetPrinter(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    std::string name;
    if (!readString(info, 0, "printerName", name, true))
    {
        return env.Undefined();
    }

    std::unique_ptr<PrinterManager> manager = newManager();
    PrinterInfo printer;
    std::string error = manager->getOnePrinter(name, printer);
    if (!error.empty())
    {
        return fail(env, error);
    }
    return printerToObject(env, printer);
}

Napi::Value GetPrinterDevMode(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    std::string name;
    if (!readString(info, 0, "printerName", name, true))
    {
        return env.Undefined();
    }

    std::unique_ptr<PrinterManager> manager = newManager();
    PrinterDevMode devMode;
    std::string error = manager->getPrinterDevMode(name, devMode);
    if (!error.empty())
    {
        return fail(env, error);
    }
    return devModeToObject(env, devMode);
}

Napi::Value GetPrinterDriverOptions(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    std::string name;
    if (!readString(info, 0, "printerName", name, true))
    {
        return env.Undefined();
    }

    std::unique_ptr<PrinterManager> manager = newManager();
    std::map<std::string, std::map<std::string, bool> > options;
    std::string error = manager->getPrinterDriverOptions(name, options);
    if (!error.empty())
    {
        return fail(env, error);
    }

    Napi::Object result = Napi::Object::New(env);
    for (std::map<std::string, std::map<std::string, bool> >::const_iterator it = options.begin();
         it != options.end(); ++it)
    {
        Napi::Object choices = Napi::Object::New(env);
        for (std::map<std::string, bool>::const_iterator choice = it->second.begin();
             choice != it->second.end(); ++choice)
        {
            choices.Set(choice->first, Napi::Boolean::New(env, choice->second));
        }
        result.Set(it->first, choices);
    }
    return result;
}

/** getJobs([printerName][, includeCompleted]) */
Napi::Value GetJobs(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    std::string name;
    bool includeCompleted = false;

    if (info.Length() > 0 && info[0].IsBoolean())
    {
        if (!readBool(info, 0, false, includeCompleted))
        {
            return env.Undefined();
        }
    }
    else
    {
        if (!readString(info, 0, "printerName", name, false))
        {
            return env.Undefined();
        }
        if (!readBool(info, 1, false, includeCompleted))
        {
            return env.Undefined();
        }
    }

    std::unique_ptr<PrinterManager> manager = newManager();
    std::vector<JobInfo> jobs;
    std::string error = manager->getJobs(name, includeCompleted, jobs);
    if (!error.empty())
    {
        return fail(env, error);
    }

    Napi::Array result = Napi::Array::New(env, jobs.size());
    for (size_t i = 0; i < jobs.size(); ++i)
    {
        result.Set(static_cast<uint32_t>(i), jobToObject(env, jobs[i]));
    }
    return result;
}

Napi::Value GetJob(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    std::string name;
    int jobId = 0;
    if (!readString(info, 0, "printerName", name, true) || !readInt(info, 1, "jobId", jobId))
    {
        return env.Undefined();
    }

    std::unique_ptr<PrinterManager> manager = newManager();
    JobInfo job;
    std::string error = manager->getOneJob(name, jobId, job);
    if (!error.empty())
    {
        return fail(env, error);
    }
    return jobToObject(env, job);
}

/** setJob(printerName, jobId, command) */
Napi::Value SetJob(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    std::string name;
    std::string command;
    int jobId = 0;
    if (!readString(info, 0, "printerName", name, true) || !readInt(info, 1, "jobId", jobId) ||
        !readString(info, 2, "command", command, true))
    {
        return env.Undefined();
    }
    if (jobId <= 0)
    {
        return fail(env, "jobId must be a positive number");
    }

    std::unique_ptr<PrinterManager> manager = newManager();
    bool done = false;
    std::string error = manager->setJob(name, jobId, command, done);
    if (!error.empty())
    {
        return fail(env, error);
    }
    return Napi::Boolean::New(env, done);
}

/** printDirect(data, printerName, docName, type) */
Napi::Value PrintDirect(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    std::string data;
    std::string name;
    std::string docName;
    std::string type;
    if (!readData(info, 0, "data", data) || !readString(info, 1, "printerName", name, true) ||
        !readString(info, 2, "docName", docName, false) ||
        !readString(info, 3, "type", type, true))
    {
        return env.Undefined();
    }

    std::unique_ptr<PrinterManager> manager = newManager();
    int jobId = 0;
    std::string error = manager->printDirect(name, docName, type, data, jobId);
    if (!error.empty())
    {
        return fail(env, error);
    }
    return Napi::Number::New(env, jobId);
}

/** printDirectAsync(data, printerName, docName, type) -> Promise<jobId> */
Napi::Value PrintDirectAsync(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    Napi::Promise::Deferred deferred = Napi::Promise::Deferred::New(env);

    std::string data;
    std::string name;
    std::string docName;
    std::string type;
    if (!readData(info, 0, "data", data) || !readString(info, 1, "printerName", name, true) ||
        !readString(info, 2, "docName", docName, false) ||
        !readString(info, 3, "type", type, true))
    {
        deferred.Reject(env.Undefined());
        return deferred.Promise();
    }

    PrintWorker *worker = new PrintWorker(env, deferred, false, name, docName, type, data);
    worker->Queue();
    return deferred.Promise();
}

/** printFile(filename, docName, printerName, type) */
Napi::Value PrintFile(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    std::string filename;
    std::string docName;
    std::string name;
    std::string type;
    if (!readString(info, 0, "filename", filename, true) ||
        !readString(info, 1, "docName", docName, false) ||
        !readString(info, 2, "printerName", name, true) ||
        !readString(info, 3, "type", type, true))
    {
        return env.Undefined();
    }

    std::unique_ptr<PrinterManager> manager = newManager();
    int jobId = 0;
    std::string error = manager->printFile(name, docName, type, filename, jobId);
    if (!error.empty())
    {
        return fail(env, error);
    }
    return Napi::Number::New(env, jobId);
}

/** printFileAsync(filename, docName, printerName, type) -> Promise<jobId> */
Napi::Value PrintFileAsync(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    Napi::Promise::Deferred deferred = Napi::Promise::Deferred::New(env);

    std::string filename;
    std::string docName;
    std::string name;
    std::string type;
    if (!readString(info, 0, "filename", filename, true) ||
        !readString(info, 1, "docName", docName, false) ||
        !readString(info, 2, "printerName", name, true) ||
        !readString(info, 3, "type", type, true))
    {
        deferred.Reject(env.Undefined());
        return deferred.Promise();
    }

    PrintWorker *worker = new PrintWorker(env, deferred, true, name, docName, type, filename);
    worker->Queue();
    return deferred.Promise();
}

Napi::Value GetSupportedPrintFormats(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    std::unique_ptr<PrinterManager> manager = newManager();
    return toArray(env, manager->getSupportedPrintFormats());
}

Napi::Value GetSupportedJobCommands(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    std::unique_ptr<PrinterManager> manager = newManager();
    return toArray(env, manager->getSupportedJobCommands());
}

Napi::Object Init(Napi::Env env, Napi::Object exports)
{
    exports.Set("getPrinters", Napi::Function::New(env, GetPrinters));
    exports.Set("getDefaultPrinterName", Napi::Function::New(env, GetDefaultPrinterName));
    exports.Set("getPrinter", Napi::Function::New(env, GetPrinter));
    exports.Set("getPrinterDevMode", Napi::Function::New(env, GetPrinterDevMode));
    exports.Set("getPrinterDriverOptions", Napi::Function::New(env, GetPrinterDriverOptions));
    exports.Set("getJobs", Napi::Function::New(env, GetJobs));
    exports.Set("getJob", Napi::Function::New(env, GetJob));
    exports.Set("setJob", Napi::Function::New(env, SetJob));
    exports.Set("printDirect", Napi::Function::New(env, PrintDirect));
    exports.Set("printDirectAsync", Napi::Function::New(env, PrintDirectAsync));
    exports.Set("printFile", Napi::Function::New(env, PrintFile));
    exports.Set("printFileAsync", Napi::Function::New(env, PrintFileAsync));
    exports.Set("getSupportedPrintFormats", Napi::Function::New(env, GetSupportedPrintFormats));
    exports.Set("getSupportedJobCommands", Napi::Function::New(env, GetSupportedJobCommands));
    return exports;
}

NODE_API_MODULE(nodeprinting, Init)
