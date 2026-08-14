#ifndef SOFTFP_WRAPPERS_HPP
#define SOFTFP_WRAPPERS_HPP

#include <cstdint>

#define SOFTFP_ABI __attribute__((pcs("aapcs")))

template <typename T> struct is_fp { static constexpr bool value = false; };
template <> struct is_fp<float> { static constexpr bool value = true; };
template <> struct is_fp<double> { static constexpr bool value = true; };

template <typename... Ts>
constexpr bool any_fp_v = (is_fp<Ts>::value || ...);

template <auto Func> struct SoftfpWrap;

template <typename Ret, typename... Args, Ret (*Func)(Args...)>
struct SoftfpWrap<Func>
{
    static constexpr bool needs_wrap = any_fp_v<Ret, Args...>;

    static SOFTFP_ABI Ret wrapped(Args... args)
    {
        return Func(args...);
    }

    static constexpr uintptr_t get()
    {
        if constexpr (needs_wrap)
            return (uintptr_t)&wrapped;
        return (uintptr_t)Func;
    }
};

#define WRAP(func) SoftfpWrap<func>::get()
#define SOFTFP_CALL(type, addr) ((SOFTFP_ABI type)(addr))
#define SOFTFP_FN(ret, name, ...) ret(SOFTFP_ABI *name)(__VA_ARGS__)

#endif
