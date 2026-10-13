#ifndef PRINTER_MANAGER_HPP
#define PRINTER_MANAGER_HPP

#include <map>
#include <string>
#include <vector>

/**
 * Cross-platform printer interface, implemented once per platform:
 *
 *   src/win/WinPrinterManager.cpp      Windows  (Winspool / GDI / DEVMODE)
 *   src/posix/PosixPrinterManager.cpp  Linux & macOS (CUPS + PPD)
 *
 * Only one of the two is compiled; binding.gyp selects it by OS and both
 * provide createPrinterManager(). The N-API layer (src/node_printer.cpp) only
 * ever talks to PrinterManager, so it stays platform independent.
 *
 * Conventions every implementation must follow:
 *   - every std::string is UTF-8; the Windows implementation converts to and
 *     from UTF-16 internally (CP_UTF8), CUPS is UTF-8 natively
 *   - a method returns an empty string on success, or a human readable error
 *     message on failure; exceptions never cross this interface
 *   - out parameters are only meaningful when the returned error is empty
 *   - no method may abort the process or leak memory on an error path
 *   - job commands are normalised with normaliseJobCommand() before dispatch,
 *     so callers may pass any case or surrounding whitespace
 */

typedef std::string PrinterName; // UTF-8 printer queue name

enum Orientation
{
    PORTRAIT = 0,
    LANDSCAPE
};

enum Duplex
{
    SIMPLEX = 0,
    VERTICAL,  // long edge binding
    HORIZONTAL // short edge binding
};

enum Color
{
    MONOCHROME = 0,
    COLOR
};

enum PrintQuality
{
    DRAFT = 0,
    LOW,
    MEDIUM,
    HIGH
};

/** A print job, normalised across platforms. */
struct JobInfo
{
    int id = 0;
    std::string name;        // document title
    std::string printerName; // queue the job belongs to
    std::string user;        // submitting user, as reported by the spooler
    std::string format;      // RAW, TEXT, PDF, POSTSCRIPT, ... or the raw MIME type
    std::string state;       // pending|processing|held|stopped|canceled|aborted|completed|unknown
    std::string status;      // human readable summary, e.g. "PRINTING"
    std::vector<std::string> statusArray; // canonical reasons, e.g. {"PRINTING","PENDING"}
    int priority = 0;
    double size = 0;         // document size in bytes
    int position = 0;        // 1 based position in the queue, 0 when unknown
    int totalPages = 0;      // pages, 0 when unknown
    int pagesPrinted = 0;    // pages already printed, 0 when unknown
    double submittedTime = -1;  // milliseconds since epoch, -1 when unknown
    double completedTime = -1;  // milliseconds since epoch, -1 when unknown
    double processingTime = -1; // milliseconds since epoch, -1 when unknown
};

/** A printer queue, normalised across platforms. */
struct PrinterInfo
{
    std::string name;
    std::string instance;    // CUPS instance, empty on Windows
    std::string server;      // print server / host, empty for local queues
    std::string shareName;
    std::string portName;
    std::string driverName;  // driver name (Windows), make and model (CUPS)
    std::string location;
    std::string comment;
    std::string uri;         // device URI (CUPS), port based URI on Windows
    std::string state;       // idle|processing|stopped|unknown
    bool isDefault = false;
    bool isShared = false;
    bool isLocal = true;
    bool acceptsJobs = true;
    int status = 0;          // raw platform status code
    std::vector<std::string> statusArray;    // canonical status names
    int attributes = 0;      // raw platform attribute bitmask (Windows), 0 on posix
    std::vector<std::string> attributeArray; // canonical attribute names (Windows)
    int averagePPM = 0;      // pages per minute, 0 when unknown (Windows)
    int cJobs = 0;           // jobs currently queued for this printer
    int defaultPriority = 0;
    int startTime = 0;
    int untilTime = 0;
    std::map<std::string, std::string> options; // raw platform options
};

/** Printer default configuration (Windows DEVMODE / CUPS PPD defaults). */
struct PrinterDevMode
{
    std::string deviceName;
    std::string paperSize;    // "A4", "LETTER", ... empty when unknown
    int paperSizeCode = 0;    // raw platform code (DMPAPER_* / media keyword)
    std::string mediaType;    // e.g. "Plain", "Glossy", empty when unknown
    Orientation orientation = PORTRAIT;
    Duplex duplex = SIMPLEX;
    Color color = MONOCHROME;
    PrintQuality printQuality = MEDIUM;
    int copies = 1;
    bool collate = false;
    int scale = 100;
    std::string defaultSource; // "Auto", "Tray 1", ... empty when unknown
    int defaultSourceCode = 0; // raw platform code (DMBIN_* / InputSlot keyword)
    int resolutionX = 0;       // dpi, 0 when unknown
    int resolutionY = 0;       // dpi, 0 when unknown
};

/* ------------------------------------------------------------------ */
/* shared header-only helpers                                         */
/* ------------------------------------------------------------------ */

/** Upper case ASCII copy, used to normalise job commands and formats. */
inline std::string toUpperAscii(const std::string &value)
{
    std::string result(value);
    for (size_t i = 0; i < result.size(); ++i)
    {
        if (result[i] >= 'a' && result[i] <= 'z')
        {
            result[i] = static_cast<char>(result[i] - 'a' + 'A');
        }
    }
    return result;
}

/** Trim ASCII whitespace and upper case: "  resume " -> "RESUME". */
inline std::string normaliseJobCommand(const std::string &value)
{
    size_t begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
    {
        return std::string();
    }
    size_t end = value.find_last_not_of(" \t\r\n");
    return toUpperAscii(value.substr(begin, end - begin + 1));
}

/** Canonical job commands; implementations support a documented subset. */
inline const std::vector<std::string> &allJobCommands()
{
    static const std::vector<std::string> commands = {
        "CANCEL", "PAUSE", "RESUME", "RESTART", "DELETE", "HOLD", "RELEASE",
        "RETAIN", "SENT-TO-PRINTER", "LAST-PAGE-EJECTED"};
    return commands;
}

inline const char *orientationToString(Orientation value)
{
    return value == LANDSCAPE ? "LANDSCAPE" : "PORTRAIT";
}

inline const char *duplexToString(Duplex value)
{
    switch (value)
    {
    case VERTICAL:
        return "VERTICAL";
    case HORIZONTAL:
        return "HORIZONTAL";
    default:
        return "SIMPLEX";
    }
}

inline const char *colorToString(Color value)
{
    return value == COLOR ? "COLOR" : "MONOCHROME";
}

inline const char *printQualityToString(PrintQuality value)
{
    switch (value)
    {
    case DRAFT:
        return "DRAFT";
    case LOW:
        return "LOW";
    case HIGH:
        return "HIGH";
    default:
        return "MEDIUM";
    }
}

/* ------------------------------------------------------------------ */
/* the platform interface                                             */
/* ------------------------------------------------------------------ */

class PrinterManager
{
public:
    virtual ~PrinterManager() {}

    /** Every print queue known to the spooler. */
    virtual std::string getPrinters(std::vector<PrinterInfo> &printers) = 0;

    /** Name of the default queue; empty when none is configured. */
    virtual std::string getDefaultPrinterName(PrinterName &printerName) = 0;

    /** One queue by name ("queue/instance" accepted on posix). */
    virtual std::string getOnePrinter(const PrinterName &name, PrinterInfo &printer) = 0;

    /** Default configuration of a queue (paper, duplex, colour, quality...). */
    virtual std::string getPrinterDevMode(const PrinterName &name, PrinterDevMode &devMode) = 0;

    /** Driver options: Windows printer data / CUPS PPD choices. Best effort. */
    virtual std::string getPrinterDriverOptions(const PrinterName &name,
                                                std::map<std::string, std::map<std::string, bool> > &options) = 0;

    /** Jobs of a queue; an empty printerName means every queue.
     *  includeCompleted adds finished and cancelled jobs. */
    virtual std::string getJobs(const PrinterName &name, bool includeCompleted,
                                std::vector<JobInfo> &jobs) = 0;

    /** One job of a queue by id. */
    virtual std::string getOneJob(const PrinterName &name, int jobId, JobInfo &job) = 0;

    /** Control a job: CANCEL, PAUSE, RESUME, RESTART, DELETE, HOLD, RELEASE...
     *  The command outcome is reported in `done`. */
    virtual std::string setJob(const PrinterName &name, int jobId,
                               const std::string &command, bool &done) = 0;

    /** Send a buffer/text to a queue; returns the spooler job id. */
    virtual std::string printDirect(const PrinterName &name, const std::string &docName,
                                    const std::string &type, const std::string &data,
                                    int &jobId) = 0;

    /** Send a file to a queue; returns the spooler job id. */
    virtual std::string printFile(const PrinterName &name, const std::string &docName,
                                  const std::string &type, const std::string &filename,
                                  int &jobId) = 0;

    /** Data types accepted by printDirect/printFile on this platform. */
    virtual std::vector<std::string> getSupportedPrintFormats() = 0;

    /** Job commands accepted by setJob on this platform. */
    virtual std::vector<std::string> getSupportedJobCommands() = 0;
};

/** Factory implemented by each platform file. Caller owns the pointer. */
PrinterManager *createPrinterManager();

#endif // PRINTER_MANAGER_HPP
