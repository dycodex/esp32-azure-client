#ifndef DYCODEX_ESP32_AZURE_CALLBACK_H_
#define DYCODEX_ESP32_AZURE_CALLBACK_H_

// Credits to Victor.dMdB for this code
// Original stackoverflow answer: https://stackoverflow.com/a/19809787

#include <functional>

template <typename T>
struct Callback;

template <typename Ret, typename... Params>
struct Callback<Ret(Params...)> {
    template <typename... Args>
    static Ret callback(Args... args) { return func(args...); }
    static std::function<Ret(Params...)> func;
};

// Initialize the static member.
template <typename Ret, typename... Params>
std::function<Ret(Params...)> Callback<Ret(Params...)>::func;
#endif // DYCODEX_ESP32_AZURE_CALLBACK_H_