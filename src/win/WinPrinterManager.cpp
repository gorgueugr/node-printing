/*
 * Windows backend of node-printing (Winspool / GDI / DEVMODE).
 *
 * See src/PrinterManager.hpp for the frozen contract. Conventions kept here:
 *   - every std::string is UTF-8; UTF-16 is used only at the Win32 boundary
 *     (utf8ToWide / wideToUtf8, CP_UTF8)
 *   - a method returns an empty string on success, a human readable message
 *     (always carrying the Win32 error code and its FormatMessage text) on
 *     failure; nothing throws and nothing leaks, printer handles are RAII
 *   - JOB_INFO_1W/JOB_INFO_2W, PRINTER_INFO_2W and DEVMODEW are mapped into the
 *     platform independent structs, unmapped values are left 0/-1/empty
 *
 * Job command aliases accepted by setJob (after normaliseJobCommand()):
 *   PAUSE  and HOLD    -> JOB_CONTROL_PAUSE
 *   RESUME and RELEASE -> JOB_CONTROL_RESUME
 * The remaining commands map one to one. JOB_CONTROL_RETAIN, JOB_CONTROL_RELEASE,
 * JOB_CONTROL_SENT_TO_PRINTER and JOB_CONTROL_LAST_PAGE_EJECTED only exist in
 * Windows SDKs starting with XP, so their handling is #ifdef fed.
 *
 * Deliberate behaviour worth knowing:
 *   - getJobs("") enumerates every queue and skips the ones that cannot be
 *     opened (a single unreachable network queue must not fail the whole call);
 *     with an explicit printer name every failure is reported
 *   - EnumJobsW is tried at level 2 first (it carries Size/Time/pDevMode) and
 *     falls back to level 1 for drivers that only implement that
 *   - the spooler exposes no "processing" timestamp, so processingTime stays -1
 *     and completedTime is derived from Submitted + Time for finished jobs
 */

#ifdef _WIN32

#include "../PrinterManager.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <winspool.h>
#include <wingdi.h>

#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "Winspool.lib")
#pragma comment(lib, "Gdi32.lib")

namespace
{

/* ------------------------------------------------------------------ */
/* UTF-8 / UTF-16 conversion                                           */
/* ------------------------------------------------------------------ */

std::wstring utf8ToWide(const std::string &value)
{
    if (value.empty())
    {
        return std::wstring();
    }

    const int length = static_cast<int>(value.size());
    int size = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), length, NULL, 0);
    if (size <= 0)
    {
        return std::wstring();
    }

    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), length, &result[0], size);
    return result;
}

std::string wideToUtf8(const wchar_t *value)
{
    if (value == NULL || *value == L'\0')
    {
        return std::string();
    }

    int size = WideCharToMultiByte(CP_UTF8, 0, value, -1, NULL, 0, NULL, NULL);
    if (size <= 0)
    {
        return std::string();
    }

    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, &result[0], size, NULL, NULL);
    result.resize(static_cast<size_t>(size - 1)); // drop the terminating NUL
    return result;
}

std::string wideToUtf8(const std::wstring &value)
{
    return wideToUtf8(value.c_str());
}

/** cbData bytes of UTF-16, cut at the first NUL, as UTF-8. */
std::string wideBufferToUtf8(const void *data, DWORD cbData)
{
    if (data == NULL || cbData < sizeof(wchar_t))
    {
        return std::string();
    }

    const wchar_t *text = reinterpret_cast<const wchar_t *>(data);
    std::wstring buffer(text, text + (cbData / sizeof(wchar_t)));
    size_t end = buffer.find(L'\0');
    if (end != std::wstring::npos)
    {
        buffer.resize(end);
    }
    return wideToUtf8(buffer);
}

/* ------------------------------------------------------------------ */
/* error reporting                                                     */
/* ------------------------------------------------------------------ */

std::string trimTrailingWhitespace(const std::string &value)
{
    size_t end = value.find_last_not_of(" \t\r\n");
    if (end == std::string::npos)
    {
        return std::string();
    }
    return value.substr(0, end + 1);
}

/** FormatMessage text of a Win32 error code, empty when unavailable. */
std::string win32ErrorText(DWORD code)
{
    LPWSTR buffer = NULL;
    DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                      FORMAT_MESSAGE_IGNORE_INSERTS,
                                  NULL, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                  reinterpret_cast<LPWSTR>(&buffer), 0, NULL);

    std::string text;
    if (length > 0 && buffer != NULL)
    {
        text = wideToUtf8(buffer);
        LocalFree(reinterpret_cast<HLOCAL>(buffer));
    }
    return trimTrailingWhitespace(text);
}

/** "context (Win32 error code): message" */
std::string errorWithCode(const std::string &context, DWORD code)
{
    std::ostringstream stream;
    stream << context << " (Win32 error " << static_cast<unsigned long>(code) << ")";

    std::string text = win32ErrorText(code);
    if (!text.empty())
    {
        stream << ": " << text;
    }
    return stream.str();
}

std::string joinList(const std::vector<std::string> &values)
{
    std::string result;
    for (size_t i = 0; i < values.size(); ++i)
    {
        if (i != 0)
        {
            result += ", ";
        }
        result += values[i];
    }
    return result;
}

/* ------------------------------------------------------------------ */
/* small utilities                                                     */
/* ------------------------------------------------------------------ */

wchar_t lowerWideChar(wchar_t value)
{
    if (value >= L'A' && value <= L'Z')
    {
        return static_cast<wchar_t>(value - L'A' + L'a');
    }
    return value;
}

bool equalsIgnoreCase(const std::wstring &left, const std::wstring &right)
{
    if (left.size() != right.size())
    {
        return false;
    }
    for (size_t i = 0; i < left.size(); ++i)
    {
        if (lowerWideChar(left[i]) != lowerWideChar(right[i]))
        {
            return false;
        }
    }
    return true;
}

std::string localComputerName()
{
    wchar_t buffer[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD size = MAX_COMPUTERNAME_LENGTH + 1;
    if (!GetComputerNameW(buffer, &size))
    {
        return std::string();
    }
    return wideToUtf8(buffer);
}

/** True for NULL, the local machine name, "\\machine" or ".". */
bool isLocalServerName(const wchar_t *serverName)
{
    if (serverName == NULL || *serverName == L'\0')
    {
        return true;
    }

    std::wstring server(serverName);
    while (!server.empty() && server[0] == L'\\')
    {
        server.erase(0, 1);
    }
    if (server.empty() || server == L".")
    {
        return true;
    }

    return equalsIgnoreCase(server, utf8ToWide(localComputerName()));
}

/* ------------------------------------------------------------------ */
/* RAII printer handle and buffer helpers                              */
/* ------------------------------------------------------------------ */

class PrinterHandle
{
public:
    PrinterHandle() : _handle(NULL) {}
    ~PrinterHandle() { close(); }

    bool open(const std::wstring &printerName)
    {
        close();
        if (printerName.empty())
        {
            return false;
        }
        if (!OpenPrinterW(const_cast<LPWSTR>(printerName.c_str()), &_handle, NULL))
        {
            _handle = NULL;
            return false;
        }
        return true;
    }

    void close()
    {
        if (_handle != NULL)
        {
            ClosePrinter(_handle);
            _handle = NULL;
        }
    }

    HANDLE get() const { return _handle; }

private:
    PrinterHandle(const PrinterHandle &);
    PrinterHandle &operator=(const PrinterHandle &);

    HANDLE _handle;
};

/** Enough properly aligned structures to hold `bytes` of a Win32 buffer. */
template <typename Structure>
std::vector<Structure> allocateStructures(DWORD bytes)
{
    size_t count = (static_cast<size_t>(bytes) + sizeof(Structure) - 1) / sizeof(Structure);
    if (count == 0)
    {
        count = 1;
    }
    return std::vector<Structure>(count);
}

enum EnumerationResult
{
    ENUMERATION_OK = 0, // buffer filled, `returned` holds the entry count
    ENUMERATION_EMPTY,  // nothing to enumerate, not an error
    ENUMERATION_FAILED  // errorCode holds the Win32 error code
};

/**
 * Two call size probe shared by EnumPrintersW / EnumJobsW / GetJobW /
 * EnumPrinterDataExW: ask for the required size with a NULL buffer, allocate,
 * then enumerate, retrying while the spooler keeps growing between the calls.
 * "Found nothing" is reported as ENUMERATION_EMPTY so callers can tell it apart
 * from a real error.
 */
template <typename Structure, typename Call>
EnumerationResult growingEnumerate(std::vector<Structure> &buffer, DWORD &returned, Call call,
                                   DWORD &errorCode)
{
    buffer.clear();
    returned = 0;
    errorCode = 0;

    DWORD needed = 0;
    DWORD count = 0;
    SetLastError(ERROR_SUCCESS);
    if (call(NULL, 0, &needed, &count))
    {
        if (needed == 0 && count == 0)
        {
            return ENUMERATION_EMPTY;
        }
    }
    else
    {
        DWORD probeError = GetLastError();
        if (needed == 0)
        {
            if (probeError == ERROR_SUCCESS || probeError == ERROR_INSUFFICIENT_BUFFER ||
                probeError == ERROR_MORE_DATA || probeError == ERROR_INVALID_PARAMETER ||
                probeError == ERROR_NOT_FOUND || probeError == ERROR_FILE_NOT_FOUND)
            {
                return ENUMERATION_EMPTY;
            }
            errorCode = probeError;
            return ENUMERATION_FAILED;
        }
    }

    if (needed > (64u << 20))
    {
        errorCode = ERROR_NOT_ENOUGH_MEMORY;
        return ENUMERATION_FAILED;
    }

    buffer = allocateStructures<Structure>(needed);
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        DWORD used = 0;
        DWORD entryCount = 0;
        DWORD capacity = static_cast<DWORD>(buffer.size() * sizeof(Structure));
        SetLastError(ERROR_SUCCESS);
        if (call(reinterpret_cast<LPBYTE>(buffer.data()), capacity, &used, &entryCount))
        {
            returned = entryCount;
            return ENUMERATION_OK;
        }

        DWORD code = GetLastError();
        if ((code == ERROR_MORE_DATA || code == ERROR_INSUFFICIENT_BUFFER) && used > 0 &&
            used < (64u << 20))
        {
            buffer = allocateStructures<Structure>(used);
            continue;
        }

        errorCode = code;
        return ENUMERATION_FAILED;
    }

    errorCode = ERROR_INSUFFICIENT_BUFFER;
    return ENUMERATION_FAILED;
}


/* ------------------------------------------------------------------ */
/* status / attribute / paper size maps                                */
/* ------------------------------------------------------------------ */

typedef std::map<std::string, DWORD> NameToFlagMap;

/** Names of every flag of `flags` that is listed in `map`. */
std::vector<std::string> flagsToNames(DWORD flags, const NameToFlagMap &map)
{
    std::vector<std::string> names;
    for (NameToFlagMap::const_iterator it = map.begin(); it != map.end(); ++it)
    {
        if (it->second != 0 && (flags & it->second) == it->second)
        {
            names.push_back(it->first);
        }
    }
    return names;
}

/** Name of the entry whose value equals `code`, empty when not listed. */
std::string nameForCode(DWORD code, const NameToFlagMap &map)
{
    for (NameToFlagMap::const_iterator it = map.begin(); it != map.end(); ++it)
    {
        if (it->second == code)
        {
            return it->first;
        }
    }
    return std::string();
}

const NameToFlagMap &getPrinterStatusMap()
{
    static const NameToFlagMap map = {
#define STATUS_PRINTER_ADD(value, type) {value, type}
        STATUS_PRINTER_ADD("BUSY", PRINTER_STATUS_BUSY),
        STATUS_PRINTER_ADD("DOOR-OPEN", PRINTER_STATUS_DOOR_OPEN),
        STATUS_PRINTER_ADD("ERROR", PRINTER_STATUS_ERROR),
        STATUS_PRINTER_ADD("INITIALIZING", PRINTER_STATUS_INITIALIZING),
        STATUS_PRINTER_ADD("IO-ACTIVE", PRINTER_STATUS_IO_ACTIVE),
        STATUS_PRINTER_ADD("MANUAL-FEED", PRINTER_STATUS_MANUAL_FEED),
        STATUS_PRINTER_ADD("NO-TONER", PRINTER_STATUS_NO_TONER),
        STATUS_PRINTER_ADD("NOT-AVAILABLE", PRINTER_STATUS_NOT_AVAILABLE),
        STATUS_PRINTER_ADD("OFFLINE", PRINTER_STATUS_OFFLINE),
        STATUS_PRINTER_ADD("OUT-OF-MEMORY", PRINTER_STATUS_OUT_OF_MEMORY),
        STATUS_PRINTER_ADD("OUTPUT-BIN-FULL", PRINTER_STATUS_OUTPUT_BIN_FULL),
        STATUS_PRINTER_ADD("PAGE-PUNT", PRINTER_STATUS_PAGE_PUNT),
        STATUS_PRINTER_ADD("PAPER-JAM", PRINTER_STATUS_PAPER_JAM),
        STATUS_PRINTER_ADD("PAPER-OUT", PRINTER_STATUS_PAPER_OUT),
        STATUS_PRINTER_ADD("PAPER-PROBLEM", PRINTER_STATUS_PAPER_PROBLEM),
        STATUS_PRINTER_ADD("PAUSED", PRINTER_STATUS_PAUSED),
        STATUS_PRINTER_ADD("PENDING-DELETION", PRINTER_STATUS_PENDING_DELETION),
        STATUS_PRINTER_ADD("POWER-SAVE", PRINTER_STATUS_POWER_SAVE),
        STATUS_PRINTER_ADD("PRINTING", PRINTER_STATUS_PRINTING),
        STATUS_PRINTER_ADD("PROCESSING", PRINTER_STATUS_PROCESSING),
        STATUS_PRINTER_ADD("SERVER-UNKNOWN", PRINTER_STATUS_SERVER_UNKNOWN),
        STATUS_PRINTER_ADD("TONER-LOW", PRINTER_STATUS_TONER_LOW),
        STATUS_PRINTER_ADD("USER-INTERVENTION", PRINTER_STATUS_USER_INTERVENTION),
        STATUS_PRINTER_ADD("WAITING", PRINTER_STATUS_WAITING),
        STATUS_PRINTER_ADD("WARMING-UP", PRINTER_STATUS_WARMING_UP),
#undef STATUS_PRINTER_ADD
    };
    return map;
}

const NameToFlagMap &getPrinterAttributeMap()
{
    static const NameToFlagMap map = {
#define ATTRIBUTE_PRINTER_ADD(value, type) {value, type}
        ATTRIBUTE_PRINTER_ADD("DIRECT", PRINTER_ATTRIBUTE_DIRECT),
        ATTRIBUTE_PRINTER_ADD("DO-COMPLETE-FIRST", PRINTER_ATTRIBUTE_DO_COMPLETE_FIRST),
        ATTRIBUTE_PRINTER_ADD("ENABLE-DEVQ", PRINTER_ATTRIBUTE_ENABLE_DEVQ),
        ATTRIBUTE_PRINTER_ADD("HIDDEN", PRINTER_ATTRIBUTE_HIDDEN),
        ATTRIBUTE_PRINTER_ADD("KEEPPRINTEDJOBS", PRINTER_ATTRIBUTE_KEEPPRINTEDJOBS),
        ATTRIBUTE_PRINTER_ADD("LOCAL", PRINTER_ATTRIBUTE_LOCAL),
        ATTRIBUTE_PRINTER_ADD("NETWORK", PRINTER_ATTRIBUTE_NETWORK),
        ATTRIBUTE_PRINTER_ADD("PUBLISHED", PRINTER_ATTRIBUTE_PUBLISHED),
        ATTRIBUTE_PRINTER_ADD("QUEUED", PRINTER_ATTRIBUTE_QUEUED),
        ATTRIBUTE_PRINTER_ADD("RAW-ONLY", PRINTER_ATTRIBUTE_RAW_ONLY),
        ATTRIBUTE_PRINTER_ADD("SHARED", PRINTER_ATTRIBUTE_SHARED),
        ATTRIBUTE_PRINTER_ADD("OFFLINE", PRINTER_ATTRIBUTE_WORK_OFFLINE),
#ifdef PRINTER_ATTRIBUTE_FAX
        ATTRIBUTE_PRINTER_ADD("FAX", PRINTER_ATTRIBUTE_FAX),
#endif
#ifdef PRINTER_ATTRIBUTE_FRIENDLY_NAME
        ATTRIBUTE_PRINTER_ADD("FRIENDLY-NAME", PRINTER_ATTRIBUTE_FRIENDLY_NAME),
        ATTRIBUTE_PRINTER_ADD("MACHINE", PRINTER_ATTRIBUTE_MACHINE),
        ATTRIBUTE_PRINTER_ADD("PUSHED-USER", PRINTER_ATTRIBUTE_PUSHED_USER),
        ATTRIBUTE_PRINTER_ADD("PUSHED-MACHINE", PRINTER_ATTRIBUTE_PUSHED_MACHINE),
#endif
#ifdef PRINTER_ATTRIBUTE_TS
        ATTRIBUTE_PRINTER_ADD("TS", PRINTER_ATTRIBUTE_TS),
#endif
#undef ATTRIBUTE_PRINTER_ADD
    };
    return map;
}

const NameToFlagMap &getJobStatusMap()
{
    static const NameToFlagMap map = {
#define JOB_STATUS_ADD(value, type) {value, type}
        JOB_STATUS_ADD("BLOCKED-DEVQ", JOB_STATUS_BLOCKED_DEVQ),
        JOB_STATUS_ADD("DELETED", JOB_STATUS_DELETED),
        JOB_STATUS_ADD("DELETING", JOB_STATUS_DELETING),
        JOB_STATUS_ADD("ERROR", JOB_STATUS_ERROR),
        JOB_STATUS_ADD("OFFLINE", JOB_STATUS_OFFLINE),
        JOB_STATUS_ADD("PAPEROUT", JOB_STATUS_PAPEROUT),
        JOB_STATUS_ADD("PAUSED", JOB_STATUS_PAUSED),
        JOB_STATUS_ADD("PRINTED", JOB_STATUS_PRINTED),
        JOB_STATUS_ADD("PRINTING", JOB_STATUS_PRINTING),
        JOB_STATUS_ADD("RESTART", JOB_STATUS_RESTART),
        JOB_STATUS_ADD("SPOOLING", JOB_STATUS_SPOOLING),
        JOB_STATUS_ADD("USER-INTERVENTION", JOB_STATUS_USER_INTERVENTION),
#ifdef JOB_STATUS_COMPLETE
        JOB_STATUS_ADD("COMPLETE", JOB_STATUS_COMPLETE),
#endif
#ifdef JOB_STATUS_RETAINED
        JOB_STATUS_ADD("RETAINED", JOB_STATUS_RETAINED),
#endif
#undef JOB_STATUS_ADD
    };
    return map;
}

/** Canonical short paper names, one entry per DMPAPER_* constant. */
const NameToFlagMap &getPaperSizeMap()
{
    static const NameToFlagMap map = {
#define PAPER(value, type) {value, type}
        PAPER("LETTER", DMPAPER_LETTER),
        PAPER("LETTER_SMALL", DMPAPER_LETTERSMALL),
        PAPER("TABLOID", DMPAPER_TABLOID),
        PAPER("LEDGER", DMPAPER_LEDGER),
        PAPER("LEGAL", DMPAPER_LEGAL),
        PAPER("STATEMENT", DMPAPER_STATEMENT),
        PAPER("EXECUTIVE", DMPAPER_EXECUTIVE),
        PAPER("A3", DMPAPER_A3),
        PAPER("A4", DMPAPER_A4),
        PAPER("A4_SMALL", DMPAPER_A4SMALL),
        PAPER("A5", DMPAPER_A5),
        PAPER("B4", DMPAPER_B4),
        PAPER("B5", DMPAPER_B5),
        PAPER("FOLIO", DMPAPER_FOLIO),
        PAPER("QUARTO", DMPAPER_QUARTO),
        PAPER("10X14", DMPAPER_10X14),
        PAPER("11X17", DMPAPER_11X17),
        PAPER("NOTE", DMPAPER_NOTE),
        PAPER("ENV_9", DMPAPER_ENV_9),
        PAPER("ENV_10", DMPAPER_ENV_10),
        PAPER("ENV_11", DMPAPER_ENV_11),
        PAPER("ENV_12", DMPAPER_ENV_12),
        PAPER("ENV_14", DMPAPER_ENV_14),
        PAPER("CSHEET", DMPAPER_CSHEET),
        PAPER("DSHEET", DMPAPER_DSHEET),
        PAPER("ESHEET", DMPAPER_ESHEET),
        PAPER("ENV_DL", DMPAPER_ENV_DL),
        PAPER("ENV_C5", DMPAPER_ENV_C5),
        PAPER("ENV_C3", DMPAPER_ENV_C3),
        PAPER("ENV_C4", DMPAPER_ENV_C4),
        PAPER("ENV_C6", DMPAPER_ENV_C6),
        PAPER("ENV_C65", DMPAPER_ENV_C65),
        PAPER("ENV_B4", DMPAPER_ENV_B4),
        PAPER("ENV_B5", DMPAPER_ENV_B5),
        PAPER("ENV_B6", DMPAPER_ENV_B6),
        PAPER("ENV_ITALY", DMPAPER_ENV_ITALY),
        PAPER("ENV_MONARCH", DMPAPER_ENV_MONARCH),
        PAPER("ENV_PERSONAL", DMPAPER_ENV_PERSONAL),
        PAPER("FANFOLD_US", DMPAPER_FANFOLD_US),
        PAPER("FANFOLD_STD_GERMAN", DMPAPER_FANFOLD_STD_GERMAN),
        PAPER("FANFOLD_LGL_GERMAN", DMPAPER_FANFOLD_LGL_GERMAN),
        PAPER("ISO_B4", DMPAPER_ISO_B4),
        PAPER("JAPANESE_POSTCARD", DMPAPER_JAPANESE_POSTCARD),
        PAPER("9X11", DMPAPER_9X11),
        PAPER("10X11", DMPAPER_10X11),
        PAPER("15X11", DMPAPER_15X11),
        PAPER("ENV_INVITE", DMPAPER_ENV_INVITE),
        PAPER("LETTER_EXTRA", DMPAPER_LETTER_EXTRA),
        PAPER("LEGAL_EXTRA", DMPAPER_LEGAL_EXTRA),
        PAPER("TABLOID_EXTRA", DMPAPER_TABLOID_EXTRA),
        PAPER("A4_EXTRA", DMPAPER_A4_EXTRA),
        PAPER("LETTER_TRANSVERSE", DMPAPER_LETTER_TRANSVERSE),
        PAPER("A4_TRANSVERSE", DMPAPER_A4_TRANSVERSE),
        PAPER("LETTER_EXTRA_TRANSVERSE", DMPAPER_LETTER_EXTRA_TRANSVERSE),
        PAPER("A_PLUS", DMPAPER_A_PLUS),
        PAPER("B_PLUS", DMPAPER_B_PLUS),
        PAPER("LETTER_PLUS", DMPAPER_LETTER_PLUS),
        PAPER("A4_PLUS", DMPAPER_A4_PLUS),
        PAPER("A5_TRANSVERSE", DMPAPER_A5_TRANSVERSE),
        PAPER("B5_TRANSVERSE", DMPAPER_B5_TRANSVERSE),
        PAPER("A3_EXTRA", DMPAPER_A3_EXTRA),
        PAPER("A5_EXTRA", DMPAPER_A5_EXTRA),
        PAPER("B5_EXTRA", DMPAPER_B5_EXTRA),
        PAPER("A2", DMPAPER_A2),
        PAPER("A3_TRANSVERSE", DMPAPER_A3_TRANSVERSE),
        PAPER("A3_EXTRA_TRANSVERSE", DMPAPER_A3_EXTRA_TRANSVERSE),
        PAPER("DBL_JAPANESE_POSTCARD", DMPAPER_DBL_JAPANESE_POSTCARD),
        PAPER("A6", DMPAPER_A6),
        PAPER("JENV_KAKU2", DMPAPER_JENV_KAKU2),
        PAPER("JENV_KAKU3", DMPAPER_JENV_KAKU3),
        PAPER("JENV_CHOU3", DMPAPER_JENV_CHOU3),
        PAPER("JENV_CHOU4", DMPAPER_JENV_CHOU4),
        PAPER("LETTER_ROTATED", DMPAPER_LETTER_ROTATED),
        PAPER("A3_ROTATED", DMPAPER_A3_ROTATED),
        PAPER("A4_ROTATED", DMPAPER_A4_ROTATED),
        PAPER("A5_ROTATED", DMPAPER_A5_ROTATED),
        PAPER("B4_JIS_ROTATED", DMPAPER_B4_JIS_ROTATED),
        PAPER("B5_JIS_ROTATED", DMPAPER_B5_JIS_ROTATED),
        PAPER("JAPANESE_POSTCARD_ROTATED", DMPAPER_JAPANESE_POSTCARD_ROTATED),
        PAPER("DBL_JAPANESE_POSTCARD_ROTATED", DMPAPER_DBL_JAPANESE_POSTCARD_ROTATED),
        PAPER("A6_ROTATED", DMPAPER_A6_ROTATED),
        PAPER("JENV_KAKU2_ROTATED", DMPAPER_JENV_KAKU2_ROTATED),
        PAPER("JENV_KAKU3_ROTATED", DMPAPER_JENV_KAKU3_ROTATED),
        PAPER("JENV_CHOU3_ROTATED", DMPAPER_JENV_CHOU3_ROTATED),
        PAPER("JENV_CHOU4_ROTATED", DMPAPER_JENV_CHOU4_ROTATED),
        PAPER("B6_JIS", DMPAPER_B6_JIS),
        PAPER("B6_JIS_ROTATED", DMPAPER_B6_JIS_ROTATED),
        PAPER("12X11", DMPAPER_12X11),
        PAPER("JENV_YOU4", DMPAPER_JENV_YOU4),
        PAPER("JENV_YOU4_ROTATED", DMPAPER_JENV_YOU4_ROTATED),
        PAPER("P16K", DMPAPER_P16K),
        PAPER("P32K", DMPAPER_P32K),
        PAPER("P32KBIG", DMPAPER_P32KBIG),
        PAPER("PENV_1", DMPAPER_PENV_1),
        PAPER("PENV_2", DMPAPER_PENV_2),
        PAPER("PENV_3", DMPAPER_PENV_3),
        PAPER("PENV_4", DMPAPER_PENV_4),
        PAPER("PENV_5", DMPAPER_PENV_5),
        PAPER("PENV_6", DMPAPER_PENV_6),
        PAPER("PENV_7", DMPAPER_PENV_7),
        PAPER("PENV_8", DMPAPER_PENV_8),
        PAPER("PENV_9", DMPAPER_PENV_9),
        PAPER("PENV_10", DMPAPER_PENV_10),
        PAPER("P16K_ROTATED", DMPAPER_P16K_ROTATED),
        PAPER("P32K_ROTATED", DMPAPER_P32K_ROTATED),
        PAPER("P32KBIG_ROTATED", DMPAPER_P32KBIG_ROTATED),
        PAPER("PENV_1_ROTATED", DMPAPER_PENV_1_ROTATED),
        PAPER("PENV_2_ROTATED", DMPAPER_PENV_2_ROTATED),
        PAPER("PENV_3_ROTATED", DMPAPER_PENV_3_ROTATED),
        PAPER("PENV_4_ROTATED", DMPAPER_PENV_4_ROTATED),
        PAPER("PENV_5_ROTATED", DMPAPER_PENV_5_ROTATED),
        PAPER("PENV_6_ROTATED", DMPAPER_PENV_6_ROTATED),
        PAPER("PENV_7_ROTATED", DMPAPER_PENV_7_ROTATED),
        PAPER("PENV_8_ROTATED", DMPAPER_PENV_8_ROTATED),
        PAPER("PENV_9_ROTATED", DMPAPER_PENV_9_ROTATED),
        PAPER("PENV_10_ROTATED", DMPAPER_PENV_10_ROTATED),
#undef PAPER
    };
    return map;
}

/** DMBIN_* feed bins. */
const NameToFlagMap &getPaperSourceMap()
{
    static const NameToFlagMap map = {
#define SOURCE(value, type) {value, type}
        SOURCE("Auto", DMBIN_AUTO),
        SOURCE("Tray 1", DMBIN_UPPER),
        SOURCE("Tray 2", DMBIN_LOWER),
        SOURCE("Middle", DMBIN_MIDDLE),
        SOURCE("Manual", DMBIN_MANUAL),
        SOURCE("Envelope", DMBIN_ENVELOPE),
        SOURCE("Envelope Manual", DMBIN_ENVMANUAL),
        SOURCE("Tractor", DMBIN_TRACTOR),
        SOURCE("Small Format", DMBIN_SMALLFMT),
        SOURCE("Large Format", DMBIN_LARGEFMT),
        SOURCE("Large Capacity", DMBIN_LARGECAPACITY),
        SOURCE("Cassette", DMBIN_CASSETTE),
        SOURCE("Form Source", DMBIN_FORMSOURCE),
        SOURCE("Custom", DMBIN_USER),
#undef SOURCE
    };
    return map;
}

/** DMMEDIA_* media types. */
const NameToFlagMap &getMediaTypeMap()
{
    static const NameToFlagMap map = {
#define MEDIA(value, type) {value, type}
        MEDIA("Plain", DMMEDIA_STANDARD),
        MEDIA("Transparency", DMMEDIA_TRANSPARENCY),
        MEDIA("Glossy", DMMEDIA_GLOSSY),
        MEDIA("User", DMMEDIA_USER),
#undef MEDIA
    };
    return map;
}

/* ------------------------------------------------------------------ */
/* print formats and job commands                                      */
/* ------------------------------------------------------------------ */

const std::vector<std::string> &winSupportedPrintFormats()
{
    static const std::vector<std::string> formats = {"RAW", "TEXT", "XPS_PASS", "PDF", "POSTSCRIPT"};
    return formats;
}

bool isSupportedPrintFormat(const std::string &upperType)
{
    const std::vector<std::string> &formats = winSupportedPrintFormats();
    for (size_t i = 0; i < formats.size(); ++i)
    {
        if (formats[i] == upperType)
        {
            return true;
        }
    }
    return false;
}

std::string unsupportedPrintFormatError(const std::string &type)
{
    return "unsupported print format \"" + type + "\"; supported formats are " +
           joinList(winSupportedPrintFormats());
}

std::string unsupportedJobCommandError(const std::string &command)
{
    return "unsupported job command \"" + command + "\"; supported commands are " +
           joinList(allJobCommands());
}

/** Normalised command -> JOB_CONTROL_*; false for commands this SDK lacks. */
bool resolveJobCommand(const std::string &command, DWORD &control)
{
    if (command == "CANCEL")
    {
        control = JOB_CONTROL_CANCEL;
        return true;
    }
    if (command == "PAUSE" || command == "HOLD") // alias
    {
        control = JOB_CONTROL_PAUSE;
        return true;
    }
    if (command == "RESUME" || command == "RELEASE") // alias
    {
        control = JOB_CONTROL_RESUME;
        return true;
    }
    if (command == "RESTART")
    {
        control = JOB_CONTROL_RESTART;
        return true;
    }
    if (command == "DELETE")
    {
        control = JOB_CONTROL_DELETE;
        return true;
    }
#ifdef JOB_CONTROL_RETAIN
    if (command == "RETAIN")
    {
        control = JOB_CONTROL_RETAIN;
        return true;
    }
#endif
#ifdef JOB_CONTROL_SENT_TO_PRINTER
    if (command == "SENT-TO-PRINTER")
    {
        control = JOB_CONTROL_SENT_TO_PRINTER;
        return true;
    }
#endif
#ifdef JOB_CONTROL_LAST_PAGE_EJECTED
    if (command == "LAST-PAGE-EJECTED")
    {
        control = JOB_CONTROL_LAST_PAGE_EJECTED;
        return true;
    }
#endif
    return false;
}


/* ------------------------------------------------------------------ */
/* win32 status -> canonical state                                     */
/* ------------------------------------------------------------------ */

/** PRINTER_STATUS_* -> idle|processing|stopped|unknown */
std::string printerState(DWORD status)
{
    const DWORD stopped = PRINTER_STATUS_PAUSED | PRINTER_STATUS_ERROR | PRINTER_STATUS_OFFLINE |
                          PRINTER_STATUS_NOT_AVAILABLE | PRINTER_STATUS_PENDING_DELETION |
                          PRINTER_STATUS_SERVER_UNKNOWN;
    const DWORD processing = PRINTER_STATUS_PRINTING | PRINTER_STATUS_PROCESSING |
                             PRINTER_STATUS_BUSY | PRINTER_STATUS_IO_ACTIVE |
                             PRINTER_STATUS_WARMING_UP | PRINTER_STATUS_INITIALIZING;
    const DWORD idle = PRINTER_STATUS_WAITING | PRINTER_STATUS_POWER_SAVE;

    if ((status & stopped) != 0)
    {
        return "stopped";
    }
    if ((status & processing) != 0)
    {
        return "processing";
    }
    if ((status & idle) != 0 || status == 0)
    {
        return "idle";
    }
    return "unknown";
}

/** A queue takes jobs unless it is offline, paused or in error. */
bool printerAcceptsJobs(DWORD attributes, DWORD status)
{
    if ((attributes & PRINTER_ATTRIBUTE_WORK_OFFLINE) != 0)
    {
        return false;
    }

    const DWORD blocking = PRINTER_STATUS_PAUSED | PRINTER_STATUS_OFFLINE |
                           PRINTER_STATUS_NOT_AVAILABLE | PRINTER_STATUS_PENDING_DELETION |
                           PRINTER_STATUS_ERROR | PRINTER_STATUS_SERVER_UNKNOWN;
    return (status & blocking) == 0;
}

/** JOB_STATUS_* -> pending|processing|held|stopped|canceled|aborted|completed|unknown */
std::string jobState(DWORD status)
{
#if defined(JOB_STATUS_COMPLETE)
    const DWORD completed = JOB_STATUS_PRINTED | JOB_STATUS_COMPLETE;
#else
    const DWORD completed = JOB_STATUS_PRINTED;
#endif
    const DWORD canceled = JOB_STATUS_DELETED | JOB_STATUS_DELETING;
    const DWORD stopped = JOB_STATUS_OFFLINE | JOB_STATUS_BLOCKED_DEVQ |
                          JOB_STATUS_USER_INTERVENTION | JOB_STATUS_PAPEROUT;

    if ((status & completed) != 0)
    {
        return "completed";
    }
    if ((status & canceled) != 0)
    {
        return "canceled";
    }
    if ((status & JOB_STATUS_PAUSED) != 0)
    {
        return "held";
    }
    if ((status & stopped) != 0)
    {
        return "stopped";
    }
    if ((status & JOB_STATUS_ERROR) != 0)
    {
        return "aborted";
    }
    if ((status & JOB_STATUS_PRINTING) != 0)
    {
        return "processing";
    }
    if ((status & (JOB_STATUS_SPOOLING | JOB_STATUS_RESTART)) != 0)
    {
        return "pending";
    }
    return "unknown";
}

/** Finished or cancelled jobs, dropped when the caller asked only for pending ones. */
bool isFinishedJob(DWORD status)
{
#if defined(JOB_STATUS_COMPLETE)
    const DWORD finished = JOB_STATUS_PRINTED | JOB_STATUS_COMPLETE;
#else
    const DWORD finished = JOB_STATUS_PRINTED;
#endif
    return (status & (finished | JOB_STATUS_DELETED | JOB_STATUS_DELETING)) != 0;
}

/** pStatus text when the driver supplies one, canonical flags otherwise. */
std::string jobStatusText(const wchar_t *statusText, DWORD status)
{
    std::string text = wideToUtf8(statusText);
    if (!text.empty())
    {
        return text;
    }
    return joinList(flagsToNames(status, getJobStatusMap()));
}

/* ------------------------------------------------------------------ */
/* FILETIME / SYSTEMTIME -> milliseconds since the unix epoch          */
/* ------------------------------------------------------------------ */

/** 0 -> -1 (unknown); ticks are 100 ns since 1601-01-01. */
double fileTimeToUnixMilliseconds(const FILETIME &time)
{
    ULARGE_INTEGER value;
    value.LowPart = time.dwLowDateTime;
    value.HighPart = time.dwHighDateTime;
    if (value.QuadPart == 0)
    {
        return -1.0;
    }

    const unsigned long long epochDifference = 116444736000000000ULL; // 1601 -> 1970
    if (value.QuadPart < epochDifference)
    {
        return -1.0;
    }
    return static_cast<double>(value.QuadPart - epochDifference) / 10000.0;
}

double systemTimeToUnixMilliseconds(const SYSTEMTIME &time)
{
    if (time.wYear == 0)
    {
        return -1.0;
    }

    FILETIME fileTime;
    if (!SystemTimeToFileTime(&time, &fileTime))
    {
        return -1.0;
    }
    return fileTimeToUnixMilliseconds(fileTime);
}

/* ------------------------------------------------------------------ */
/* struct mapping                                                      */
/* ------------------------------------------------------------------ */

void fillJobInfo(const JOB_INFO_2W &source, const std::string &fallbackPrinterName, JobInfo &job)
{
    job.id = static_cast<int>(source.JobId);
    job.name = wideToUtf8(source.pDocument);
    job.printerName = source.pPrinterName != NULL ? wideToUtf8(source.pPrinterName)
                                                 : fallbackPrinterName;
    job.user = wideToUtf8(source.pUserName);
    job.format = wideToUtf8(source.pDatatype);
    job.state = jobState(source.Status);
    job.status = jobStatusText(source.pStatus, source.Status);
    job.statusArray = flagsToNames(source.Status, getJobStatusMap());
    job.priority = static_cast<int>(source.Priority);
    job.size = static_cast<double>(source.Size);
    job.position = static_cast<int>(source.Position);
    job.totalPages = static_cast<int>(source.TotalPages);
    job.pagesPrinted = static_cast<int>(source.PagesPrinted);
    job.submittedTime = systemTimeToUnixMilliseconds(source.Submitted);
    job.completedTime = -1.0;
    job.processingTime = -1.0;

    // Time is the number of seconds elapsed since the job started spooling, it
    // only makes sense once the job is done.
    if (source.Time > 0 && isFinishedJob(source.Status) && job.submittedTime >= 0)
    {
        job.completedTime = job.submittedTime + (static_cast<double>(source.Time) * 1000.0);
    }
}

void fillJobInfo(const JOB_INFO_1W &source, const std::string &fallbackPrinterName, JobInfo &job)
{
    job.id = static_cast<int>(source.JobId);
    job.name = wideToUtf8(source.pDocument);
    job.printerName = source.pPrinterName != NULL ? wideToUtf8(source.pPrinterName)
                                                 : fallbackPrinterName;
    job.user = wideToUtf8(source.pUserName);
    job.format = wideToUtf8(source.pDatatype);
    job.state = jobState(source.Status);
    job.status = jobStatusText(source.pStatus, source.Status);
    job.statusArray = flagsToNames(source.Status, getJobStatusMap());
    job.priority = static_cast<int>(source.Priority);
    job.size = 0.0; // JOB_INFO_1W carries no byte size
    job.position = static_cast<int>(source.Position);
    job.totalPages = static_cast<int>(source.TotalPages);
    job.pagesPrinted = static_cast<int>(source.PagesPrinted);
    job.submittedTime = systemTimeToUnixMilliseconds(source.Submitted);
    job.completedTime = -1.0;
    job.processingTime = -1.0;
}

/** The default queue name, empty when none is configured; never fails. */
std::string fetchDefaultPrinterName()
{
    DWORD size = 0;
    GetDefaultPrinterW(NULL, &size);
    if (size == 0)
    {
        return std::string();
    }

    std::wstring buffer(static_cast<size_t>(size), L'\0');
    DWORD capacity = size;
    if (!GetDefaultPrinterW(&buffer[0], &capacity))
    {
        return std::string();
    }
    return wideToUtf8(buffer.c_str());
}

void fillPrinterInfo(const PRINTER_INFO_2W &source, const std::string &defaultPrinterName,
                     PrinterInfo &printer)
{
    printer.name = wideToUtf8(source.pPrinterName);
    printer.instance = std::string(); // CUPS only
    printer.server = isLocalServerName(source.pServerName) ? std::string()
                                                           : wideToUtf8(source.pServerName);
    printer.shareName = wideToUtf8(source.pShareName);
    printer.portName = wideToUtf8(source.pPortName);
    printer.driverName = wideToUtf8(source.pDriverName);
    printer.location = wideToUtf8(source.pLocation);
    printer.comment = wideToUtf8(source.pComment);
    printer.uri = "winspool://" + printer.portName; // best effort device identifier
    printer.state = printerState(source.Status);
    printer.isDefault = !printer.name.empty() &&
                        equalsIgnoreCase(utf8ToWide(printer.name), utf8ToWide(defaultPrinterName));
    printer.isShared = (source.Attributes & PRINTER_ATTRIBUTE_SHARED) != 0;
    printer.isLocal = (source.Attributes & PRINTER_ATTRIBUTE_LOCAL) != 0;
    printer.acceptsJobs = printerAcceptsJobs(source.Attributes, source.Status);
    printer.status = static_cast<int>(source.Status);
    printer.statusArray = flagsToNames(source.Status, getPrinterStatusMap());
    printer.attributes = static_cast<int>(source.Attributes);
    printer.attributeArray = flagsToNames(source.Attributes, getPrinterAttributeMap());
    printer.averagePPM = static_cast<int>(source.AveragePPM);
    printer.cJobs = static_cast<int>(source.cJobs);
    printer.defaultPriority = static_cast<int>(source.DefaultPriority);
    printer.startTime = static_cast<int>(source.StartTime);
    printer.untilTime = static_cast<int>(source.UntilTime);
    printer.options.clear();
}

/* ------------------------------------------------------------------ */
/* spooler document writer                                             */
/* ------------------------------------------------------------------ */

/**
 * StartDocPrinterW / StartPagePrinter / WritePrinter / EndPagePrinter /
 * EndDocPrinter with RAII clean up: whatever happens, an open page and document
 * are closed before the printer handle is released.
 */
class SpoolJobWriter
{
public:
    SpoolJobWriter()
        : _printer(NULL), _jobId(0), _pageOpen(false), _documentOpen(false)
    {
    }

    ~SpoolJobWriter() { closeDocument(); }

    void attach(HANDLE printer) { _printer = printer; }

    bool startDocument(const std::wstring &documentName, const std::wstring &dataType)
    {
        DOC_INFO_1W info;
        info.pDocName = const_cast<LPWSTR>(documentName.c_str());
        info.pOutputFile = NULL;
        info.pDatatype = const_cast<LPWSTR>(dataType.c_str());

        _jobId = StartDocPrinterW(_printer, 1, reinterpret_cast<LPBYTE>(&info));
        if (_jobId == 0)
        {
            return false;
        }
        _documentOpen = true;
        return true;
    }

    bool startPage()
    {
        if (_pageOpen)
        {
            return true;
        }
        if (!StartPagePrinter(_printer))
        {
            return false;
        }
        _pageOpen = true;
        return true;
    }

    bool endPage()
    {
        if (!_pageOpen)
        {
            return true;
        }
        _pageOpen = false;
        return EndPagePrinter(_printer) != FALSE;
    }

    bool endDocument()
    {
        if (!endPage())
        {
            return false;
        }
        if (!_documentOpen)
        {
            return true;
        }
        _documentOpen = false;
        return EndDocPrinter(_printer) != FALSE;
    }

    /** Buffers of any size, spooled in at most 1 MB chunks (WritePrinter is 32 bit). */
    bool writeChunked(const char *data, size_t length, std::string &error)
    {
        const size_t chunkSize = 1024 * 1024;
        size_t offset = 0;

        while (offset < length)
        {
            const size_t remaining = length - offset;
            const DWORD chunk =
                static_cast<DWORD>(remaining < chunkSize ? remaining : chunkSize);
            DWORD written = 0;
            if (!WritePrinter(_printer, const_cast<char *>(data + offset), chunk, &written))
            {
                error = errorWithCode("WritePrinter failed", GetLastError());
                return false;
            }
            if (written != chunk)
            {
                std::ostringstream stream;
                stream << "WritePrinter wrote only " << written << " of " << chunk << " bytes";
                error = stream.str();
                return false;
            }
            offset += chunk;
        }
        return true;
    }

    DWORD jobId() const { return _jobId; }

    /** Closes an open page/document, ignoring errors: used on the error paths. */
    void closeDocument()
    {
        if (_pageOpen)
        {
            _pageOpen = false;
            EndPagePrinter(_printer);
        }
        if (_documentOpen)
        {
            _documentOpen = false;
            EndDocPrinter(_printer);
        }
    }

private:
    SpoolJobWriter(const SpoolJobWriter &);
    SpoolJobWriter &operator=(const SpoolJobWriter &);

    HANDLE _printer;
    DWORD _jobId;
    bool _pageOpen;
    bool _documentOpen;
};


/* ------------------------------------------------------------------ */
/* driver data value rendering                                         */
/* ------------------------------------------------------------------ */

std::string bytesToHex(const BYTE *data, DWORD size)
{
    static const char digits[] = "0123456789abcdef";
    std::string result;
    for (DWORD i = 0; i < size; ++i)
    {
        if (i != 0)
        {
            result += " ";
        }
        result += "0x";
        result += digits[(data[i] >> 4) & 0x0f];
        result += digits[data[i] & 0x0f];
    }
    return result;
}

/** PRINTER_ENUM_VALUESW data as text; strings are decoded as UTF-16. */
std::string printerDataValueToString(const PRINTER_ENUM_VALUESW &value)
{
    switch (value.dwType)
    {
    case REG_SZ:
    case REG_EXPAND_SZ:
    case REG_MULTI_SZ:
        return wideBufferToUtf8(value.pData, value.cbData);
    case REG_DWORD:
    {
        if (value.pData == NULL || value.cbData < sizeof(DWORD))
        {
            return std::string();
        }
        DWORD number = 0;
        std::memcpy(&number, value.pData, sizeof(number));
        std::ostringstream stream;
        stream << number;
        return stream.str();
    }
    default:
        if (value.pData == NULL || value.cbData == 0)
        {
            return std::string();
        }
        return bytesToHex(value.pData, value.cbData);
    }
}

/* ------------------------------------------------------------------ */
/* the Windows implementation                                          */
/* ------------------------------------------------------------------ */

class WinPrinterManager : public PrinterManager
{
public:
    virtual ~WinPrinterManager() {}

    virtual std::string getPrinters(std::vector<PrinterInfo> &printers);
    virtual std::string getDefaultPrinterName(PrinterName &printerName);
    virtual std::string getOnePrinter(const PrinterName &name, PrinterInfo &printer);
    virtual std::string getPrinterDevMode(const PrinterName &name, PrinterDevMode &devMode);
    virtual std::string getPrinterDriverOptions(const PrinterName &name,
                                                std::map<std::string, std::map<std::string, bool> > &options);
    virtual std::string getJobs(const PrinterName &name, bool includeCompleted,
                                std::vector<JobInfo> &jobs);
    virtual std::string getOneJob(const PrinterName &name, int jobId, JobInfo &job);
    virtual std::string setJob(const PrinterName &name, int jobId, const std::string &command,
                               bool &done);
    virtual std::string printDirect(const PrinterName &name, const std::string &docName,
                                    const std::string &type, const std::string &data, int &jobId);
    virtual std::string printFile(const PrinterName &name, const std::string &docName,
                                  const std::string &type, const std::string &filename, int &jobId);
    virtual std::vector<std::string> getSupportedPrintFormats();
    virtual std::vector<std::string> getSupportedJobCommands();
};

std::string WinPrinterManager::getPrinters(std::vector<PrinterInfo> &printers)
{
    printers.clear();

    std::vector<PRINTER_INFO_2W> buffer;
    DWORD count = 0;
    DWORD errorCode = 0;
    auto enumerate = [](LPBYTE data, DWORD capacity, LPDWORD needed, LPDWORD returned) -> BOOL {
        // local queues and connections: the spooler sees both
        return EnumPrintersW(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS, NULL, 2, data, capacity,
                             needed, returned);
    };

    EnumerationResult result = growingEnumerate(buffer, count, enumerate, errorCode);
    if (result == ENUMERATION_EMPTY)
    {
        return std::string(); // no queue is not an error
    }
    if (result == ENUMERATION_FAILED)
    {
        return errorWithCode("EnumPrintersW failed", errorCode);
    }

    const std::string defaultPrinterName = fetchDefaultPrinterName();
    for (DWORD i = 0; i < count; ++i)
    {
        PrinterInfo printer;
        fillPrinterInfo(buffer[i], defaultPrinterName, printer);
        printers.push_back(printer);
    }
    return std::string();
}

std::string WinPrinterManager::getDefaultPrinterName(PrinterName &printerName)
{
    // An empty result with no error simply means "no default queue configured".
    printerName = fetchDefaultPrinterName();
    return std::string();
}

std::string WinPrinterManager::getOnePrinter(const PrinterName &name, PrinterInfo &printer)
{
    std::wstring wideName = utf8ToWide(name);
    if (wideName.empty())
    {
        return "printer name must not be empty";
    }

    PrinterHandle handle;
    if (!handle.open(wideName))
    {
        DWORD code = GetLastError();
        return errorWithCode("OpenPrinterW(\"" + name + "\") failed", code);
    }

    std::vector<PRINTER_INFO_2W> buffer;
    DWORD count = 0;
    DWORD errorCode = 0;
    HANDLE printerHandle = handle.get();
    auto enumerate = [printerHandle](LPBYTE data, DWORD capacity, LPDWORD needed,
                                     LPDWORD returned) -> BOOL {
        BOOL ok = GetPrinterW(printerHandle, 2, data, capacity, needed);
        if (ok && returned != NULL)
        {
            *returned = 1; // GetPrinterW reports no entry count
        }
        return ok;
    };

    EnumerationResult result = growingEnumerate(buffer, count, enumerate, errorCode);
    if (result == ENUMERATION_FAILED)
    {
        return errorWithCode("GetPrinterW(\"" + name + "\") failed", errorCode);
    }
    if (result == ENUMERATION_EMPTY || count == 0)
    {
        return "printer \"" + name + "\" was not found";
    }

    fillPrinterInfo(buffer[0], fetchDefaultPrinterName(), printer);
    return std::string();
}

std::string WinPrinterManager::getPrinterDevMode(const PrinterName &name, PrinterDevMode &devMode)
{
    std::wstring wideName = utf8ToWide(name);
    if (wideName.empty())
    {
        return "printer name must not be empty";
    }

    PrinterHandle handle;
    if (!handle.open(wideName))
    {
        DWORD code = GetLastError();
        return errorWithCode("OpenPrinterW(\"" + name + "\") failed", code);
    }

    // DocumentPropertiesW(..., NULL, NULL, 0) returns the DEVMODE size, or a
    // negative value when the driver cannot describe its defaults.
    LONG size = DocumentPropertiesW(NULL, handle.get(), const_cast<LPWSTR>(wideName.c_str()), NULL,
                                    NULL, 0);
    if (size < 0)
    {
        return errorWithCode("DocumentPropertiesW could not size the DEVMODE of \"" + name + "\"",
                             static_cast<DWORD>(size));
    }
    if (size == 0)
    {
        return "the driver of \"" + name + "\" exposes no DEVMODE";
    }

    std::vector<DEVMODEW> buffer = allocateStructures<DEVMODEW>(static_cast<DWORD>(size));
    LONG filled = DocumentPropertiesW(NULL, handle.get(), const_cast<LPWSTR>(wideName.c_str()),
                                      buffer.data(), NULL, DM_OUT_BUFFER);
    if (filled != IDOK)
    {
        return errorWithCode("DocumentPropertiesW failed for \"" + name + "\"",
                             static_cast<DWORD>(filled));
    }

    const DEVMODEW &source = buffer[0];
    devMode.deviceName = name;
    devMode.paperSize = nameForCode(static_cast<DWORD>(source.dmPaperSize), getPaperSizeMap());
    devMode.paperSizeCode = static_cast<int>(source.dmPaperSize);
    devMode.mediaType = nameForCode(static_cast<DWORD>(source.dmMediaType), getMediaTypeMap());
    devMode.orientation = source.dmOrientation == DMORIENT_LANDSCAPE ? LANDSCAPE : PORTRAIT;

    if (source.dmDuplex == DMDUP_VERTICAL)
    {
        devMode.duplex = VERTICAL;
    }
    else if (source.dmDuplex == DMDUP_HORIZONTAL)
    {
        devMode.duplex = HORIZONTAL;
    }
    else
    {
        devMode.duplex = SIMPLEX;
    }

    devMode.color = source.dmColor == DMCOLOR_COLOR ? COLOR : MONOCHROME;
    devMode.copies = source.dmCopies > 0 ? static_cast<int>(source.dmCopies) : 1;
    devMode.collate = source.dmCollate == DMCOLLATE_TRUE;
    devMode.scale = source.dmScale > 0 ? static_cast<int>(source.dmScale) : 100;
    devMode.defaultSource =
        nameForCode(static_cast<DWORD>(source.dmDefaultSource), getPaperSourceMap());
    devMode.defaultSourceCode = static_cast<int>(source.dmDefaultSource);
    devMode.resolutionX = 0;
    devMode.resolutionY = 0;

    // A negative dmPrintQuality is one of the DMRES_* names, a positive one is
    // the horizontal resolution in dots per inch.
    if (source.dmPrintQuality < 0)
    {
        switch (source.dmPrintQuality)
        {
        case DMRES_DRAFT:
            devMode.printQuality = DRAFT;
            break;
        case DMRES_LOW:
            devMode.printQuality = LOW;
            break;
        case DMRES_HIGH:
            devMode.printQuality = HIGH;
            break;
        default:
            devMode.printQuality = MEDIUM;
            break;
        }
    }
    else if (source.dmPrintQuality > 0)
    {
        devMode.printQuality = MEDIUM;
        devMode.resolutionX = static_cast<int>(source.dmPrintQuality);
        devMode.resolutionY = source.dmYResolution > 0 ? static_cast<int>(source.dmYResolution)
                                                      : devMode.resolutionX;
    }

    return std::string();
}

std::string WinPrinterManager::getPrinterDriverOptions(
    const PrinterName &name, std::map<std::string, std::map<std::string, bool> > &options)
{
    options.clear();

    std::wstring wideName = utf8ToWide(name);
    if (wideName.empty())
    {
        return "printer name must not be empty";
    }

    PrinterHandle handle;
    if (!handle.open(wideName))
    {
        DWORD code = GetLastError();
        return errorWithCode("OpenPrinterW(\"" + name + "\") failed", code);
    }

    std::vector<PRINTER_ENUM_VALUESW> values;
    DWORD count = 0;
    DWORD errorCode = 0;
    HANDLE printerHandle = handle.get();
    auto enumerate = [printerHandle](LPBYTE data, DWORD capacity, LPDWORD needed,
                                     LPDWORD returned) -> BOOL {
        return EnumPrinterDataExW(printerHandle, L"PrinterDriverData", data, capacity, needed,
                                  returned);
    };

    // Best effort by design: a driver that exposes nothing (or an SDK whose
    // spooler cannot enumerate the key) yields an empty map and no error.
    EnumerationResult result = growingEnumerate(values, count, enumerate, errorCode);
    if (result != ENUMERATION_OK)
    {
        return std::string();
    }

    for (DWORD i = 0; i < count; ++i)
    {
        const std::string valueName = wideBufferToUtf8(values[i].pValueName,
                                                       values[i].cbValueName);
        if (valueName.empty())
        {
            continue;
        }
        options[valueName][printerDataValueToString(values[i])] = true;
    }
    return std::string();
}


/** Jobs of a single queue; ENUMERATION_EMPTY is not an error. */
std::string collectJobsForPrinter(const std::string &printerName, bool includeCompleted,
                                 std::vector<JobInfo> &jobs)
{
    std::wstring wideName = utf8ToWide(printerName);
    if (wideName.empty())
    {
        return "printer name must not be empty";
    }

    PrinterHandle handle;
    if (!handle.open(wideName))
    {
        DWORD code = GetLastError();
        return errorWithCode("OpenPrinterW(\"" + printerName + "\") failed", code);
    }
    HANDLE printerHandle = handle.get();

    std::vector<JOB_INFO_2W> level2;
    DWORD count = 0;
    DWORD errorCode = 0;
    auto enumerate2 = [printerHandle](LPBYTE data, DWORD capacity, LPDWORD needed,
                                      LPDWORD returned) -> BOOL {
        return EnumJobsW(printerHandle, 0, 0xFFFFFFFF, 2, data, capacity, needed, returned);
    };
    EnumerationResult result = growingEnumerate(level2, count, enumerate2, errorCode);
    if (result == ENUMERATION_OK)
    {
        for (DWORD i = 0; i < count; ++i)
        {
            JobInfo job;
            fillJobInfo(level2[i], printerName, job);
            if (includeCompleted || !isFinishedJob(level2[i].Status))
            {
                jobs.push_back(job);
            }
        }
        return std::string();
    }
    if (result == ENUMERATION_EMPTY)
    {
        return std::string();
    }

    // Some drivers only implement level 1; it is still better than nothing.
    std::vector<JOB_INFO_1W> level1;
    DWORD count1 = 0;
    DWORD errorCode1 = 0;
    auto enumerate1 = [printerHandle](LPBYTE data, DWORD capacity, LPDWORD needed,
                                      LPDWORD returned) -> BOOL {
        return EnumJobsW(printerHandle, 0, 0xFFFFFFFF, 1, data, capacity, needed, returned);
    };
    if (growingEnumerate(level1, count1, enumerate1, errorCode1) == ENUMERATION_OK)
    {
        for (DWORD i = 0; i < count1; ++i)
        {
            JobInfo job;
            fillJobInfo(level1[i], printerName, job);
            if (includeCompleted || !isFinishedJob(level1[i].Status))
            {
                jobs.push_back(job);
            }
        }
        return std::string();
    }

    return errorWithCode("EnumJobsW failed for printer \"" + printerName + "\"", errorCode);
}

std::string WinPrinterManager::getJobs(const PrinterName &name, bool includeCompleted,
                                       std::vector<JobInfo> &jobs)
{
    jobs.clear();

    if (!name.empty())
    {
        return collectJobsForPrinter(name, includeCompleted, jobs);
    }

    // Every queue: one unreachable network printer must not fail the whole call,
    // so queues that cannot be opened are skipped.
    std::vector<PRINTER_INFO_2W> buffer;
    DWORD count = 0;
    DWORD errorCode = 0;
    auto enumerate = [](LPBYTE data, DWORD capacity, LPDWORD needed, LPDWORD returned) -> BOOL {
        return EnumPrintersW(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS, NULL, 2, data, capacity,
                             needed, returned);
    };

    EnumerationResult result = growingEnumerate(buffer, count, enumerate, errorCode);
    if (result == ENUMERATION_FAILED)
    {
        return errorWithCode("EnumPrintersW failed while collecting the jobs of every queue",
                             errorCode);
    }
    if (result == ENUMERATION_EMPTY)
    {
        return std::string();
    }

    for (DWORD i = 0; i < count; ++i)
    {
        collectJobsForPrinter(wideToUtf8(buffer[i].pPrinterName), includeCompleted, jobs);
    }
    return std::string();
}

std::string WinPrinterManager::getOneJob(const PrinterName &name, int jobId, JobInfo &job)
{
    if (jobId < 0)
    {
        return "invalid job id " + std::to_string(jobId);
    }

    std::wstring wideName = utf8ToWide(name);
    if (wideName.empty())
    {
        return "printer name must not be empty";
    }

    PrinterHandle handle;
    if (!handle.open(wideName))
    {
        DWORD code = GetLastError();
        return errorWithCode("OpenPrinterW(\"" + name + "\") failed", code);
    }

    const DWORD wantedId = static_cast<DWORD>(jobId);
    HANDLE printerHandle = handle.get();
    std::vector<JOB_INFO_1W> buffer;
    DWORD count = 0;
    DWORD errorCode = 0;
    auto query = [printerHandle, wantedId](LPBYTE data, DWORD capacity, LPDWORD needed,
                                           LPDWORD returned) -> BOOL {
        BOOL ok = GetJobW(printerHandle, wantedId, 1, data, capacity, needed);
        if (ok && returned != NULL)
        {
            *returned = 1; // GetJobW reports no entry count
        }
        return ok;
    };

    EnumerationResult result = growingEnumerate(buffer, count, query, errorCode);
    if (result == ENUMERATION_OK && count > 0)
    {
        fillJobInfo(buffer[0], name, job);
        return std::string();
    }
    if (result == ENUMERATION_FAILED)
    {
        return errorWithCode("GetJobW(" + std::to_string(jobId) + ") failed on printer \"" + name +
                                 "\"",
                             errorCode);
    }

    return "job " + std::to_string(jobId) + " was not found on printer \"" + name + "\"";
}

std::string WinPrinterManager::setJob(const PrinterName &name, int jobId,
                                      const std::string &command, bool &done)
{
    done = false;

    if (jobId < 0)
    {
        return "invalid job id " + std::to_string(jobId);
    }

    const std::string normalised = normaliseJobCommand(command);
    DWORD control = 0;
    if (normalised.empty() || !resolveJobCommand(normalised, control))
    {
        return unsupportedJobCommandError(command);
    }

    std::wstring wideName = utf8ToWide(name);
    if (wideName.empty())
    {
        return "printer name must not be empty";
    }

    PrinterHandle handle;
    if (!handle.open(wideName))
    {
        DWORD code = GetLastError();
        return errorWithCode("OpenPrinterW(\"" + name + "\") failed", code);
    }

    if (!SetJobW(handle.get(), static_cast<DWORD>(jobId), 0, NULL, control))
    {
        DWORD code = GetLastError();
        return errorWithCode("SetJobW(" + normalised + ") failed on job " + std::to_string(jobId) +
                                 " of printer \"" + name + "\"",
                             code);
    }

    done = true;
    return std::string();
}

std::string WinPrinterManager::printDirect(const PrinterName &name, const std::string &docName,
                                           const std::string &type, const std::string &data,
                                           int &jobId)
{
    jobId = 0;

    const std::string upperType = toUpperAscii(type);
    if (!isSupportedPrintFormat(upperType))
    {
        return unsupportedPrintFormatError(type);
    }
    if (data.empty())
    {
        return "empty document";
    }

    std::wstring wideName = utf8ToWide(name);
    if (wideName.empty())
    {
        return "printer name must not be empty";
    }

    PrinterHandle handle;
    if (!handle.open(wideName))
    {
        DWORD code = GetLastError();
        return errorWithCode("OpenPrinterW(\"" + name + "\") failed", code);
    }

    const std::wstring wideDocumentName = utf8ToWide(docName.empty() ? std::string("print job") : docName);
    SpoolJobWriter writer;
    writer.attach(handle.get());

    if (!writer.startDocument(wideDocumentName, utf8ToWide(upperType)))
    {
        DWORD code = GetLastError();
        return errorWithCode("StartDocPrinterW failed on printer \"" + name + "\"", code);
    }
    if (!writer.startPage())
    {
        DWORD code = GetLastError();
        return errorWithCode("StartPagePrinter failed on printer \"" + name + "\"", code);
    }

    std::string writeError;
    if (!writer.writeChunked(data.data(), data.size(), writeError))
    {
        return writeError;
    }

    if (!writer.endPage())
    {
        DWORD code = GetLastError();
        return errorWithCode("EndPagePrinter failed on printer \"" + name + "\"", code);
    }
    if (!writer.endDocument())
    {
        DWORD code = GetLastError();
        return errorWithCode("EndDocPrinter failed on printer \"" + name + "\"", code);
    }

    jobId = static_cast<int>(writer.jobId());
    return std::string();
}

std::string WinPrinterManager::printFile(const PrinterName &name, const std::string &docName,
                                         const std::string &type, const std::string &filename,
                                         int &jobId)
{
    jobId = 0;

    const std::string upperType = toUpperAscii(type);
    if (!isSupportedPrintFormat(upperType))
    {
        return unsupportedPrintFormatError(type);
    }

    std::wstring wideName = utf8ToWide(name);
    if (wideName.empty())
    {
        return "printer name must not be empty";
    }

    const std::wstring wideFilename = utf8ToWide(filename);
    if (wideFilename.empty())
    {
        return "file name must not be empty";
    }

    const DWORD attributes = GetFileAttributesW(wideFilename.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES)
    {
        DWORD code = GetLastError();
        return errorWithCode("file \"" + filename + "\" does not exist or cannot be read", code);
    }
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
    {
        return "file \"" + filename + "\" is a directory";
    }

    // Opened through std::filesystem so that the UTF-8 path reaches Win32 as UTF-16.
    std::ifstream file(std::filesystem::u8path(filename).c_str(), std::ios::in | std::ios::binary);
    if (!file.is_open())
    {
        return "cannot open file \"" + filename + "\" for reading";
    }

    PrinterHandle handle;
    if (!handle.open(wideName))
    {
        DWORD code = GetLastError();
        return errorWithCode("OpenPrinterW(\"" + name + "\") failed", code);
    }

    const std::wstring wideDocumentName =
        utf8ToWide(docName.empty() ? std::string("print job") : docName);
    SpoolJobWriter writer;
    writer.attach(handle.get());

    if (!writer.startDocument(wideDocumentName, utf8ToWide(upperType)))
    {
        DWORD code = GetLastError();
        return errorWithCode("StartDocPrinterW failed on printer \"" + name + "\"", code);
    }
    if (!writer.startPage())
    {
        DWORD code = GetLastError();
        return errorWithCode("StartPagePrinter failed on printer \"" + name + "\"", code);
    }

    std::vector<char> buffer(1024 * 1024);
    for (;;)
    {
        file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize read = file.gcount();
        if (read > 0)
        {
            std::string writeError;
            if (!writer.writeChunked(buffer.data(), static_cast<size_t>(read), writeError))
            {
                return writeError;
            }
        }
        if (read < static_cast<std::streamsize>(buffer.size()))
        {
            break; // end of file, or a read error reported below
        }
    }

    if (file.bad())
    {
        return "failed while reading \"" + filename + "\"";
    }

    if (!writer.endPage())
    {
        DWORD code = GetLastError();
        return errorWithCode("EndPagePrinter failed on printer \"" + name + "\"", code);
    }
    if (!writer.endDocument())
    {
        DWORD code = GetLastError();
        return errorWithCode("EndDocPrinter failed on printer \"" + name + "\"", code);
    }

    jobId = static_cast<int>(writer.jobId());
    return std::string();
}

std::vector<std::string> WinPrinterManager::getSupportedPrintFormats()
{
    return winSupportedPrintFormats();
}

std::vector<std::string> WinPrinterManager::getSupportedJobCommands()
{
    return allJobCommands();
}

} // namespace

PrinterManager *createPrinterManager()
{
    return new WinPrinterManager();
}

#endif // _WIN32
