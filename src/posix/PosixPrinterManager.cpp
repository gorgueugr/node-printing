/**
 * Linux and macOS backend of node-printing, implemented with CUPS.
 *
 * The public interface (src/PrinterManager.hpp) works with UTF-8 std::string
 * and error strings; CUPS is UTF-8 natively, so no conversion is needed here.
 *
 * Everything is best-effort and defensive: a queue that reports no PPD, no
 * DEVMODE equivalent or no jobs produces a clear error message, never a crash.
 */

#if !defined(_WIN32)

#include "../PrinterManager.hpp"

#include <cups/cups.h>
#include <cups/ipp.h>
#include <cups/ppd.h>

#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

/*
 * The PPD API used below (cupsGetPPD2, ppdOpenFile, ppdMarkDefaults,
 * cupsMarkOptions, ppdFindOption) is flagged deprecated since CUPS 2.4 in
 * favour of cupsCopyDestInfo/cupsCopyDestMedia, but it remains the only way to
 * read a driver's complete option set and it is present in every CUPS 1.6+
 * installation, macOS included. Silence only that warning class.
 */
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

/* Some CUPS releases do not ship every format macro. */
#ifndef CUPS_FORMAT_AUTO
#define CUPS_FORMAT_AUTO "application/octet-stream"
#endif
#ifndef CUPS_FORMAT_COMMAND
#define CUPS_FORMAT_COMMAND "application/vnd.cups-command"
#endif
#ifndef CUPS_FORMAT_JPEG
#define CUPS_FORMAT_JPEG "image/jpeg"
#endif
#ifndef CUPS_FORMAT_PDF
#define CUPS_FORMAT_PDF "application/pdf"
#endif
#ifndef CUPS_FORMAT_POSTSCRIPT
#define CUPS_FORMAT_POSTSCRIPT "application/postscript"
#endif
#ifndef CUPS_FORMAT_RAW
#define CUPS_FORMAT_RAW "application/vnd.cups-raw"
#endif
#ifndef CUPS_FORMAT_TEXT
#define CUPS_FORMAT_TEXT "text/plain"
#endif

namespace
{

/* ------------------------------------------------------------------ */
/* small string helpers                                               */
/* ------------------------------------------------------------------ */

std::string toLowerAscii(const std::string &value)
{
    std::string result(value);
    for (size_t i = 0; i < result.size(); ++i)
    {
        if (result[i] >= 'A' && result[i] <= 'Z')
        {
            result[i] = static_cast<char>(result[i] - 'A' + 'a');
        }
    }
    return result;
}

bool startsWith(const std::string &value, const char *prefix)
{
    size_t length = strlen(prefix);
    return value.size() >= length && value.compare(0, length, prefix) == 0;
}

/** "office/color" -> "office" */
std::string queueName(const std::string &name)
{
    size_t slash = name.find('/');
    return slash == std::string::npos ? name : name.substr(0, slash);
}

/** "office/color" -> "color" (empty when there is no instance) */
std::string instanceName(const std::string &name)
{
    size_t slash = name.find('/');
    return slash == std::string::npos ? std::string() : name.substr(slash + 1);
}

std::string numberToString(long long value)
{
    std::ostringstream stream;
    stream << value;
    return stream.str();
}

/** Host part of the CUPS server, "localhost" for a local domain socket. */
std::string cupsHost()
{
    const char *server = cupsServer();
    if (server == NULL || server[0] == '\0' || server[0] == '/')
    {
        return "localhost";
    }
    return server;
}

std::string printerUri(const std::string &queue)
{
    return "ipp://" + cupsHost() + "/printers/" + queue;
}

std::string jobUri(int jobId)
{
    return "ipp://" + cupsHost() + "/jobs/" + numberToString(jobId);
}

/**
 * Name of the queue CUPS reports as the system default ("" when there is none).
 *
 * cupsGetNamedDest does not fill the is_default flag of a destination, so the
 * default queue has to be looked up separately and compared by name.
 */
std::string systemDefaultQueue()
{
    const char *name = cupsGetDefault2(CUPS_HTTP_DEFAULT);
    return name == NULL ? std::string() : std::string(name);
}

/* ------------------------------------------------------------------ */
/* RAII wrappers                                                      */
/* ------------------------------------------------------------------ */

struct IppMessage
{
    ipp_t *value;
    explicit IppMessage(ipp_t *message = NULL) : value(message) {}
    ~IppMessage()
    {
        if (value != NULL)
        {
            ippDelete(value);
        }
    }
    ipp_t *release()
    {
        ipp_t *result = value;
        value = NULL;
        return result;
    }

private:
    IppMessage(const IppMessage &);
    IppMessage &operator=(const IppMessage &);
};

struct DestList
{
    cups_dest_t *dests;
    int count;
    DestList() : dests(NULL), count(0) {}
    ~DestList()
    {
        if (dests != NULL)
        {
            cupsFreeDests(count, dests);
        }
    }

private:
    DestList(const DestList &);
    DestList &operator=(const DestList &);
};

struct NamedDest
{
    cups_dest_t *dest;
    explicit NamedDest(cups_dest_t *value = NULL) : dest(value) {}
    ~NamedDest()
    {
        if (dest != NULL)
        {
            cupsFreeDests(1, dest);
        }
    }

private:
    NamedDest(const NamedDest &);
    NamedDest &operator=(const NamedDest &);
};

struct HttpConnection
{
    http_t *value;
    HttpConnection() : value(httpConnect2(cupsServer(), ippPort(), NULL, AF_UNSPEC,
                                         cupsEncryption(), 1, 10000, NULL))
    {
    }
    ~HttpConnection()
    {
        if (value != NULL)
        {
            httpClose(value);
        }
    }

private:
    HttpConnection(const HttpConnection &);
    HttpConnection &operator=(const HttpConnection &);
};

struct PpdFile
{
    ppd_file_t *value;
    std::string filename;
    explicit PpdFile() : value(NULL) {}
    ~PpdFile()
    {
        if (value != NULL)
        {
            ppdClose(value);
        }
        if (!filename.empty())
        {
            unlink(filename.c_str());
        }
    }

private:
    PpdFile(const PpdFile &);
    PpdFile &operator=(const PpdFile &);
};

/* ------------------------------------------------------------------ */
/* IPP helpers                                                        */
/* ------------------------------------------------------------------ */

/** Collects every value of one attribute of one group. */
std::vector<std::string> attributeStrings(ipp_attribute_t *attribute);

class IppClient
{
public:
    IppClient()
    {
        if (_connection.value == NULL)
        {
            _error = std::string("cannot connect to the CUPS server: ") + cupsLastErrorString();
        }
    }

    bool connected() const { return _connection.value != NULL; }
    const std::string &connectionError() const { return _error; }

    /** Sends `request` (ownership taken) and returns the response, or NULL
     *  with a human readable error in `error`. */
    ipp_t *send(ipp_t *request, const std::string &resource, std::string &error)
    {
        if (_connection.value == NULL)
        {
            ippDelete(request);
            error = _error;
            return NULL;
        }

        ipp_t *response = cupsDoRequest(_connection.value, request, resource.c_str());
        if (response == NULL)
        {
            error = "CUPS request to " + resource + " failed: " + cupsLastErrorString();
            return NULL;
        }

        ipp_status_t status = ippGetStatusCode(response);
        if (status >= IPP_STATUS_ERROR_BAD_REQUEST)
        {
            std::ostringstream stream;
            stream << "CUPS request to " << resource << " failed: " << ippErrorString(status);
            ipp_attribute_t *message = ippFindAttribute(response, "status-message", IPP_TAG_TEXT);
            if (message != NULL)
            {
                const char *text = ippGetString(message, 0, NULL);
                if (text != NULL && text[0] != '\0')
                {
                    stream << " (" << text << ")";
                }
            }
            error = stream.str();
            ippDelete(response);
            return NULL;
        }

        return response;
    }

private:
    HttpConnection _connection;
    std::string _error;

    IppClient(const IppClient &);
    IppClient &operator=(const IppClient &);
};

/** Adds the value of one attribute of one group, first value only. */
std::string jobAttributeString(ipp_attribute_t *attribute)
{
    if (attribute == NULL)
    {
        return std::string();
    }
    const char *value = ippGetString(attribute, 0, NULL);
    return value == NULL ? std::string() : std::string(value);
}

/** Same, but for attributes that may be numeric. */
std::string attributeToText(ipp_attribute_t *attribute)
{
    if (attribute == NULL)
    {
        return std::string();
    }
    switch (ippGetValueTag(attribute))
    {
    case IPP_TAG_INTEGER:
    case IPP_TAG_ENUM:
        return numberToString(ippGetInteger(attribute, 0));
    case IPP_TAG_BOOLEAN:
        return ippGetBoolean(attribute, 0) ? "true" : "false";
    case IPP_TAG_RANGE:
    {
        int lower = 0;
        int upper = 0;
        ippGetRange(attribute, 0, &upper);
        lower = ippGetInteger(attribute, 0);
        return numberToString(lower) + "-" + numberToString(upper);
    }
    default:
        break;
    }
    const char *value = ippGetString(attribute, 0, NULL);
    return value == NULL ? std::string() : std::string(value);
}

std::vector<std::string> attributeStrings(ipp_attribute_t *attribute)
{
    std::vector<std::string> result;
    if (attribute == NULL)
    {
        return result;
    }
    int count = ippGetCount(attribute);
    for (int i = 0; i < count; ++i)
    {
        const char *value = ippGetString(attribute, i, NULL);
        if (value != NULL && value[0] != '\0')
        {
            result.push_back(value);
        }
    }
    return result;
}

/* ------------------------------------------------------------------ */
/* canonical naming                                                   */
/* ------------------------------------------------------------------ */

/** CUPS media keyword -> human readable paper size. */
std::string paperSizeName(const std::string &media)
{
    static const struct
    {
        const char *keyword;
        const char *name;
    } table[] = {
        {"iso_a3_297x420mm", "A3"}, {"a3", "A3"},
        {"iso_a4_210x297mm", "A4"}, {"a4", "A4"},
        {"iso_a5_148x210mm", "A5"}, {"a5", "A5"},
        {"iso_a6_105x148mm", "A6"}, {"a6", "A6"},
        {"jis_b4_257x364mm", "B4"}, {"b4", "B4"},
        {"jis_b5_182x257mm", "B5"}, {"b5", "B5"},
        {"na_letter_8.5x11in", "LETTER"}, {"letter", "LETTER"},
        {"na_legal_8.5x14in", "LEGAL"}, {"legal", "LEGAL"},
        {"na_ledger_11x17in", "LEDGER"}, {"tabloid", "TABLOID"}, {"ledger", "LEDGER"},
        {"na_executive_7.25x10.5in", "EXECUTIVE"}, {"executive", "EXECUTIVE"},
        {"na_number-10_4.125x9.5in", "ENV10"}, {"com10", "ENV10"},
        {"na_index-4x6_4x6in", "4X6"}, {"4x6", "4X6"},
        {"custom", "CUSTOM"}};

    std::string lower = toLowerAscii(media);
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); ++i)
    {
        if (lower == table[i].keyword)
        {
            return table[i].name;
        }
    }
    // "iso_a4_210x297mm-borderless" and other variants
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); ++i)
    {
        if (lower.size() > strlen(table[i].keyword) &&
            lower.compare(0, strlen(table[i].keyword), table[i].keyword) == 0)
        {
            return table[i].name;
        }
    }
    return media;
}

/** CUPS media-source keyword -> human readable tray name. */
std::string mediaSourceName(const std::string &source)
{
    std::string lower = toLowerAscii(source);
    if (lower == "auto" || lower == "auto-select" || lower == "default")
    {
        return "Auto";
    }
    if (lower == "manual" || lower == "manual-feed")
    {
        return "Manual";
    }
    if (lower == "by-pass-tray" || lower == "bypass-tray")
    {
        return "Bypass tray";
    }
    if (startsWith(lower, "tray-"))
    {
        return "Tray " + source.substr(5);
    }
    if (startsWith(lower, "cassette-"))
    {
        return "Cassette " + source.substr(9);
    }
    return source;
}

/** IPP print-quality values. */
PrintQuality printQualityFromIpp(int value)
{
    switch (value)
    {
    case 3:
        return DRAFT;
    case 4:
        return MEDIUM;
    case 5:
        return HIGH;
    default:
        return MEDIUM;
    }
}

/** PPD OutputMode / cupsPrintQuality keyword -> quality. */
PrintQuality printQualityFromKeyword(const std::string &keyword)
{
    std::string lower = toLowerAscii(keyword);
    if (lower.find("draft") != std::string::npos)
    {
        return DRAFT;
    }
    if (lower.find("high") != std::string::npos || lower.find("best") != std::string::npos)
    {
        return HIGH;
    }
    if (lower.find("low") != std::string::npos || lower.find("fast") != std::string::npos)
    {
        return LOW;
    }
    return MEDIUM;
}

const char *printerStateName(int state)
{
    switch (state)
    {
    case IPP_PRINTER_IDLE:
        return "idle";
    case IPP_PRINTER_PROCESSING:
        return "processing";
    case IPP_PRINTER_STOPPED:
        return "stopped";
    default:
        return "unknown";
    }
}

const char *jobStateName(int state)
{
    switch (state)
    {
    case IPP_JOB_PENDING:
        return "pending";
    case IPP_JOB_HELD:
        return "held";
    case IPP_JOB_PROCESSING:
        return "processing";
    case IPP_JOB_STOPPED:
        return "stopped";
    case IPP_JOB_CANCELED:
        return "canceled";
    case IPP_JOB_ABORTED:
        return "aborted";
    case IPP_JOB_COMPLETED:
        return "completed";
    default:
        return "unknown";
    }
}

const char *jobStateStatus(int state)
{
    switch (state)
    {
    case IPP_JOB_PENDING:
        return "PENDING";
    case IPP_JOB_HELD:
        return "PAUSED";
    case IPP_JOB_PROCESSING:
        return "PRINTING";
    case IPP_JOB_STOPPED:
        return "STOPPED";
    case IPP_JOB_CANCELED:
        return "CANCELLED";
    case IPP_JOB_ABORTED:
        return "ABORTED";
    case IPP_JOB_COMPLETED:
        return "PRINTED";
    default:
        return "UNKNOWN";
    }
}

/** job-state-reasons / printer-state-reasons value -> canonical name. */
std::string reasonName(const std::string &reason)
{
    static const struct
    {
        const char *ipp;
        const char *name;
    } table[] = {
        {"job-printing", "PRINTING"},
        {"job-pending", "PENDING"},
        {"job-held", "PAUSED"},
        {"job-stopped", "STOPPED"},
        {"job-completed-successfully", "PRINTED"},
        {"job-completed-with-errors", "PRINTED-WITH-ERRORS"},
        {"job-canceled-by-user", "CANCELLED"},
        {"job-canceled-at-device", "CANCELLED"},
        {"job-aborted-by-system", "ABORTED"},
        {"job-hold-until-specified", "HOLD-UNTIL"},
        {"job-incoming", "SPOOLING"},
        {"job-data-insufficient", "WAITING-FOR-DATA"},
        {"resources-are-not-ready", "RESOURCES-NOT-READY"},
        {"none", "OK"},
        {"media-empty", "PAPER-OUT"},
        {"media-jam", "PAPER-JAM"},
        {"media-needed", "PAPER-NEEDED"},
        {"toner-empty", "TONER-OUT"},
        {"marker-supply-empty", "TONER-OUT"},
        {"marker-supply-low", "TONER-LOW"},
        {"cover-open", "COVER-OPEN"},
        {"door-open", "DOOR-OPEN"},
        {"offline", "OFFLINE"},
        {"paused", "PAUSED"},
        {"shutdown", "SHUTDOWN"},
        {"moving-to-paused", "PAUSING"},
        {"connecting-to-device", "CONNECTING"}};

    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); ++i)
    {
        if (reason == table[i].ipp)
        {
            return table[i].name;
        }
    }
    return toUpperAscii(reason);
}

/** CUPS printer-type bit names. */
struct FlagName
{
    int flag;
    const char *name;
};

const char *flagNames(const FlagName *table, size_t count, int flags, std::string &result)
{
    result.clear();
    for (size_t i = 0; i < count; ++i)
    {
        if ((flags & table[i].flag) != 0)
        {
            if (!result.empty())
            {
                result += ",";
            }
            result += table[i].name;
        }
    }
    return result.c_str();
}

/** Data type name -> MIME type. Empty result means "unknown type". */
std::string formatToMime(const std::string &type)
{
    std::string key = toUpperAscii(type);
    if (key == "RAW")
    {
        return CUPS_FORMAT_RAW;
    }
    if (key == "TEXT" || key == "TXT")
    {
        return CUPS_FORMAT_TEXT;
    }
    if (key == "PDF")
    {
        return CUPS_FORMAT_PDF;
    }
    if (key == "POSTSCRIPT" || key == "PS")
    {
        return CUPS_FORMAT_POSTSCRIPT;
    }
    if (key == "JPEG" || key == "JPG")
    {
        return CUPS_FORMAT_JPEG;
    }
    if (key == "PNG")
    {
        return "image/png";
    }
    if (key == "COMMAND")
    {
        return CUPS_FORMAT_COMMAND;
    }
    if (key == "AUTO")
    {
        return CUPS_FORMAT_AUTO;
    }
    if (type.find('/') != std::string::npos)
    {
        return type; // already a MIME type
    }
    return std::string();
}

/** MIME type -> short data type name. */
std::string mimeToFormat(const std::string &mime)
{
    if (mime == CUPS_FORMAT_RAW)
    {
        return "RAW";
    }
    if (mime == CUPS_FORMAT_TEXT)
    {
        return "TEXT";
    }
    if (mime == CUPS_FORMAT_PDF)
    {
        return "PDF";
    }
    if (mime == CUPS_FORMAT_POSTSCRIPT)
    {
        return "POSTSCRIPT";
    }
    if (mime == CUPS_FORMAT_JPEG)
    {
        return "JPEG";
    }
    if (mime == CUPS_FORMAT_COMMAND)
    {
        return "COMMAND";
    }
    return mime;
}

/* ------------------------------------------------------------------ */
/* printer parsing                                                    */
/* ------------------------------------------------------------------ */

/** Fills a PrinterInfo from the options of a CUPS destination. */
void fillPrinterFromDest(const cups_dest_t *dest, PrinterInfo &printer)
{
    static const FlagName typeFlags[] = {
        {CUPS_PRINTER_CLASS, "CLASS"},
        {CUPS_PRINTER_REMOTE, "REMOTE"},
        {CUPS_PRINTER_FAX, "FAX"},
        {CUPS_PRINTER_SCANNER, "SCANNER"},
        {CUPS_PRINTER_BW, "MONOCHROME"},
        {CUPS_PRINTER_COLOR, "COLOR"},
        {CUPS_PRINTER_DUPLEX, "DUPLEX"},
        {CUPS_PRINTER_STAPLE, "STAPLE"},
        {CUPS_PRINTER_COPIES, "COPIES"},
        {CUPS_PRINTER_COLLATE, "COLLATE"},
        {CUPS_PRINTER_PUNCH, "PUNCH"},
        {CUPS_PRINTER_COVER, "COVER"},
        {CUPS_PRINTER_BIND, "BIND"},
        {CUPS_PRINTER_SORT, "SORT"},
        {CUPS_PRINTER_IMPLICIT, "IMPLICIT"},
        {CUPS_PRINTER_REJECTING, "REJECTING-JOBS"},
        {CUPS_PRINTER_DISCOVERED, "DISCOVERED"},
        {CUPS_PRINTER_MFP, "MFP"}};

    printer.name = dest->name != NULL ? dest->name : "";
    printer.instance = dest->instance != NULL ? dest->instance : "";
    printer.isDefault = dest->is_default != 0;
    printer.isLocal = true;
    printer.portName = printer.name;
    printer.uri = printerUri(printer.name);

    const char *server = cupsServer();
    if (server != NULL && server[0] != '\0' && server[0] != '/')
    {
        printer.server = server;
        printer.isLocal = false;
    }

    for (int i = 0; i < dest->num_options; ++i)
    {
        const cups_option_t &option = dest->options[i];
        std::string name = option.name != NULL ? option.name : "";
        std::string value = option.value != NULL ? option.value : "";
        if (!name.empty())
        {
            printer.options[name] = value;
        }

        if (name == "printer-info")
        {
            printer.comment = value;
        }
        else if (name == "printer-location")
        {
            printer.location = value;
        }
        else if (name == "printer-make-and-model")
        {
            printer.driverName = value;
        }
        else if (name == "printer-state")
        {
            printer.status = atoi(value.c_str());
            printer.state = printerStateName(printer.status);
            printer.statusArray.push_back(printer.state);
        }
        else if (name == "printer-type")
        {
            printer.attributes = atoi(value.c_str());
            std::string flags;
            flagNames(typeFlags, sizeof(typeFlags) / sizeof(typeFlags[0]),
                      printer.attributes, flags);
            size_t begin = 0;
            while (begin <= flags.size())
            {
                size_t comma = flags.find(',', begin);
                std::string flag = flags.substr(begin, comma == std::string::npos
                                                           ? std::string::npos
                                                           : comma - begin);
                if (!flag.empty())
                {
                    printer.attributeArray.push_back(flag);
                }
                if (comma == std::string::npos)
                {
                    break;
                }
                begin = comma + 1;
            }
            printer.isShared = (printer.attributes & CUPS_PRINTER_CLASS) != 0 ||
                               (printer.attributes & CUPS_PRINTER_REMOTE) != 0;
            printer.acceptsJobs = (printer.attributes & CUPS_PRINTER_REJECTING) == 0;
        }
        else if (name == "printer-is-shared")
        {
            printer.isShared = (value == "true" || value == "1");
        }
        else if (name == "printer-is-accepting-jobs")
        {
            printer.acceptsJobs = (value == "true" || value == "1");
        }
        else if (name == "device-uri")
        {
            printer.uri = value;
        }
        else if (name == "printer-state-reasons")
        {
            // CUPS reports the reasons as a single comma separated value here
            size_t begin = 0;
            while (begin <= value.size())
            {
                size_t comma = value.find(',', begin);
                std::string reason = value.substr(begin, comma == std::string::npos
                                                             ? std::string::npos
                                                             : comma - begin);
                if (!reason.empty())
                {
                    printer.statusArray.push_back(reasonName(reason));
                }
                if (comma == std::string::npos)
                {
                    break;
                }
                begin = comma + 1;
            }
        }
    }

    if (printer.name.empty() && dest->name != NULL)
    {
        printer.name = dest->name;
    }
    if (printer.portName.empty())
    {
        printer.portName = printer.name;
    }
}

/** Enriches a PrinterInfo with a Get-Printer-Attributes response. */
void fillPrinterFromIpp(ipp_t *response, PrinterInfo &printer)
{
    for (ipp_attribute_t *attribute = ippFirstAttribute(response);
         attribute != NULL;
         attribute = ippNextAttribute(response))
    {
        if (ippGetGroupTag(attribute) != IPP_TAG_PRINTER)
        {
            continue;
        }
        std::string name = ippGetName(attribute) != NULL ? ippGetName(attribute) : "";

        if (name == "printer-name")
        {
            std::string value = jobAttributeString(attribute);
            if (!value.empty())
            {
                printer.name = value;
            }
        }
        else if (name == "printer-info")
        {
            printer.comment = jobAttributeString(attribute);
        }
        else if (name == "printer-location")
        {
            printer.location = jobAttributeString(attribute);
        }
        else if (name == "printer-make-and-model")
        {
            printer.driverName = jobAttributeString(attribute);
        }
        else if (name == "printer-state")
        {
            printer.status = ippGetInteger(attribute, 0);
            printer.state = printerStateName(printer.status);
            bool known = false;
            for (size_t i = 0; i < printer.statusArray.size(); ++i)
            {
                if (printer.statusArray[i] == printer.state)
                {
                    known = true;
                }
            }
            if (!known)
            {
                printer.statusArray.push_back(printer.state);
            }
        }
        else if (name == "printer-state-reasons")
        {
            std::vector<std::string> reasons = attributeStrings(attribute);
            for (size_t i = 0; i < reasons.size(); ++i)
            {
                std::string reason = reasonName(reasons[i]);
                bool duplicate = false;
                for (size_t j = 0; j < printer.statusArray.size(); ++j)
                {
                    if (printer.statusArray[j] == reason)
                    {
                        duplicate = true;
                    }
                }
                if (!duplicate)
                {
                    printer.statusArray.push_back(reason);
                }
            }
        }
        else if (name == "printer-type")
        {
            printer.attributes = ippGetInteger(attribute, 0);
        }
        else if (name == "printer-is-shared")
        {
            printer.isShared = ippGetBoolean(attribute, 0) != 0;
        }
        else if (name == "printer-is-accepting-jobs")
        {
            printer.acceptsJobs = ippGetBoolean(attribute, 0) != 0;
        }
        else if (name == "device-uri")
        {
            printer.uri = jobAttributeString(attribute);
        }
        else if (name == "printer-uri-supported")
        {
            std::vector<std::string> values = attributeStrings(attribute);
            if (!values.empty())
            {
                printer.uri = values[0];
            }
        }
        else
        {
            std::string value = attributeToText(attribute);
            if (!value.empty())
            {
                printer.options[name] = value;
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* job parsing                                                        */
/* ------------------------------------------------------------------ */

void parseJobGroup(const std::vector<ipp_attribute_t *> &attributes, JobInfo &job)
{
    for (size_t i = 0; i < attributes.size(); ++i)
    {
        ipp_attribute_t *attribute = attributes[i];
        std::string name = ippGetName(attribute) != NULL ? ippGetName(attribute) : "";

        if (name == "job-id")
        {
            job.id = ippGetInteger(attribute, 0);
        }
        else if (name == "job-name")
        {
            job.name = jobAttributeString(attribute);
        }
        else if (name == "job-originating-user-name" || name == "job-originating-user")
        {
            job.user = jobAttributeString(attribute);
        }
        else if (name == "document-format")
        {
            job.format = mimeToFormat(jobAttributeString(attribute));
        }
        else if (name == "job-printer-uri" || name == "job-printer-name")
        {
            std::string value = jobAttributeString(attribute);
            size_t slash = value.find_last_of('/');
            job.printerName = slash == std::string::npos ? value : value.substr(slash + 1);
        }
        else if (name == "job-priority")
        {
            job.priority = ippGetInteger(attribute, 0);
        }
        else if (name == "job-size")
        {
            job.size = static_cast<double>(ippGetInteger(attribute, 0));
        }
        else if (name == "job-k-octets")
        {
            if (job.size <= 0)
            {
                job.size = static_cast<double>(ippGetInteger(attribute, 0)) * 1024.0;
            }
        }
        else if (name == "job-state")
        {
            int state = ippGetInteger(attribute, 0);
            job.state = jobStateName(state);
            job.status = jobStateStatus(state);
        }
        else if (name == "job-state-reasons")
        {
            std::vector<std::string> reasons = attributeStrings(attribute);
            for (size_t j = 0; j < reasons.size(); ++j)
            {
                std::string reason = reasonName(reasons[j]);
                bool duplicate = false;
                for (size_t k = 0; k < job.statusArray.size(); ++k)
                {
                    if (job.statusArray[k] == reason)
                    {
                        duplicate = true;
                    }
                }
                if (!duplicate)
                {
                    job.statusArray.push_back(reason);
                }
            }
        }
        else if (name == "job-media-sheets-completed" || name == "job-impressions-completed" ||
                 name == "job-pages-completed")
        {
            job.pagesPrinted = ippGetInteger(attribute, 0);
        }
        else if (name == "job-pages" || name == "job-impressions")
        {
            job.totalPages = ippGetInteger(attribute, 0);
        }
        else if (name == "time-at-creation")
        {
            job.submittedTime = static_cast<double>(ippGetInteger(attribute, 0)) * 1000.0;
        }
        else if (name == "time-at-completed")
        {
            job.completedTime = static_cast<double>(ippGetInteger(attribute, 0)) * 1000.0;
        }
        else if (name == "time-at-processing")
        {
            job.processingTime = static_cast<double>(ippGetInteger(attribute, 0)) * 1000.0;
        }
    }
}

/** One JobInfo per JOB group of an IPP response. */
void parseJobs(ipp_t *response, std::vector<JobInfo> &jobs)
{
    std::vector<ipp_attribute_t *> group;
    ipp_tag_t previousGroup = IPP_TAG_ZERO;

    for (ipp_attribute_t *attribute = ippFirstAttribute(response);
         attribute != NULL;
         attribute = ippNextAttribute(response))
    {
        ipp_tag_t groupTag = ippGetGroupTag(attribute);
        if (groupTag != previousGroup && !group.empty())
        {
            JobInfo job;
            parseJobGroup(group, job);
            jobs.push_back(job);
            group.clear();
        }
        previousGroup = groupTag;
        if (groupTag == IPP_TAG_JOB)
        {
            group.push_back(attribute);
        }
    }

    if (!group.empty())
    {
        JobInfo job;
        parseJobGroup(group, job);
        jobs.push_back(job);
    }
}

/* ------------------------------------------------------------------ */
/* chunked document sources                                           */
/* ------------------------------------------------------------------ */

class ChunkSource
{
public:
    virtual ~ChunkSource() {}
    /** Copies the next chunk into `chunk`; false when the source is done. */
    virtual bool next(std::string &chunk) = 0;
};

class BufferSource : public ChunkSource
{
public:
    explicit BufferSource(const std::string &data) : _data(data), _offset(0) {}

    virtual bool next(std::string &chunk)
    {
        const size_t chunkSize = 1024 * 1024;
        if (_offset >= _data.size())
        {
            return false;
        }
        size_t length = _data.size() - _offset;
        if (length > chunkSize)
        {
            length = chunkSize;
        }
        chunk.assign(_data, _offset, length);
        _offset += length;
        return true;
    }

private:
    const std::string &_data;
    size_t _offset;
};

class FileSource : public ChunkSource
{
public:
    explicit FileSource(const std::string &filename) : _stream(filename.c_str(), std::ios::in | std::ios::binary)
    {
    }

    bool valid() const { return _stream.is_open(); }

    virtual bool next(std::string &chunk)
    {
        const size_t chunkSize = 1024 * 1024;
        chunk.resize(chunkSize);
        _stream.read(&chunk[0], static_cast<std::streamsize>(chunkSize));
        std::streamsize read = _stream.gcount();
        chunk.resize(read > 0 ? static_cast<size_t>(read) : 0);
        return read > 0;
    }

private:
    std::ifstream _stream;
};

} // namespace

/* ------------------------------------------------------------------ */
/* the posix implementation                                           */
/* ------------------------------------------------------------------ */

class PosixPrinterManager : public PrinterManager
{
public:
    virtual std::string getPrinters(std::vector<PrinterInfo> &printers);
    virtual std::string getDefaultPrinterName(PrinterName &printerName);
    virtual std::string getOnePrinter(const PrinterName &name, PrinterInfo &printer);
    virtual std::string getPrinterDevMode(const PrinterName &name, PrinterDevMode &devMode);
    virtual std::string getPrinterDriverOptions(const PrinterName &name,
                                                std::map<std::string, std::map<std::string, bool> > &options);
    virtual std::string getJobs(const PrinterName &name, bool includeCompleted,
                                std::vector<JobInfo> &jobs);
    virtual std::string getOneJob(const PrinterName &name, int jobId, JobInfo &job);
    virtual std::string setJob(const PrinterName &name, int jobId,
                               const std::string &command, bool &done);
    virtual std::string printDirect(const PrinterName &name, const std::string &docName,
                                    const std::string &type, const std::string &data,
                                    int &jobId);
    virtual std::string printFile(const PrinterName &name, const std::string &docName,
                                  const std::string &type, const std::string &filename,
                                  int &jobId);
    virtual std::vector<std::string> getSupportedPrintFormats();
    virtual std::vector<std::string> getSupportedJobCommands();

private:
    std::string sendDocument(const PrinterName &name, const std::string &docName,
                             const std::string &type, ChunkSource &source, int &jobId);
    void fillPrinter(cups_dest_t *dest, PrinterInfo &printer);
};

void PosixPrinterManager::fillPrinter(cups_dest_t *dest, PrinterInfo &printer)
{
    fillPrinterFromDest(dest, printer);

    std::string defaultQueue = systemDefaultQueue();
    if (!defaultQueue.empty())
    {
        printer.isDefault = (printer.name == defaultQueue);
    }

    std::string error;
    IppClient client;
    if (!client.connected())
    {
        return; // keep what the destination gave us
    }

    ipp_t *request = ippNewRequest(IPP_OP_GET_PRINTER_ATTRIBUTES);
    std::string uri = printerUri(printer.name);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, uri.c_str());
    const char *requested[] = {"printer-name", "printer-info", "printer-location",
                               "printer-make-and-model", "printer-state",
                               "printer-state-reasons", "printer-is-shared",
                               "printer-is-accepting-jobs", "printer-type",
                               "printer-uri-supported", "device-uri"};
    ippAddStrings(request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD, "requested-attributes",
                  static_cast<int>(sizeof(requested) / sizeof(requested[0])), NULL, requested);

    std::string resource = "/printers/" + printer.name;
    ipp_t *response = client.send(request, resource, error);
    if (response == NULL)
    {
        return;
    }
    IppMessage message(response);
    fillPrinterFromIpp(response, printer);
}

std::string PosixPrinterManager::getPrinters(std::vector<PrinterInfo> &printers)
{
    DestList list;
    list.count = cupsGetDests2(CUPS_HTTP_DEFAULT, &list.dests);
    if (list.dests == NULL)
    {
        std::string message = cupsLastErrorString();
        return message.empty() ? std::string("cannot list printers: unknown CUPS error") : message;
    }

    std::string defaultQueue = systemDefaultQueue();
    for (int i = 0; i < list.count; ++i)
    {
        PrinterInfo printer;
        fillPrinterFromDest(&list.dests[i], printer);
        if (!defaultQueue.empty())
        {
            printer.isDefault = (printer.name == defaultQueue);
        }
        printers.push_back(printer);
    }
    return std::string();
}

std::string PosixPrinterManager::getDefaultPrinterName(PrinterName &printerName)
{
    const char *name = cupsGetDefault2(CUPS_HTTP_DEFAULT);
    if (name == NULL)
    {
        ipp_status_t status = cupsLastError();
        // "no default printer" is not an error, every other failure is
        if (status == IPP_STATUS_ERROR_NOT_FOUND || status == IPP_STATUS_OK)
        {
            printerName.clear();
            return std::string();
        }
        std::string message = cupsLastErrorString();
        return message.empty() ? std::string("cannot read the default printer") : message;
    }
    printerName = name;
    return std::string();
}

std::string PosixPrinterManager::getOnePrinter(const PrinterName &name, PrinterInfo &printer)
{
    if (name.empty())
    {
        return "printer name required";
    }

    std::string queue = queueName(name);
    std::string instance = instanceName(name);

    NamedDest dest(cupsGetNamedDest(CUPS_HTTP_DEFAULT, queue.c_str(),
                                    instance.empty() ? NULL : instance.c_str()));
    if (dest.dest == NULL)
    {
        return "printer not found: " + name + " (" + cupsLastErrorString() + ")";
    }

    fillPrinter(dest.dest, printer);
    if (printer.name.empty())
    {
        printer.name = queue;
    }
    if (!instance.empty())
    {
        printer.instance = instance;
    }
    return std::string();
}

std::string PosixPrinterManager::getPrinterDevMode(const PrinterName &name, PrinterDevMode &devMode)
{
    if (name.empty())
    {
        return "printer name required";
    }

    std::string queue = queueName(name);
    NamedDest dest(cupsGetNamedDest(CUPS_HTTP_DEFAULT, queue.c_str(),
                                    instanceName(name).empty() ? NULL : instanceName(name).c_str()));
    if (dest.dest == NULL)
    {
        return "printer not found: " + name + " (" + cupsLastErrorString() + ")";
    }

    devMode.deviceName = queue;
    bool anything = false;

    /* 1) IPP defaults: available even for raw queues without a PPD */
    IppClient client;
    std::string error;
    if (client.connected())
    {
        ipp_t *request = ippNewRequest(IPP_OP_GET_PRINTER_ATTRIBUTES);
        std::string uri = printerUri(queue);
        ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, uri.c_str());
        const char *requested[] = {"media-default", "media-ready", "media-supported",
                                   "media-type-default", "media-source-default",
                                   "print-color-mode-default", "print-quality-default",
                                   "sides-default", "orientation-requested-default",
                                   "copies-default", "printer-resolution-default"};
        ippAddStrings(request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD, "requested-attributes",
                      static_cast<int>(sizeof(requested) / sizeof(requested[0])), NULL, requested);

        ipp_t *response = client.send(request, "/printers/" + queue, error);
        if (response != NULL)
        {
            IppMessage message(response);
            for (ipp_attribute_t *attribute = ippFirstAttribute(response);
                 attribute != NULL;
                 attribute = ippNextAttribute(response))
            {
                if (ippGetGroupTag(attribute) != IPP_TAG_PRINTER)
                {
                    continue;
                }
                std::string attributeName = ippGetName(attribute) != NULL ? ippGetName(attribute) : "";
                if (attributeName == "media-default" || attributeName == "media-ready")
                {
                    std::string media = jobAttributeString(attribute);
                    if (!devMode.paperSize.empty() && attributeName == "media-ready")
                    {
                        continue; // an explicit default wins
                    }
                    if (!media.empty())
                    {
                        devMode.paperSize = paperSizeName(media);
                        devMode.paperSizeCode = 0;
                        anything = true;
                    }
                }
                else if (attributeName == "media-type-default")
                {
                    devMode.mediaType = jobAttributeString(attribute);
                    anything = true;
                }
                else if (attributeName == "media-source-default")
                {
                    std::string source = jobAttributeString(attribute);
                    devMode.defaultSource = mediaSourceName(source);
                    anything = true;
                }
                else if (attributeName == "print-color-mode-default")
                {
                    std::string mode = toLowerAscii(jobAttributeString(attribute));
                    devMode.color = (mode == "monochrome" || mode == "mono") ? MONOCHROME : COLOR;
                    anything = true;
                }
                else if (attributeName == "print-quality-default")
                {
                    devMode.printQuality = printQualityFromIpp(ippGetInteger(attribute, 0));
                    anything = true;
                }
                else if (attributeName == "sides-default")
                {
                    std::string sides = jobAttributeString(attribute);
                    if (sides == "one-sided")
                    {
                        devMode.duplex = SIMPLEX;
                    }
                    else if (sides == "two-sided-long-edge")
                    {
                        devMode.duplex = VERTICAL;
                    }
                    else if (sides == "two-sided-short-edge")
                    {
                        devMode.duplex = HORIZONTAL;
                    }
                    anything = true;
                }
                else if (attributeName == "orientation-requested-default")
                {
                    devMode.orientation = ippGetInteger(attribute, 0) == 4 ? LANDSCAPE : PORTRAIT;
                    anything = true;
                }
                else if (attributeName == "copies-default")
                {
                    devMode.copies = ippGetInteger(attribute, 0);
                    anything = true;
                }
                else if (attributeName == "printer-resolution-default")
                {
                    int y = 0;
                    ipp_res_t units = IPP_RES_PER_INCH;
                    int x = ippGetResolution(attribute, 0, &y, &units);
                    if (x > 0)
                    {
                        devMode.resolutionX = x;
                        devMode.resolutionY = y;
                        anything = true;
                    }
                }
            }
        }
    }

    /* 2) PPD defaults: anything IPP did not provide */
    PpdFile ppd;
    const char *ppdPath = cupsGetPPD2(CUPS_HTTP_DEFAULT, queue.c_str());
    if (ppdPath != NULL)
    {
        ppd.filename = ppdPath;
        ppd.value = ppdOpenFile(ppdPath);
    }

    if (ppd.value != NULL)
    {
        ppdMarkDefaults(ppd.value);
        cupsMarkOptions(ppd.value, dest.dest->num_options, dest.dest->options);

        ppd_option_t *option = ppdFindOption(ppd.value, "PageSize");
        if (option != NULL && devMode.paperSize.empty())
        {
            devMode.paperSize = paperSizeName(option->defchoice);
            anything = true;
        }
        option = ppdFindOption(ppd.value, "InputSlot");
        if (option != NULL && devMode.defaultSource.empty())
        {
            devMode.defaultSource = mediaSourceName(option->defchoice);
            anything = true;
        }
        option = ppdFindOption(ppd.value, "MediaType");
        if (option != NULL && devMode.mediaType.empty())
        {
            devMode.mediaType = option->defchoice;
            anything = true;
        }
        option = ppdFindOption(ppd.value, "Orientation");
        if (option != NULL)
        {
            devMode.orientation = (strcmp(option->defchoice, "4") == 0) ? LANDSCAPE : PORTRAIT;
            anything = true;
        }
        option = ppdFindOption(ppd.value, "Duplex");
        if (option != NULL && devMode.duplex == SIMPLEX)
        {
            if (strcmp(option->defchoice, "DuplexNoTumble") == 0)
            {
                devMode.duplex = VERTICAL;
            }
            else if (strcmp(option->defchoice, "DuplexTumble") == 0)
            {
                devMode.duplex = HORIZONTAL;
            }
            anything = true;
        }
        option = ppdFindOption(ppd.value, "ColorModel");
        if (option != NULL)
        {
            devMode.color = (strcmp(option->defchoice, "Gray") == 0 ||
                             strcmp(option->defchoice, "Black") == 0)
                                ? MONOCHROME
                                : COLOR;
            anything = true;
        }
        option = ppdFindOption(ppd.value, "cupsPrintQuality");
        if (option == NULL)
        {
            option = ppdFindOption(ppd.value, "OutputMode");
        }
        if (option != NULL)
        {
            devMode.printQuality = printQualityFromKeyword(option->defchoice);
            anything = true;
        }
        option = ppdFindOption(ppd.value, "Resolution");
        if (option != NULL && devMode.resolutionX == 0)
        {
            const char *choice = option->defchoice;
            int x = 0;
            int y = 0;
            if (sscanf(choice, "%dx%d", &x, &y) == 2)
            {
                devMode.resolutionX = x;
                devMode.resolutionY = y;
                anything = true;
            }
            else if (sscanf(choice, "%d", &x) == 1)
            {
                devMode.resolutionX = x;
                devMode.resolutionY = x;
                anything = true;
            }
        }
    }

    if (!anything)
    {
        return "printer has no default configuration (no PPD and no IPP defaults): " + queue;
    }
    return std::string();
}

std::string PosixPrinterManager::getPrinterDriverOptions(const PrinterName &name,
                                                         std::map<std::string, std::map<std::string, bool> > &options)
{
    if (name.empty())
    {
        return "printer name required";
    }

    std::string queue = queueName(name);
    NamedDest dest(cupsGetNamedDest(CUPS_HTTP_DEFAULT, queue.c_str(),
                                    instanceName(name).empty() ? NULL : instanceName(name).c_str()));
    if (dest.dest == NULL)
    {
        return "printer not found: " + name + " (" + cupsLastErrorString() + ")";
    }

    PpdFile ppd;
    const char *ppdPath = cupsGetPPD2(CUPS_HTTP_DEFAULT, queue.c_str());
    if (ppdPath == NULL)
    {
        return std::string(); // raw queue: no driver options, but no error either
    }
    ppd.filename = ppdPath;
    ppd.value = ppdOpenFile(ppdPath);
    if (ppd.value == NULL)
    {
        return "cannot parse the PPD of printer " + queue;
    }

    ppdMarkDefaults(ppd.value);
    cupsMarkOptions(ppd.value, dest.dest->num_options, dest.dest->options);

    int groupCount = ppd.value->num_groups;
    ppd_group_t *groups = ppd.value->groups;
    for (int i = 0; i < groupCount; ++i)
    {
        int optionCount = groups[i].num_options;
        ppd_option_t *groupOptions = groups[i].options;
        for (int j = 0; j < optionCount; ++j)
        {
            std::map<std::string, bool> choices;
            for (int k = 0; k < groupOptions[j].num_choices; ++k)
            {
                choices[groupOptions[j].choices[k].choice] =
                    groupOptions[j].choices[k].marked != 0;
            }
            options[groupOptions[j].keyword] = choices;
        }
    }
    return std::string();
}

std::string PosixPrinterManager::getJobs(const PrinterName &name, bool includeCompleted,
                                         std::vector<JobInfo> &jobs)
{
    IppClient client;
    if (!client.connected())
    {
        return client.connectionError();
    }

    ipp_t *request = ippNewRequest(IPP_OP_GET_JOBS);
    std::string resource = "/";
    if (!name.empty())
    {
        std::string queue = queueName(name);
        resource = "/printers/" + queue;
        std::string uri = printerUri(queue);
        ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, uri.c_str());
    }
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD, "which-jobs", NULL,
                 includeCompleted ? "all" : "not-completed");
    ippAddBoolean(request, IPP_TAG_OPERATION, "my-jobs", 0);
    const char *requested[] = {"job-id", "job-name", "job-originating-user-name",
                               "document-format", "job-printer-uri", "job-priority",
                               "job-size", "job-k-octets", "job-state",
                               "job-state-reasons", "job-media-sheets-completed",
                               "job-impressions-completed", "time-at-creation",
                               "time-at-completed", "time-at-processing"};
    ippAddStrings(request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD, "requested-attributes",
                  static_cast<int>(sizeof(requested) / sizeof(requested[0])), NULL, requested);

    std::string error;
    ipp_t *response = client.send(request, resource, error);
    if (response == NULL)
    {
        return error;
    }
    IppMessage message(response);
    parseJobs(response, jobs);
    return std::string();
}

std::string PosixPrinterManager::getOneJob(const PrinterName &name, int jobId, JobInfo &job)
{
    if (jobId <= 0)
    {
        return "wrong job id";
    }

    IppClient client;
    if (!client.connected())
    {
        return client.connectionError();
    }

    ipp_t *request = ippNewRequest(IPP_OP_GET_JOB_ATTRIBUTES);
    std::string uri = jobUri(jobId);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "job-uri", NULL, uri.c_str());
    const char *requested[] = {"job-id", "job-name", "job-originating-user-name",
                               "document-format", "job-printer-uri", "job-priority",
                               "job-size", "job-k-octets", "job-state",
                               "job-state-reasons", "job-impressions-completed",
                               "time-at-creation", "time-at-completed",
                               "time-at-processing"};
    ippAddStrings(request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD, "requested-attributes",
                  static_cast<int>(sizeof(requested) / sizeof(requested[0])), NULL, requested);

    std::string error;
    ipp_t *response = client.send(request, "/jobs/" + numberToString(jobId), error);
    if (response == NULL)
    {
        return error;
    }
    IppMessage message(response);

    std::vector<JobInfo> jobs;
    parseJobs(response, jobs);
    if (jobs.empty())
    {
        return "job not found: " + numberToString(jobId);
    }
    job = jobs[0];
    if (job.printerName.empty() && !name.empty())
    {
        job.printerName = queueName(name);
    }
    return std::string();
}

std::string PosixPrinterManager::setJob(const PrinterName &name, int jobId,
                                        const std::string &command, bool &done)
{
    done = false;
    if (jobId <= 0)
    {
        return "wrong job id";
    }

    std::string action = normaliseJobCommand(command);
    ipp_op_t operation;
    bool purge = false;

    if (action == "CANCEL")
    {
        operation = IPP_OP_CANCEL_JOB;
    }
    else if (action == "DELETE")
    {
        operation = IPP_OP_CANCEL_JOB;
        purge = true; // CUPS: drop the job from the queue history
    }
    else if (action == "PAUSE" || action == "HOLD" || action == "RETAIN")
    {
        operation = IPP_OP_HOLD_JOB;
    }
    else if (action == "RESUME" || action == "RELEASE")
    {
        operation = IPP_OP_RELEASE_JOB;
    }
    else if (action == "RESTART")
    {
        operation = IPP_OP_RESTART_JOB;
    }
    else
    {
        std::string supported;
        std::vector<std::string> commands = getSupportedJobCommands();
        for (size_t i = 0; i < commands.size(); ++i)
        {
            if (!supported.empty())
            {
                supported += ", ";
            }
            supported += commands[i];
        }
        return "wrong job command '" + command + "'. supported commands: " + supported;
    }

    IppClient client;
    if (!client.connected())
    {
        return client.connectionError();
    }

    ipp_t *request = ippNewRequest(operation);
    std::string uri = jobUri(jobId);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "job-uri", NULL, uri.c_str());
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", NULL,
                 cupsUser());
    if (purge)
    {
        ippAddBoolean(request, IPP_TAG_OPERATION, "purge-job", 1);
    }

    std::string error;
    ipp_t *response = client.send(request, "/jobs/" + numberToString(jobId), error);
    if (response == NULL)
    {
        return error;
    }
    IppMessage message(response);
    done = true;
    return std::string();
}

std::string PosixPrinterManager::sendDocument(const PrinterName &name, const std::string &docName,
                                              const std::string &type, ChunkSource &source,
                                              int &jobId)
{
    jobId = 0;
    if (name.empty())
    {
        return "printer name required";
    }

    std::string mime = formatToMime(type);
    if (mime.empty())
    {
        std::string supported;
        std::vector<std::string> formats = getSupportedPrintFormats();
        for (size_t i = 0; i < formats.size(); ++i)
        {
            if (!supported.empty())
            {
                supported += ", ";
            }
            supported += formats[i];
        }
        return "unsupported data type '" + type + "'. supported types: " + supported;
    }

    std::string queue = queueName(name);
    std::string title = docName.empty() ? std::string("node-printing job") : docName;

    int id = cupsCreateJob(CUPS_HTTP_DEFAULT, queue.c_str(), title.c_str(), 0, NULL);
    if (id == 0)
    {
        std::string message = cupsLastErrorString();
        return "cannot create the print job on '" + queue + "': " +
               (message.empty() ? std::string("unknown CUPS error") : message);
    }

    if (cupsStartDocument(CUPS_HTTP_DEFAULT, queue.c_str(), id, title.c_str(), mime.c_str(), 1) !=
        HTTP_CONTINUE)
    {
        std::string message = cupsLastErrorString();
        cupsCancelJob2(CUPS_HTTP_DEFAULT, queue.c_str(), id, 1);
        return "cannot start the document: " + (message.empty() ? std::string("unknown CUPS error") : message);
    }

    std::string chunk;
    while (source.next(chunk))
    {
        if (chunk.empty())
        {
            continue;
        }
        if (cupsWriteRequestData(CUPS_HTTP_DEFAULT, chunk.data(), chunk.size()) != HTTP_CONTINUE)
        {
            std::string message = cupsLastErrorString();
            cupsFinishDocument(CUPS_HTTP_DEFAULT, queue.c_str());
            cupsCancelJob2(CUPS_HTTP_DEFAULT, queue.c_str(), id, 1);
            return "cannot send the document data: " +
                   (message.empty() ? std::string("unknown CUPS error") : message);
        }
    }

    ipp_status_t status = cupsFinishDocument(CUPS_HTTP_DEFAULT, queue.c_str());
    if (status != IPP_STATUS_OK)
    {
        std::string message = cupsLastErrorString();
        cupsCancelJob2(CUPS_HTTP_DEFAULT, queue.c_str(), id, 1);
        return "cannot finish the document: " + (message.empty() ? std::string("unknown CUPS error") : message);
    }

    jobId = id;
    return std::string();
}

std::string PosixPrinterManager::printDirect(const PrinterName &name, const std::string &docName,
                                             const std::string &type, const std::string &data,
                                             int &jobId)
{
    if (data.empty())
    {
        jobId = 0;
        return "empty document";
    }
    BufferSource source(data);
    return sendDocument(name, docName, type, source, jobId);
}

std::string PosixPrinterManager::printFile(const PrinterName &name, const std::string &docName,
                                           const std::string &type, const std::string &filename,
                                           int &jobId)
{
    if (filename.empty())
    {
        jobId = 0;
        return "file name required";
    }

    FileSource source(filename);
    if (!source.valid())
    {
        jobId = 0;
        return "cannot read the file: " + filename;
    }
    return sendDocument(name, docName, type, source, jobId);
}

std::vector<std::string> PosixPrinterManager::getSupportedPrintFormats()
{
    std::vector<std::string> formats;
    formats.push_back("RAW");
    formats.push_back("TEXT");
    formats.push_back("PDF");
    formats.push_back("POSTSCRIPT");
    formats.push_back("JPEG");
    formats.push_back("PNG");
    formats.push_back("COMMAND");
    formats.push_back("AUTO");
    return formats;
}

/**
 * CUPS has no separate "pause a job" and "hold a job" operation: PAUSE maps to
 * Hold-Job and RESUME to Release-Job, so HOLD/RETAIN and RELEASE are accepted
 * as aliases with the same meaning.
 */
std::vector<std::string> PosixPrinterManager::getSupportedJobCommands()
{
    std::vector<std::string> commands;
    commands.push_back("CANCEL");
    commands.push_back("DELETE");
    commands.push_back("PAUSE");
    commands.push_back("RESUME");
    commands.push_back("RESTART");
    commands.push_back("HOLD");
    commands.push_back("RELEASE");
    return commands;
}

PrinterManager *createPrinterManager()
{
    return new PosixPrinterManager();
}

#endif // !_WIN32
