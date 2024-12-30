{
    "targets": [
        {
            "target_name": "nodeprinting",
            "sources": [
                "src/node_printer.hpp",
                "src/PrinterManager.hpp",
                "src/node_printer.cpp",
                "src/win/WinPrinterManager.cpp",
                "src/posix/PosixPrinterManager.cpp",
            ],
            "include_dirs": ["<!(node -p \"require('node-addon-api').include\")"],
            "dependencies": [
                "<!(node -p \"require('node-addon-api').targets\"):node_addon_api"
            ],
            "conditions": [
                [
                    'OS=="win"',
                    {
                        "libraries": ["-lwinspool.lib"],
                        "sources/": [["exclude", "src/posix/PosixPrinterManager.cpp"]],
                    },
                ],
                [
                    'OS=="linux"',
                    {
                        "libraries": ["-lcups", "-lcupsppdc"],
                        "sources/": [["exclude", "src/win/WinPrinterManager.cpp"]],
                    },
                ],
            ],
            "cflags!": ["-fno-exceptions"],
            "cflags_cc!": ["-fno-exceptions"],
            "xcode_settings": {
                "GCC_ENABLE_CPP_EXCEPTIONS": "YES",
                "CLANG_CXX_LIBRARY": "libc++",
                "MACOSX_DEPLOYMENT_TARGET": "10.7",
            },
            "msvs_settings": {"VCCLCompilerTool": {"ExceptionHandling": 1}},
        }
    ]
}
