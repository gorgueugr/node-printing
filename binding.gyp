{
    "targets": [
        {
            "target_name": "nodeprinting",
            "sources": [
                "src/node_printer.cpp",
                "src/win/WinPrinterManager.cpp",
                "src/posix/PosixPrinterManager.cpp"
            ],
            "include_dirs": [
                "<!(node -p \"require('node-addon-api').include_dir\")"
            ],
            "dependencies": [
                "<!(node -p \"require('node-addon-api').targets\"):node_addon_api_except"
            ],
            "defines": [
                "NAPI_VERSION=8",
                "NAPI_CPP_EXCEPTIONS"
            ],
            "conditions": [
                [
                    "OS==\"win\"",
                    {
                        "libraries": [
                            "Winspool.lib",
                            "Gdi32.lib"
                        ],
                        "sources/": [
                            [
                                "exclude",
                                "src/posix/PosixPrinterManager.cpp"
                            ]
                        ],
                        "msvs_settings": {
                            "VCCLCompilerTool": {
                                "AdditionalOptions": [
                                    "/utf-8"
                                ],
                                "ExceptionHandling": 1,
                                "WarningLevel": 3,
                                "Optimization": 2,
                                "RuntimeLibrary": 0
                            }
                        }
                    }
                ],
                [
                    "OS!=\"win\"",
                    {
                        "include_dirs": [
                            "<!@(cups-config --cflags)"
                        ],
                        "libraries": [
                            "<!@(cups-config --libs)"
                        ],
                        "cflags": [
                            "-Wall"
                        ],
                        "cflags_cc": [
                            "-std=c++17",
                            "-Wall"
                        ],
                        "cflags_cc!": [
                            "-fno-exceptions"
                        ],
                        "sources/": [
                            [
                                "exclude",
                                "src/win/WinPrinterManager.cpp"
                            ]
                        ],
                        "xcode_settings": {
                            "GCC_ENABLE_CPP_EXCEPTIONS": "YES",
                            "CLANG_CXX_LIBRARY": "libc++",
                            "CLANG_CXX_LANGUAGE_STANDARD": "c++17",
                            "MACOSX_DEPLOYMENT_TARGET": "10.15"
                        }
                    }
                ]
            ]
        }
    ]
}
