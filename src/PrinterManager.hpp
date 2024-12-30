#ifndef PRINTER_MANAGER_HPP
#define PRINTER_MANAGER_HPP

#include <string>
#include <vector>
#include <map>

typedef std::wstring PrinterName;
typedef std::string ErrorMessage;

enum Orientation
{
    PORTRAIT = 0,
    LANDSCAPE
};

enum Duplex
{
    SIMPLEX = 0,
    VERTICAL,
    HORIZONTAL
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

const std::map orientation_str = std::map<Orientation, std::string>{{Orientation::PORTRAIT, "PORTRAIT"}, {Orientation::LANDSCAPE, "LANDSCAPE"}};
const std::map duplex_str = std::map<Duplex, std::string>{{Duplex::SIMPLEX, "SIMPLEX"}, {Duplex::VERTICAL, "VERTICAL"}, {Duplex::HORIZONTAL, "HORIZONTAL"}};
const std::map color_str = std::map<Color, std::string>{{Color::MONOCHROME, "MONOCHROME"}, {Color::COLOR, "COLOR"}};
const std::map printQuality_str = std::map<PrintQuality, std::string>{{PrintQuality::DRAFT, "DRAFT"}, {PrintQuality::LOW, "LOW"}, {PrintQuality::MEDIUM, "MEDIUM"}, {PrintQuality::HIGH, "HIGH"}};

struct JobInfo
{
    int id;
    std::string name;
    std::string user;
    int priority;
    int size;
    std::string status;
    std::vector<std::string> statusArray;
    int position;
    int totalPages;
    int pagesPrinted;
};

struct PrinterInfo
{
    std::string name;
    std::string server;
    std::string shareName;
    std::string portName;
    std::string driverName;
    std::string location;
    std::string comment;
    std::vector<std::string> statusArray;
    int status;
    int attributes;
    std::vector<std::string> attributeArray;
    int averagePPM;
    int cJobs;
    int defaultPriority;
    int startTime;
    int untilTime;
};

struct PrinterDevMode
{
    PrinterName deviceName;
    std::string paperSize;
    Orientation orientation;
    int copies;
    std::string defaultSource;
    PrintQuality printQuality;
    int scale;
    bool collate;
    Color color;
    Duplex duplex;
};

template <typename Type>
class MemValue
{
public:
    MemValue(const int iSizeKbytes)
    {
        _value = (Type *)malloc(iSizeKbytes);
    }

    ~MemValue()
    {
        free();
    }

    Type *get() { return _value; }
    operator bool() { return (_value != NULL); }
    Type *operator->() { return _value; }

protected:
    Type *_value;
    void free()
    {
        if (_value != NULL)
        {
            ::free(_value);
            _value = NULL;
        }
    }
};

class PrinterManager
{
public:
    ErrorMessage *getDefaultPrinterName(PrinterName &printerName);
    ErrorMessage *getOneJob(PrinterName name, int jobId, JobInfo &jobInfo);
    ErrorMessage *getOnePrinter(PrinterName name, PrinterInfo &printerInfo);
    ErrorMessage *getPrinters(std::vector<PrinterInfo> &printersInfo);
    ErrorMessage *printDirect(PrinterName name, std::string docName, std::string type, std::string data, int &jobId);
    ErrorMessage *getSupportedPrintFormats(std::vector<std::string> &dataTypes);
    ErrorMessage *getPrinterDevMode(const std::wstring &printerName, PrinterDevMode &pDevMode);
};

#endif