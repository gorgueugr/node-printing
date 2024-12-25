#ifndef PRINTER_MANAGER_HPP
#define PRINTER_MANAGER_HPP

#include <string>
#include <vector>

typedef std::wstring PrinterName;
typedef std::string ErrorMessage;

struct JobInfo
{
    int id;
    std::string name;
    std::string user;
    int priority;
    int size;
    std::string status;
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
    virtual void free()
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
    PrinterName getDefaultPrinterName();
    ErrorMessage *getOneJob(PrinterName name, int jobId, JobInfo &jobInfo);
    ErrorMessage *getOnePrinter(PrinterName name, PrinterInfo &printerInfo);
    ErrorMessage *getPrinters(std::vector<PrinterInfo> &printersInfo);
};

#endif