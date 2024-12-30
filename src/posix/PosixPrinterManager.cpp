#include "../PrinterManager.hpp"
#include <cups/cups.h>
#include <cups/ppd.h>
#include <stdexcept>
#include <sstream>

// Helper function to convert const char* to std::wstring
std::wstring charToWString(const char *str)
{
    std::string s(str);
    return std::wstring(s.begin(), s.end());
}

ErrorMessage *PrinterManager::getDefaultPrinterName(PrinterName &printerName)
{
    char defaultPrinter[1024];
    if (cupsGetDefault2(CUPS_HTTP_DEFAULT, defaultPrinter, sizeof(defaultPrinter)) == NULL)
    {
        static ErrorMessage errorMsg = "Error could not get default printer name";
        return &errorMsg;
    }

    printerName = charToWString(defaultPrinter);
    return NULL;
}

ErrorMessage *PrinterManager::getOneJob(PrinterName name, int jobId, JobInfo &jobInfo)
{
    cups_job_t *jobs;
    int num_jobs = cupsGetJobs(&jobs, name.c_str(), 0, CUPS_WHICHJOBS_ALL);

    if (num_jobs == 0)
    {
        static ErrorMessage errorMsg = "No jobs found for the printer";
        return &errorMsg;
    }

    for (int i = 0; i < num_jobs; ++i)
    {
        if (jobs[i].id == jobId)
        {
            jobInfo.id = jobs[i].id;
            jobInfo.name = jobs[i].title;
            jobInfo.user = jobs[i].user;
            jobInfo.priority = jobs[i].priority;
            jobInfo.size = jobs[i].size;
            jobInfo.status = jobs[i].state == IPP_JOB_COMPLETED ? "COMPLETED" : "PENDING";
            jobInfo.position = jobs[i].priority;
            jobInfo.totalPages = jobs[i].size;
            jobInfo.pagesPrinted = jobs[i].size - jobs[i].size_left;
            break;
        }
    }

    cupsFreeJobs(num_jobs, jobs);
    return NULL;
}

ErrorMessage *PrinterManager::getOnePrinter(PrinterName name, PrinterInfo &printerInfo)
{
    cups_dest_t *dests;
    int num_dests = cupsGetDests(&dests);

    if (num_dests == 0)
    {
        static ErrorMessage errorMsg = "No printers found";
        return &errorMsg;
    }

    for (int i = 0; i < num_dests; ++i)
    {
        if (dests[i].name == name)
        {
            printerInfo.name = dests[i].name;
            printerInfo.server = cupsServer();
            printerInfo.shareName = dests[i].name;
            printerInfo.portName = dests[i].name;
            printerInfo.driverName = dests[i].instance;
            printerInfo.location = cupsGetOption("printer-location", dests[i].num_options, dests[i].options);
            printerInfo.comment = cupsGetOption("printer-info", dests[i].num_options, dests[i].options);
            printerInfo.status = dests[i].is_default ? 1 : 0;
            printerInfo.attributes = dests[i].num_options;
            printerInfo.averagePPM = 0; // Not available in CUPS
            printerInfo.cJobs = 0;      // Not available in CUPS
            printerInfo.defaultPriority = 0;
            printerInfo.startTime = 0;
            printerInfo.untilTime = 0;
            break;
        }
    }

    cupsFreeDests(num_dests, dests);
    return NULL;
}

ErrorMessage *PrinterManager::getPrinters(std::vector<PrinterInfo> &printersInfo)
{
    cups_dest_t *dests;
    int num_dests = cupsGetDests(&dests);

    if (num_dests == 0)
    {
        static ErrorMessage errorMsg = "No printers found";
        return &errorMsg;
    }

    for (int i = 0; i < num_dests; ++i)
    {
        PrinterInfo printerInfo;
        printerInfo.name = dests[i].name;
        printerInfo.server = cupsServer();
        printerInfo.shareName = dests[i].name;
        printerInfo.portName = dests[i].name;
        printerInfo.driverName = dests[i].instance;
        printerInfo.location = cupsGetOption("printer-location", dests[i].num_options, dests[i].options);
        printerInfo.comment = cupsGetOption("printer-info", dests[i].num_options, dests[i].options);
        printerInfo.status = dests[i].is_default ? 1 : 0;
        printerInfo.attributes = dests[i].num_options;
        printerInfo.averagePPM = 0; // Not available in CUPS
        printerInfo.cJobs = 0;      // Not available in CUPS
        printerInfo.defaultPriority = 0;
        printerInfo.startTime = 0;
        printerInfo.untilTime = 0;

        printersInfo.push_back(printerInfo);
    }

    cupsFreeDests(num_dests, dests);
    return NULL;
}

ErrorMessage *PrinterManager::printDirect(PrinterName name, std::string docName, std::string type, std::string data, int &jobId)
{
    const char *options[] = {"document-format", type.c_str()};
    jobId = cupsPrintFile(name.c_str(), docName.c_str(), docName.c_str(), 1, options);

    if (jobId == 0)
    {
        static ErrorMessage errorMsg = "Failed to print document";
        return &errorMsg;
    }

    return NULL;
}

ErrorMessage *PrinterManager::getSupportedPrintFormats(std::vector<std::string> &dataTypes)
{
    // CUPS does not provide a direct way to get supported print formats
    // This is a placeholder implementation
    dataTypes.push_back("application/pdf");
    dataTypes.push_back("application/postscript");
    dataTypes.push_back("application/vnd.cups-raw");
    dataTypes.push_back("image/jpeg");
    dataTypes.push_back("image/png");

    return NULL;
}

ErrorMessage *PrinterManager::getPrinterDevMode(const std::wstring &printerName, PrinterDevMode &pDevMode)
{
    ppd_file_t *ppd = ppdOpenFile(cupsGetPPD(printerName.c_str()));
    if (ppd == NULL)
    {
        static ErrorMessage errorMsg = "Failed to open PPD file";
        return &errorMsg;
    }

    ppdMarkDefaults(ppd);
    ppd_option_t *option = ppdFindOption(ppd, "PageSize");
    if (option != NULL)
    {
        for (int i = 0; i < option->num_choices; i++)
        {
            if (option->choices[i].marked)
            {
                pDevMode.paperSize = option->choices[i].text;
                break;
            }
        }
    }

    option = ppdFindOption(ppd, "Orientation");
    if (option != NULL)
    {
        for (int i = 0; i < option->num_choices; i++)
        {
            if (option->choices[i].marked)
            {
                pDevMode.orientation = (option->choices[i].choice == std::string("Portrait")) ? Orientation::PORTRAIT : Orientation::LANDSCAPE;
                break;
            }
        }
    }

    option = ppdFindOption(ppd, "Duplex");
    if (option != NULL)
    {
        for (int i = 0; i < option->num_choices; i++)
        {
            if (option->choices[i].marked)
            {
                if (option->choices[i].choice == std::string("None"))
                {
                    pDevMode.duplex = Duplex::SIMPLEX;
                }
                else if (option->choices[i].choice == std::string("DuplexNoTumble"))
                {
                    pDevMode.duplex = Duplex::VERTICAL;
                }
                else if (option->choices[i].choice == std::string("DuplexTumble"))
                {
                    pDevMode.duplex = Duplex::HORIZONTAL;
                }
                break;
            }
        }
    }

    option = ppdFindOption(ppd, "ColorModel");
    if (option != NULL)
    {
        for (int i = 0; i < option->num_choices; i++)
        {
            if (option->choices[i].marked)
            {
                pDevMode.color = (option->choices[i].choice == std::string("Gray")) ? Color::MONOCHROME : Color::COLOR;
                break;
            }
        }
    }

    option = ppdFindOption(ppd, "Copies");
    if (option != NULL)
    {
        for (int i = 0; i < option->num_choices; i++)
        {
            if (option->choices[i].marked)
            {
                pDevMode.copies = std::stoi(option->choices[i].choice);
                break;
            }
        }
    }

    option = ppdFindOption(ppd, "InputSlot");
    if (option != NULL)
    {
        for (int i = 0; i < option->num_choices; i++)
        {
            if (option->choices[i].marked)
            {
                pDevMode.defaultSource = option->choices[i].text;
                break;
            }
        }
    }

    option = ppdFindOption(ppd, "Resolution");
    if (option != NULL)
    {
        for (int i = 0; i < option->num_choices; i++)
        {
            if (option->choices[i].marked)
            {
                if (option->choices[i].choice == std::string("Draft"))
                {
                    pDevMode.printQuality = PrintQuality::DRAFT;
                }
                else if (option->choices[i].choice == std::string("Normal"))
                {
                    pDevMode.printQuality = PrintQuality::MEDIUM;
                }
                else if (option->choices[i].choice == std::string("High"))
                {
                    pDevMode.printQuality = PrintQuality::HIGH;
                }
                break;
            }
        }
    }

    option = ppdFindOption(ppd, "Collate");
    if (option != NULL)
    {
        for (int i = 0; i < option->num_choices; i++)
        {
            if (option->choices[i].marked)
            {
                pDevMode.collate = (option->choices[i].choice == std::string("True"));
                break;
            }
        }
    }

    ppdClose(ppd);
    return NULL;
}
