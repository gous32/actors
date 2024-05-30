#ifndef STATIC_ANALYSIS_INL_H_
#error "Direct inclusion of this file is not allowed, include static_analysis.h"
// For the sake of sane code completion.
#include "static_analysis.h"
#endif

#include <library/cpp/yt/misc/preprocessor.h>

#include <library/cpp/yt/string/format_analyser.h>

#include <string_view>
#include <variant> // monostate

namespace NYT::NLogging::NDetail {

////////////////////////////////////////////////////////////////////////////////

// Tag for dispatching proper TFormatArg specialization.
template <class T>
struct TLoggerFormatArg
{ };

////////////////////////////////////////////////////////////////////////////////

// Stateless constexpr way of capturing arg types
// without invoking any ctors. With the help of macros
// can turn non-constexpr argument pack of arguments
// into constexpr pack of types.
template <class... TArgs>
struct TLoggerFormatArgs
{ };

// Used for macro conversion. Purposefully undefined.
template <class... TArgs>
TLoggerFormatArgs<TArgs...> AsFormatArgs(TArgs&&...);

////////////////////////////////////////////////////////////////////////////////

template <bool First, bool Second>
struct TAnalyserDispatcher
{
    template <class... TArgs>
    static consteval void Do(std::string_view, std::string_view, TLoggerFormatArgs<TArgs...>)
    {
        // Give up :(
        // We can't crash here, because, for example, YT_LOG_ERROR(error) exists
        // and we can't really check if error is actually TError or something else here.
        // and probably shouldn't bother trying.
    }
};

template <bool Second>
struct TAnalyserDispatcher<true, Second>
{
    template <class TFirst, class... TArgs>
    static consteval void Do(std::string_view str, std::string_view, TLoggerFormatArgs<TFirst, TArgs...>)
    {
        // Remove outer \"'s generated by PP_STRINGIZE.
        auto stripped = std::string_view(std::begin(str) + 1, std::size(str) - 2);
        ::NYT::NDetail::TFormatAnalyser::ValidateFormat<TLoggerFormatArg<TArgs>...>(stripped);
    }
};

template <>
struct TAnalyserDispatcher<false, true>
{
    template <class TFirst, class TSecond, class... TArgs>
    static consteval void Do(std::string_view, std::string_view str, TLoggerFormatArgs<TFirst, TSecond, TArgs...>)
    {
        // Remove outer \"'s generated by PP_STRINGIZE.
        auto stripped = std::string_view(std::begin(str) + 1, std::size(str) - 2);
        ::NYT::NDetail::TFormatAnalyser::ValidateFormat<TLoggerFormatArg<TArgs>...>(stripped);
    }
};

////////////////////////////////////////////////////////////////////////////////

// This value is never read since homogenization works for unevaluated expressions.
inline constexpr auto InvalidToken = std::monostate{};

////////////////////////////////////////////////////////////////////////////////

#define PP_VA_PICK_1_IMPL(N, ...) N
#define PP_VA_PICK_2_IMPL(_1, N, ...) N

////////////////////////////////////////////////////////////////////////////////

//! Parameter pack parsing.

#define STATIC_ANALYSIS_CAPTURE_TYPES(...) \
    decltype(::NYT::NLogging::NDetail::AsFormatArgs(__VA_ARGS__)){}

#define STATIC_ANALYSIS_FIRST_TOKEN(...) \
    PP_STRINGIZE( \
        PP_VA_PICK_1_IMPL(__VA_ARGS__ __VA_OPT__(,) ::NYT::NLogging::NDetail::InvalidToken))

#define STATIC_ANALYSIS_SECOND_TOKEN(...) \
    PP_STRINGIZE(\
        PP_VA_PICK_2_IMPL( \
            __VA_ARGS__ __VA_OPT__(,) \
            ::NYT::NLogging::NDetail::InvalidToken, \
            ::NYT::NLogging::NDetail::InvalidToken))

#define STATIC_ANALYSIS_FIRST_TOKEN_COND(...) \
    STATIC_ANALYSIS_FIRST_TOKEN(__VA_ARGS__)[0] == '\"'

#define STATIC_ANALYSIS_SECOND_TOKEN_COND(...) \
    STATIC_ANALYSIS_SECOND_TOKEN(__VA_ARGS__)[0] == '\"'

#undef STATIC_ANALYSIS_CHECK_LOG_FORMAT
#define STATIC_ANALYSIS_CHECK_LOG_FORMAT(...) \
    ::NYT \
    ::NLogging \
    ::NDetail \
    ::TAnalyserDispatcher< \
        STATIC_ANALYSIS_FIRST_TOKEN_COND(__VA_ARGS__), \
        STATIC_ANALYSIS_SECOND_TOKEN_COND(__VA_ARGS__) \
    >::Do( \
        STATIC_ANALYSIS_FIRST_TOKEN(__VA_ARGS__), \
        STATIC_ANALYSIS_SECOND_TOKEN(__VA_ARGS__), \
        STATIC_ANALYSIS_CAPTURE_TYPES(__VA_ARGS__))

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NLogging::NDetail

template <class T>
struct NYT::TFormatArg<NYT::NLogging::NDetail::TLoggerFormatArg<T>>
    : public NYT::TFormatArgBase
{
    // We mix in '\"' and ' ' which is an artifact of logging stringize.
    // We want to support YT_LOG_XXX("Value: %" PRIu64, 42)
    // for plantform independent prints of numbers.
    // String below may be converted to a token:
    // "\"Value: %\" \"u\""
    // Thus adding a \" \" sequence.
    static constexpr auto FlagSpecifiers
        = TFormatArgBase::ExtendFlags</*Hot*/ false, 2, std::array{'\"', ' '}, /*TFrom*/ NYT::TFormatArg<T>>();
};
