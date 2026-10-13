#ifndef NODE_PRINTER_HPP
#define NODE_PRINTER_HPP

#include <napi.h>

/** N-API entry points exported by the addon (see src/node_printer.cpp). */

/** All print queues known to the spooler. */
Napi::Value GetPrinters(const Napi::CallbackInfo &info);

/** Name of the default queue, empty string when none is configured. */
Napi::Value GetDefaultPrinterName(const Napi::CallbackInfo &info);

/** One queue by name. */
Napi::Value GetPrinter(const Napi::CallbackInfo &info);

/** Default configuration of one queue. */
Napi::Value GetPrinterDevMode(const Napi::CallbackInfo &info);

/** Driver options of one queue. */
Napi::Value GetPrinterDriverOptions(const Napi::CallbackInfo &info);

/** Jobs of a queue (or of every queue when the name is omitted). */
Napi::Value GetJobs(const Napi::CallbackInfo &info);

/** One job by id. */
Napi::Value GetJob(const Napi::CallbackInfo &info);

/** Control a job: CANCEL, PAUSE, RESUME, RESTART, DELETE, HOLD, RELEASE... */
Napi::Value SetJob(const Napi::CallbackInfo &info);

/** Send a string/Buffer to a queue. */
Napi::Value PrintDirect(const Napi::CallbackInfo &info);

/** Same as printDirect but asynchronous: returns a Promise. */
Napi::Value PrintDirectAsync(const Napi::CallbackInfo &info);

/** Send a file to a queue. */
Napi::Value PrintFile(const Napi::CallbackInfo &info);

/** Same as printFile but asynchronous: returns a Promise. */
Napi::Value PrintFileAsync(const Napi::CallbackInfo &info);

/** Data types accepted by printDirect/printFile on this platform. */
Napi::Value GetSupportedPrintFormats(const Napi::CallbackInfo &info);

/** Job commands accepted by setJob on this platform. */
Napi::Value GetSupportedJobCommands(const Napi::CallbackInfo &info);

#endif // NODE_PRINTER_HPP
