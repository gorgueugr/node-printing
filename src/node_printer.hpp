#ifndef NODE_PRINTER_HPP
#define NODE_PRINTER_HPP

#include <napi.h>
#include <string>

/**
 * Send data to printer
 *
 * @param data String/NativeBuffer, mandatory, raw data bytes
 * @param printername String, mandatory, specifying printer name
 * @param docname String, mandatory, specifying document name
 * @param type String, mandatory, specifying data type. E.G.: RAW, TEXT, ...
 *
 * @returns true for success, false for failure.
 */
// Napi::Value PrintDirect(const Napi::CallbackInfo& info);

/**
 * Send file to printer
 *
 * @param filename String, mandatory, specifying filename to print
 * @param docname String, mandatory, specifying document name
 * @param printer String, mandatory, specifying printer name
 *
 * @returns jobId for success, or error message for failure.
 */
// Napi::Value PrintFile(const Napi::CallbackInfo& info);

/** Retrieve all printers and jobs
 * posix: minimum version: CUPS 1.1.21/OS X 10.4
 */
Napi::Array GetPrinters(const Napi::CallbackInfo &info);

/**
 * Return default printer name, if null then default printer is not set
 */
Napi::String GetDefaultPrinterName(const Napi::CallbackInfo &info);

/** Retrieve printer info and jobs
 * @param printer name String
 */
Napi::Value GetOnePrinter(const Napi::CallbackInfo &info);

/** Retrieve printer driver info
 * @param printer name String
 */
// Napi::Value GetPrinterDriverOptions(const Napi::CallbackInfo& info);

/** Retrieve job info
 *  @param printer name String
 *  @param job id Number
 */
Napi::Value GetOneJob(const Napi::CallbackInfo &info);

/** Set job command.
 * arguments:
 * @param printer name String
 * @param job id Number
 * @param job command String
 * Possible commands:
 *      "CANCEL"
 *      "PAUSE"
 *      "RESTART"
 *      "RESUME"
 *      "DELETE"
 *      "SENT-TO-PRINTER"
 *      "LAST-PAGE-EJECTED"
 *      "RETAIN"
 *      "RELEASE"
 */
Napi::Value SetOneJob(const Napi::CallbackInfo &info);

/** Get supported print formats for printDirect. It depends on platform
 */
//  Napi::Value GetSupportedPrintFormats(const Napi::CallbackInfo& info);

/** Get supported job commands for setJob method
 */
Napi::Value GetSupportedJobCommands(const Napi::CallbackInfo &info);

#endif
