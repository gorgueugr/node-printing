#include "../PrinterManager.hpp"
#include <windows.h>

// template <typename Type>
// class MemValue
// {
// public:
//     MemValue(const DWORD iSizeKbytes)
//     {
//         _value = (Type *)malloc(iSizeKbytes);
//     }

//     ~MemValue()
//     {
//         free();
//     }

//     Type *get() { return _value; }
//     operator bool() { return (_value != NULL); }

// protected:
//     Type *_value;
//     virtual void free()
//     {
//         if (_value != NULL)
//         {
//             ::free(_value);
//             _value = NULL;
//         }
//     }
// };

class WinPrinterManager : public PrinterManager
{
public:
    std::string getDefaultPrinterName();
};
