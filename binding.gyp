{
  "targets": [
    {
      "target_name": "nodeprinting",
      "sources": [
        "src/node_printer.hpp",
        
        "src/node_printer_win.cpp"
      ],
      "include_dirs": [
        "<!(node -p \"require('node-addon-api').include\")"
      ],
       'dependencies': [
        "<!(node -p \"require('node-addon-api').targets\"):node_addon_api",
    ],
       'defines': [ 'NAPI_DISABLE_CPP_EXCEPTIONS' ],
      "conditions": [
        ['OS=="win"', {
          "libraries": [
            "-lwinspool.lib"
          ]
        }]
      ],
      'cflags!': [ '-fno-exceptions' ],
      'cflags_cc!': [ '-fno-exceptions' ],
      'xcode_settings': {
        'GCC_ENABLE_CPP_EXCEPTIONS': 'YES',
        'CLANG_CXX_LIBRARY': 'libc++',
        'MACOSX_DEPLOYMENT_TARGET': '10.7'
      },
      'msvs_settings': {
        'VCCLCompilerTool': { 'ExceptionHandling': 1 },
      }
    }
  ]
}
