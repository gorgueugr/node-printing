#include "../PrinterManager.hpp"

#include <windows.h>
#include <Winspool.h>
#include <Wingdi.h>
#pragma comment(lib, "Winspool.lib")

#include <string>
#include <map>
#include <utility>
#include <sstream>
#include <iostream>

struct PrinterHandle
{
    PrinterHandle(LPWSTR iPrinterName)
    {
        _ok = OpenPrinterW((LPWSTR)iPrinterName, &_printer, NULL);
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

std::string LPWSTRToString(const wchar_t *wstr)
{
    if (wstr == NULL)
    {
        return std::string("");
    }
    if (wstr[0] == L'\0')
    {
        return std::string("");
    }

    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &strTo[0], size_needed, NULL, NULL);

    return strTo;
}

// const StatusMapType &getJobCommandMap()
// {
//     static StatusMapType result;
//     if (!result.empty())
//     {
//         return result;
//     }
// #define COMMAND_JOB_ADD(value, type) result.insert(std::make_pair(value, type))
//     COMMAND_JOB_ADD("CANCEL", JOB_CONTROL_CANCEL);
//     COMMAND_JOB_ADD("PAUSE", JOB_CONTROL_PAUSE);
//     COMMAND_JOB_ADD("RESTART", JOB_CONTROL_RESTART);
//     COMMAND_JOB_ADD("RESUME", JOB_CONTROL_RESUME);
//     COMMAND_JOB_ADD("DELETE", JOB_CONTROL_DELETE);
//     COMMAND_JOB_ADD("SENT-TO-PRINTER", JOB_CONTROL_SENT_TO_PRINTER);
//     COMMAND_JOB_ADD("LAST-PAGE-EJECTED", JOB_CONTROL_LAST_PAGE_EJECTED);
// #ifdef JOB_CONTROL_RETAIN
//     COMMAND_JOB_ADD("RETAIN", JOB_CONTROL_RETAIN);
// #endif
// #ifdef JOB_CONTROL_RELEASE
//     COMMAND_JOB_ADD("RELEASE", JOB_CONTROL_RELEASE);
// #endif
// #undef COMMAND_JOB_ADD
//     return result;
// }

typedef std::map<std::string, DWORD> StatusMapType;
typedef std::map<SHORT, std::string> PaperSizeMapType;

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
    STATUS_PRINTER_ADD("NO-TObPrinterNameNER", PRINTER_STATUS_NO_TONER);
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

const StatusMapType &getAttributeMap()
{
    static StatusMapType result;
    if (!result.empty())
    {
        return result;
    }
    // add only first time
#define ATTRIBUTE_PRINTER_ADD(value, type) result.insert(std::make_pair(value, type))
    ATTRIBUTE_PRINTER_ADD("DIRECT", PRINTER_ATTRIBUTE_DIRECT);
    ATTRIBUTE_PRINTER_ADD("DO-COMPLETE-FIRST", PRINTER_ATTRIBUTE_DO_COMPLETE_FIRST);
    ATTRIBUTE_PRINTER_ADD("ENABLE-DEVQ", PRINTER_ATTRIBUTE_ENABLE_DEVQ);
    ATTRIBUTE_PRINTER_ADD("HIDDEN", PRINTER_ATTRIBUTE_HIDDEN);
    ATTRIBUTE_PRINTER_ADD("KEEPPRINTEDJOBS", PRINTER_ATTRIBUTE_KEEPPRINTEDJOBS);
    ATTRIBUTE_PRINTER_ADD("LOCAL", PRINTER_ATTRIBUTE_LOCAL);
    ATTRIBUTE_PRINTER_ADD("NETWORK", PRINTER_ATTRIBUTE_NETWORK);
    ATTRIBUTE_PRINTER_ADD("PUBLISHED", PRINTER_ATTRIBUTE_PUBLISHED);
    ATTRIBUTE_PRINTER_ADD("QUEUED", PRINTER_ATTRIBUTE_QUEUED);
    ATTRIBUTE_PRINTER_ADD("RAW-ONLY", PRINTER_ATTRIBUTE_RAW_ONLY);
    ATTRIBUTE_PRINTER_ADD("SHARED", PRINTER_ATTRIBUTE_SHARED);
    ATTRIBUTE_PRINTER_ADD("OFFLINE", PRINTER_ATTRIBUTE_WORK_OFFLINE);
    // XP
#ifdef PRINTER_ATTRIBUTE_FAX
    ATTRIBUTE_PRINTER_ADD("FAX", PRINTER_ATTRIBUTE_FAX);
#endif
    // vista
#ifdef PRINTER_ATTRIBUTE_FRIENDLY_NAME
    ATTRIBUTE_PRINTER_ADD("FRIENDLY-NAME", PRINTER_ATTRIBUTE_FRIENDLY_NAME);
    ATTRIBUTE_PRINTER_ADD("MACHINE", PRINTER_ATTRIBUTE_MACHINE);
    ATTRIBUTE_PRINTER_ADD("PUSHED-USER", PRINTER_ATTRIBUTE_PUSHED_USER);
    ATTRIBUTE_PRINTER_ADD("PUSHED-MACHINE", PRINTER_ATTRIBUTE_PUSHED_MACHINE);
#endif
    // server 2003
#ifdef PRINTER_ATTRIBUTE_TS
    ATTRIBUTE_PRINTER_ADD("TS", PRINTER_ATTRIBUTE_TS);
#endif
#undef ATTRIBUTE_PRINTER_ADD
    return result;
}

const PaperSizeMapType &getPaperSizeMap()
{
    static PaperSizeMapType result;
    if (!result.empty())
    {
        return result;
    }
    // add only first time
#define PAPERSIZE_PRINTER_ADD(type, value) result.insert(std::make_pair(type, value))
    PAPERSIZE_PRINTER_ADD(DMPAPER_A4, "A4");
    PAPERSIZE_PRINTER_ADD(DMPAPER_LETTER, "Letter 8 1/2 x 11 in");
    PAPERSIZE_PRINTER_ADD(DMPAPER_LETTERSMALL, "Letter Small 8 1/2 x 11 in");
    PAPERSIZE_PRINTER_ADD(DMPAPER_TABLOID, "Tabloid 11 x 17 in");
    PAPERSIZE_PRINTER_ADD(DMPAPER_LEDGER, "Ledger 17 x 11 in");
    PAPERSIZE_PRINTER_ADD(DMPAPER_LEGAL, "Legal 8 1/2 x 14 in");
    PAPERSIZE_PRINTER_ADD(DMPAPER_STATEMENT, "Statement 5 1/2 x 8 1/2 in");
    PAPERSIZE_PRINTER_ADD(DMPAPER_EXECUTIVE, "Executive 7 1/4 x 10 1/2 in");
    PAPERSIZE_PRINTER_ADD(DMPAPER_A3, "A3 297 x 420 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_A4, "A4 210 x 297 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_A4SMALL, "A4 Small 210 x 297 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_A5, "A5 148 x 210 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_B4, "B4 (JIS) 250 x 354");
    PAPERSIZE_PRINTER_ADD(DMPAPER_B5, "B5 (JIS) 182 x 257 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_FOLIO, "Folio 8 1/2 x 13 in");
    PAPERSIZE_PRINTER_ADD(DMPAPER_QUARTO, "Quarto 215 x 275 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_10X14, "10x14 in");
    PAPERSIZE_PRINTER_ADD(DMPAPER_11X17, "11x17 in");
    PAPERSIZE_PRINTER_ADD(DMPAPER_NOTE, "Note 8 1/2 x 11 in");
    PAPERSIZE_PRINTER_ADD(DMPAPER_ENV_9, "Envelope #9 3 7/8 x 8 7/8");
    PAPERSIZE_PRINTER_ADD(DMPAPER_ENV_10, "Envelope #10 4 1/8 x 9 1/2");
    PAPERSIZE_PRINTER_ADD(DMPAPER_ENV_11, "Envelope #11 4 1/2 x 10 3/8");
    PAPERSIZE_PRINTER_ADD(DMPAPER_ENV_12, "Envelope #12 4 \276 x 11");
    PAPERSIZE_PRINTER_ADD(DMPAPER_ENV_14, "Envelope #14 5 x 11 1/2");
    PAPERSIZE_PRINTER_ADD(DMPAPER_CSHEET, "C size sheet");
    PAPERSIZE_PRINTER_ADD(DMPAPER_DSHEET, "D size sheet");
    PAPERSIZE_PRINTER_ADD(DMPAPER_ESHEET, "E size sheet");
    PAPERSIZE_PRINTER_ADD(DMPAPER_ENV_DL, "Envelope DL 110 x 220mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_ENV_C5, "Envelope C5 162 x 229 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_ENV_C3, "Envelope C3  324 x 458 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_ENV_C4, "Envelope C4  229 x 324 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_ENV_C6, "Envelope C6  114 x 162 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_ENV_C65, "Envelope C65 114 x 229 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_ENV_B4, "Envelope B4  250 x 353 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_ENV_B5, "Envelope B5  176 x 250 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_ENV_B6, "Envelope B6  176 x 125 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_ENV_ITALY, "Envelope 110 x 230 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_ENV_MONARCH, "Envelope Monarch 3.875 x 7.5 in");
    PAPERSIZE_PRINTER_ADD(DMPAPER_ENV_PERSONAL, "6 3/4 Envelope 3 5/8 x 6 1/2 in");
    PAPERSIZE_PRINTER_ADD(DMPAPER_FANFOLD_US, "US Std Fanfold 14 7/8 x 11 in");
    PAPERSIZE_PRINTER_ADD(DMPAPER_FANFOLD_STD_GERMAN, "German Std Fanfold 8 1/2 x 12 in");
    PAPERSIZE_PRINTER_ADD(DMPAPER_FANFOLD_LGL_GERMAN, "German Legal Fanfold 8 1/2 x 13 in");
    PAPERSIZE_PRINTER_ADD(DMPAPER_ISO_B4, "B4 (ISO) 250 x 353 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_JAPANESE_POSTCARD, "Japanese Postcard 100 x 148 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_9X11, "9 x 11 in");
    PAPERSIZE_PRINTER_ADD(DMPAPER_10X11, "10 x 11 in");
    PAPERSIZE_PRINTER_ADD(DMPAPER_15X11, "15 x 11 in");
    PAPERSIZE_PRINTER_ADD(DMPAPER_ENV_INVITE, "Envelope Invite 220 x 220 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_RESERVED_48, "RESERVED--DO NOT USE");
    PAPERSIZE_PRINTER_ADD(DMPAPER_RESERVED_49, "RESERVED--DO NOT USE");
    PAPERSIZE_PRINTER_ADD(DMPAPER_LETTER_EXTRA, "Letter Extra 9 \275 x 12 in");
    PAPERSIZE_PRINTER_ADD(DMPAPER_LEGAL_EXTRA, "Legal Extra 9 \275 x 15 in");
    PAPERSIZE_PRINTER_ADD(DMPAPER_TABLOID_EXTRA, "Tabloid Extra 11.69 x 18 in");
    PAPERSIZE_PRINTER_ADD(DMPAPER_A4_EXTRA, "A4 Extra 9.27 x 12.69 in");
    PAPERSIZE_PRINTER_ADD(DMPAPER_LETTER_TRANSVERSE, "Letter Transverse 8 \275 x 11 in");
    PAPERSIZE_PRINTER_ADD(DMPAPER_A4_TRANSVERSE, "A4 Transverse 210 x 297 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_LETTER_EXTRA_TRANSVERSE, "Letter Extra Transverse 9\275 x 12 in");
    PAPERSIZE_PRINTER_ADD(DMPAPER_A_PLUS, "SuperA/SuperA/A4 227 x 356 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_B_PLUS, "SuperB/SuperB/A3 305 x 487 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_LETTER_PLUS, "Letter Plus 8.5 x 12.69 in");
    PAPERSIZE_PRINTER_ADD(DMPAPER_A4_PLUS, "A4 Plus 210 x 330 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_A5_TRANSVERSE, "A5 Transverse 148 x 210 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_B5_TRANSVERSE, "B5 (JIS) Transverse 182 x 257 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_A3_EXTRA, "A3 Extra 322 x 445 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_A5_EXTRA, "A5 Extra 174 x 235 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_B5_EXTRA, "B5 (ISO) Extra 201 x 276 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_A2, "A2 420 x 594 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_A3_TRANSVERSE, "A3 Transverse 297 x 420 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_A3_EXTRA_TRANSVERSE, "A3 Extra Transverse 322 x 445 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_DBL_JAPANESE_POSTCARD, "Japanese Double Postcard 200 x 148 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_A6, "A6 105 x 148 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_JENV_KAKU2, "Japanese Envelope Kaku #2");
    PAPERSIZE_PRINTER_ADD(DMPAPER_JENV_KAKU3, "Japanese Envelope Kaku #3");
    PAPERSIZE_PRINTER_ADD(DMPAPER_JENV_CHOU3, "Japanese Envelope Chou #3");
    PAPERSIZE_PRINTER_ADD(DMPAPER_JENV_CHOU4, "Japanese Envelope Chou #4");
    PAPERSIZE_PRINTER_ADD(DMPAPER_LETTER_ROTATED, "Letter Rotated 11 x 8 1/2 11 in");
    PAPERSIZE_PRINTER_ADD(DMPAPER_A3_ROTATED, "A3 Rotated 420 x 297 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_A4_ROTATED, "A4 Rotated 297 x 210 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_A5_ROTATED, "A5 Rotated 210 x 148 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_B4_JIS_ROTATED, "B4 (JIS) Rotated 364 x 257 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_B5_JIS_ROTATED, "B5 (JIS) Rotated 257 x 182 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_JAPANESE_POSTCARD_ROTATED, "apanese Postcard Rotated 148 x 100 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_DBL_JAPANESE_POSTCARD_ROTATED, "ouble Japanese Postcard Rotated 148 x 200 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_A6_ROTATED, "A6 Rotated 148 x 105 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_JENV_KAKU2_ROTATED, "Japanese Envelope Kaku #2 Rotated");
    PAPERSIZE_PRINTER_ADD(DMPAPER_JENV_KAKU3_ROTATED, "Japanese Envelope Kaku #3 Rotated");
    PAPERSIZE_PRINTER_ADD(DMPAPER_JENV_CHOU3_ROTATED, "Japanese Envelope Chou #3 Rotated");
    PAPERSIZE_PRINTER_ADD(DMPAPER_JENV_CHOU4_ROTATED, "Japanese Envelope Chou #4 Rotated");
    PAPERSIZE_PRINTER_ADD(DMPAPER_B6_JIS, "B6 (JIS) 128 x 182 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_B6_JIS_ROTATED, "B6 (JIS) Rotated 182 x 128 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_12X11, "12 x 11 in");
    PAPERSIZE_PRINTER_ADD(DMPAPER_JENV_YOU4, "Japanese Envelope You #4");
    PAPERSIZE_PRINTER_ADD(DMPAPER_JENV_YOU4_ROTATED, "Japanese Envelope You #4 Rotate");
    PAPERSIZE_PRINTER_ADD(DMPAPER_P16K, "PRC 16K 146 x 215 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_P32K, "PRC 32K 97 x 151 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_P32KBIG, "PRC 32K(Big) 97 x 151 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_PENV_1, "PRC Envelope #1 102 x 165 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_PENV_2, "PRC Envelope #2 102 x 176 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_PENV_3, "PRC Envelope #3 125 x 176 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_PENV_4, "PRC Envelope #4 110 x 208 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_PENV_5, "PRC Envelope #5 110 x 220 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_PENV_6, "PRC Envelope #6 120 x 230 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_PENV_7, "PRC Envelope #7 160 x 230 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_PENV_8, "PRC Envelope #8 120 x 309 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_PENV_9, "PRC Envelope #9 229 x 324 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_PENV_10, "PRC Envelope #10 324 x 458 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_P16K_ROTATED, "RC 16K Rotated");
    PAPERSIZE_PRINTER_ADD(DMPAPER_P32K_ROTATED, "RC 32K Rotated");
    PAPERSIZE_PRINTER_ADD(DMPAPER_P32KBIG_ROTATED, "RC 32K(Big) Rotated");
    PAPERSIZE_PRINTER_ADD(DMPAPER_PENV_1_ROTATED, "PRC Envelope #1 Rotated 165 x 102 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_PENV_2_ROTATED, "PRC Envelope #2 Rotated 176 x 102 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_PENV_3_ROTATED, "PRC Envelope #3 Rotated 176 x 125 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_PENV_4_ROTATED, "PRC Envelope #4 Rotated 208 x 110 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_PENV_5_ROTATED, "PRC Envelope #5 Rotated 220 x 110 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_PENV_6_ROTATED, "PRC Envelope #6 Rotated 230 x 120 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_PENV_7_ROTATED, "PRC Envelope #7 Rotated 230 x 160 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_PENV_8_ROTATED, "PRC Envelope #8 Rotated 309 x 120 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_PENV_9_ROTATED, "PRC Envelope #9 Rotated 324 x 229 mm");
    PAPERSIZE_PRINTER_ADD(DMPAPER_PENV_10_ROTATED, "PRC Envelope #10 Rotated 458 x 324 mm");
#undef ATTRIBUTE_PRINTER_ADD
    return result;
}

std::string getPaperSizeName(SHORT paperSize)
{
    if (auto search = getPaperSizeMap().find(paperSize); search != getPaperSizeMap().end())
        return search->second;

    return std::string();
}

Orientation getOrientationType(SHORT orientation)
{
    if (orientation == DMORIENT_LANDSCAPE)
    {
        return Orientation::LANDSCAPE;
    }

    return Orientation::PORTRAIT;
}

Duplex getDuplexType(SHORT duplex)
{
    if (duplex == DMDUP_SIMPLEX)
    {
        return Duplex::SIMPLEX;
    }
    if (duplex == DMDUP_VERTICAL)
    {
        return Duplex::VERTICAL;
    }
    if (duplex == DMDUP_HORIZONTAL)
    {
        return Duplex::HORIZONTAL;
    }

    return Duplex::SIMPLEX;
}

Color getColorType(SHORT color)
{
    if (color == DMCOLOR_COLOR)
    {
        return Color::COLOR;
    }
    if (color == DMCOLOR_MONOCHROME)
    {
        return Color::MONOCHROME;
    }

    return Color::MONOCHROME;
}

PrintQuality getPrintQualityType(DWORD printQuality)
{

    switch (printQuality)
    {
    case DMRES_DRAFT:
        return PrintQuality::DRAFT;
    case DMRES_LOW:
        return PrintQuality::LOW;
    case DMRES_MEDIUM:
        return PrintQuality::MEDIUM;
    case DMRES_HIGH:
        return PrintQuality::HIGH;
    default:
        return PrintQuality::DRAFT;
    }
    return PrintQuality::DRAFT;
}

std::string getPrinterSource(DWORD defaultSource)
{
    switch (defaultSource)
    {
    case DMBIN_UPPER:
        return "UPPER";
    case DMBIN_LOWER:
        return "LOWER";
    case DMBIN_MIDDLE:
        return "MIDDLE";
    case DMBIN_MANUAL:
        return "MANUAL";
    case DMBIN_ENVELOPE:
        return "ENVELOPE";
    case DMBIN_ENVMANUAL:
        return "ENVMANUAL";
    case DMBIN_AUTO:
        return "AUTO";
    case DMBIN_TRACTOR:
        return "TRACTOR";
    case DMBIN_SMALLFMT:
        return "SMALLFMT";
    case DMBIN_LARGEFMT:
        return "LARGEFMT";
    case DMBIN_LARGECAPACITY:
        return "LARGECAPACITY";
    case DMBIN_CASSETTE:
        return "CASSETTE";
    case DMBIN_FORMSOURCE:
        return "FORMSOURCE";
    case DMBIN_USER:
        return "USER";
    default:
        return "UNKNOWN";
    }
}

std::vector<std::string> getStatusArray(DWORD status)
{
    std::vector<std::string> result;

    for (const auto &statusMap : getStatusMap())
    {
        if (status & statusMap.second)
        {
            result.push_back(statusMap.first);
        }
    }

    return result;
}

// * ___________________________________________________________________________
// *
// *                        PrinterManager Implementation
// * ___________________________________________________________________________

ErrorMessage *PrinterManager::getDefaultPrinterName(PrinterName &printerName)
{
    DWORD cSize = 0;
    GetDefaultPrinterW(NULL, &cSize);

    if (cSize == 0)
    {
        static ErrorMessage errorMsg = "Error could not get default printer name";
        return &errorMsg;
    }

    MemValue<uint16_t> bPrinterName(cSize * sizeof(uint16_t));

    if (bPrinterName == NULL)
    {
        static ErrorMessage errorMsg = "Error on allocating memory for printer name";
        return &errorMsg;
    }

    BOOL res = GetDefaultPrinterW((LPWSTR)(bPrinterName.get()), &cSize);

    if (!res)
    {
        static ErrorMessage errorMsg = "Error on GetDefaultPrinterW";
        return &errorMsg;
    }

    std::string result = LPWSTRToString(reinterpret_cast<const wchar_t *>(bPrinterName.get()));
    printerName = std::wstring(result.begin(), result.end());

    return NULL;
}

ErrorMessage *PrinterManager::getOneJob(PrinterName name, int jobId, JobInfo &jobInfo)
{

    PrinterHandle printerHandle((LPWSTR)name.c_str());

    if (!printerHandle)
    {
        static ErrorMessage errorMsg = "Could not open printer ";
        return &errorMsg;
    }

    DWORD sizeBytes = 0, dummyBytes = 0;
    GetJobW(*printerHandle, static_cast<DWORD>(jobId), 2, NULL, sizeBytes, &sizeBytes);
    MemValue<JOB_INFO_2W> job(sizeBytes);

    if (!job)
    {
        static ErrorMessage errorMsg = "Error on allocating memory for printers";
        return &errorMsg;
    }

    BOOL bOK = GetJobW(*printerHandle, static_cast<DWORD>(jobId), 2, (LPBYTE)job.get(), sizeBytes, &dummyBytes);
    if (!bOK)
    {
        static ErrorMessage errorMsg = "Error on GetJob. Wrong job id or it was deleted";
        return &errorMsg;
    }
    // pStatus
    // A pointer to a null-terminated string that specifies the status of the print job.
    // This member should be checked prior to Status and, if pStatus is NULL, the status is defined by the contents of the Status member.

    std::vector<std::string> statusArray;

    if (job->pStatus == NULL)
    {
        statusArray = getStatusArray(job->Status);
    }

    jobInfo.name = LPWSTRToString(job->pPrinterName);
    jobInfo.user = LPWSTRToString(job->pUserName);
    jobInfo.priority = job->Priority;
    jobInfo.size = job->Size;
    jobInfo.status = LPWSTRToString(job->pStatus);
    jobInfo.statusArray = statusArray;
    jobInfo.position = job->Position;
    jobInfo.totalPages = job->TotalPages;
    jobInfo.pagesPrinted = job->PagesPrinted;

    return NULL;
}

std::vector<std::string> getAttributeArray(DWORD attributes)
{
    std::vector<std::string> result;

    for (const auto &attributeMap : getAttributeMap())
    {
        if (attributes & attributeMap.second)
        {
            result.push_back(attributeMap.first);
        }
    }

    return result;
}

void ParsePrinterObject(PRINTER_INFO_2W *printer, PrinterInfo &printerInfo)
{
    printerInfo.name = LPWSTRToString(printer->pPrinterName);
    printerInfo.server = LPWSTRToString(printer->pServerName);
    printerInfo.shareName = LPWSTRToString(printer->pShareName);
    printerInfo.portName = LPWSTRToString(printer->pPortName);
    printerInfo.driverName = LPWSTRToString(printer->pDriverName);
    printerInfo.location = LPWSTRToString(printer->pLocation);
    printerInfo.comment = LPWSTRToString(printer->pComment);
    printerInfo.status = printer->Status;
    printerInfo.statusArray = getStatusArray(printer->Status);
    printerInfo.attributes = printer->Attributes;
    printerInfo.attributeArray = getAttributeArray(printer->Attributes);
    printerInfo.averagePPM = printer->AveragePPM;
    printerInfo.cJobs = printer->cJobs;
    printerInfo.defaultPriority = printer->DefaultPriority;
    printerInfo.startTime = printer->StartTime;
    printerInfo.untilTime = printer->UntilTime;
}

ErrorMessage *PrinterManager::getOnePrinter(PrinterName name, PrinterInfo &printerInfo)
{

    PrinterHandle printerHandle((LPWSTR)name.c_str());

    if (!printerHandle)
    {
        static ErrorMessage errorMsg = "Could not open printer";
        return &errorMsg;
    }

    DWORD sizeBytes = 0, dummyBytes = 0;
    GetPrinterW(printerHandle, 2, NULL, 0, &sizeBytes);

    MemValue<PRINTER_INFO_2W> printer(sizeBytes);
    if (!printer)
    {

        static ErrorMessage errorMsg = "Error on allocating memory for printer info";
        return &errorMsg;
    }

    BOOL bOK = GetPrinterW(printerHandle, 2, (LPBYTE)printer.get(), sizeBytes, &dummyBytes);
    if (!bOK)
    {
        static ErrorMessage errorMsg = "Error on GetPrinter. Wrong printer name or it was deleted";
        return &errorMsg;
    }

    ParsePrinterObject(printer.get(), printerInfo);

    return NULL;
}

ErrorMessage *PrinterManager::getPrinters(std::vector<PrinterInfo> &printersInfo)
{

    DWORD printers_size = 0;
    DWORD printers_size_bytes = 0, dummyBytes = 0;
    DWORD flags = PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS;

    // Get required buffer size
    EnumPrintersW(flags, NULL, 2, NULL, 0, &printers_size_bytes, &printers_size);

    MemValue<PRINTER_INFO_2W> printers(printers_size_bytes);
    if (!printers)
    {
        static ErrorMessage errorMsg = "Failed to allocate memory for printers";
        return &errorMsg;
    }

    BOOL bError = EnumPrintersW(flags, NULL, 2, (LPBYTE)(printers.get()),
                                printers_size_bytes, &dummyBytes, &printers_size);

    if (!bError)
    {
        static ErrorMessage errorMsg = "EnumPrinters Error ";
        return &errorMsg;
    }

    PRINTER_INFO_2W *printer = printers.get();

    for (DWORD i = 0; i < printers_size; ++i, ++printer)
    {
        PrinterInfo printerInfo;
        ParsePrinterObject(printer, printerInfo);
        printersInfo.push_back(printerInfo);
    }

    return NULL;
}

ErrorMessage *PrinterManager::printDirect(PrinterName name, std::string docName, std::string type, std::string data, int &jobId)
{

    PrinterHandle printerHandle((LPWSTR)name.c_str());

    if (!printerHandle)
    {
        static ErrorMessage errorMsg = "Could not open printer ";
        return &errorMsg;
    }

    std::wstring docNameWide(docName.begin(), docName.end());
    std::wstring typeWide(type.begin(), type.end());

    DOC_INFO_1W DocInfo;
    DocInfo.pDocName = (LPWSTR)docNameWide.c_str();
    DocInfo.pOutputFile = NULL;
    // RAW format: A data type consisting of PDL data that can be sent to a device without further processing.
    // https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-rprn/e81cbc09-ab05-4a32-ae4a-8ec57b436c43#Appendix_A_211
    DocInfo.pDatatype = (LPWSTR)typeWide.c_str();

    jobId = StartDocPrinterW(*printerHandle, 1, (LPBYTE)&DocInfo);
    if (jobId == 0)
    {
        static ErrorMessage errorMsg = "StartDocPrinter error: ";
        return &errorMsg;
    }

    if (!StartPagePrinter(*printerHandle))
    {
        static ErrorMessage errorMsg = "StartPagePrinter error: ";
        return &errorMsg;
    }

    DWORD bytesWritten = 0;
    BOOL success = WritePrinter(*printerHandle, (LPVOID)data.c_str(),
                                (DWORD)data.size(), &bytesWritten);

    EndPagePrinter(*printerHandle);
    EndDocPrinter(*printerHandle);

    if (!success || bytesWritten != data.size())
    {
        static ErrorMessage errorMsg = "Failed to write all data to printer";
        return &errorMsg;
    }

    return NULL;
}

ErrorMessage *PrinterManager::getSupportedPrintFormats(std::vector<std::string> &dataTypes)
{

    DWORD numBytes = 0, processorsNum = 0;

    // Check the amount of bytes required
    EnumPrintProcessorsW(NULL, NULL, 1, (LPBYTE)(NULL), numBytes, &numBytes, &processorsNum);
    MemValue<_PRINTPROCESSOR_INFO_1W> processors(numBytes);

    // Retrieve processors
    BOOL isOK = EnumPrintProcessorsW(NULL, NULL, 1, (LPBYTE)(processors.get()), numBytes, &numBytes, &processorsNum);
    if (!isOK)
    {
        static ErrorMessage errorMsg = "Error on EnumPrintProcessorsW";
        return &errorMsg;
    }

    _PRINTPROCESSOR_INFO_1W *pProcessor = processors.get();
    for (DWORD processor_i = 0; processor_i < processorsNum; ++processor_i, ++pProcessor)
    {
        numBytes = 0;
        DWORD dataTypesNum = 0;
        EnumPrintProcessorDatatypesW(NULL, pProcessor->pName, 1, (LPBYTE)(NULL), numBytes, &numBytes, &dataTypesNum);
        MemValue<_DATATYPES_INFO_1W> dataTypesWin(numBytes);
        isOK = EnumPrintProcessorDatatypesW(NULL, pProcessor->pName, 1, (LPBYTE)(dataTypesWin.get()), numBytes, &numBytes, &dataTypesNum);

        if (!isOK)
        {
            static ErrorMessage errorMsg = "Error on EnumPrintProcessorDatatypesW";
            return &errorMsg;
        }

        _DATATYPES_INFO_1W *pDataType = dataTypesWin.get();
        for (DWORD j = 0; j < dataTypesNum; ++j, ++pDataType)
        {
            dataTypes.push_back(LPWSTRToString(pDataType->pName));
        }
    }

    return NULL;
}

ErrorMessage *PrinterManager::getPrinterDevMode(const std::wstring &printerName, PrinterDevMode &pDevMode)
{

    PrinterHandle printerHandle((LPWSTR)printerName.c_str());

    if (!printerHandle)
    {
        static ErrorMessage errorMsg = "Could not open printer ";
        return &errorMsg;
    }

    // Get required buffer size
    DWORD needed = 0;
    GetPrinterW(printerHandle, 2, NULL, 0, &needed);
    if (needed == 0)
    {
        static ErrorMessage errorMsg = "Failed to get printer info size";
        return &errorMsg;
    }

    // Allocate memory for printer info
    MemValue<PRINTER_INFO_2W> pInfo(needed);

    if (!pInfo)
    {
        static ErrorMessage errorMsg = "Memory allocation failed";
        return &errorMsg;
    }
    // Get printer info
    if (!GetPrinterW(printerHandle, 2, (LPBYTE)pInfo.get(), needed, &needed))
    {
        static ErrorMessage errorMsg = "Failed to get printer info";
        return &errorMsg;
    }

    if (pInfo->pDevMode == NULL)
    {
        static ErrorMessage errorMsg = "Failed to get printer info";
        return &errorMsg;
    }

    pDevMode.deviceName = printerName;
    pDevMode.paperSize = getPaperSizeName(pInfo->pDevMode->dmPaperSize);

    // dmPaperSize This member must be zero if the length and width of the paper are specified by the dmPaperLength and dmPaperWidth members.
    if (pInfo->pDevMode->dmPaperSize == 0)
    {
        pDevMode.paperSize = std::to_string(pInfo->pDevMode->dmPaperWidth) + " x " + std::to_string(pInfo->pDevMode->dmPaperLength);
    }

    pDevMode.orientation = getOrientationType(pInfo->pDevMode->dmOrientation);
    pDevMode.duplex = getDuplexType(pInfo->pDevMode->dmDuplex);
    pDevMode.color = getColorType(pInfo->pDevMode->dmColor);
    pDevMode.copies = (int)pInfo->pDevMode->dmCopies;
    pDevMode.defaultSource = getPrinterSource(pInfo->pDevMode->dmDefaultSource);
    pDevMode.printQuality = getPrintQualityType(pInfo->pDevMode->dmPrintQuality);
    pDevMode.scale = pInfo->pDevMode->dmScale;
    pDevMode.collate = (pInfo->pDevMode->dmCollate == DMCOLLATE_TRUE);

    return NULL;
}
