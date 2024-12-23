#include <napi.h>

// Función simple que devuelve el doble de un número
Napi::Number Double(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsNumber()) {
        Napi::TypeError::New(env, "Se espera un número como argumento").ThrowAsJavaScriptException();
        return Napi::Number::New(env, 0);
    }

    double value = info[0].As<Napi::Number>().DoubleValue();
    return Napi::Number::New(env, value * 2);
}

// Inicialización del módulo
Napi::Object Init(Napi::Env env, Napi::Object exports) {
    exports.Set(Napi::String::New(env, "double"), Napi::Function::New(env, Double));
    return exports;
}

NODE_API_MODULE(addon, Init)
