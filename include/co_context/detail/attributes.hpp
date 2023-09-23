#pragma once

#define CO_CONTEXT_AWAIT_HINT nodiscard("Did you forget to co_await?")

#if defined(__GNUG__)
#define CO_CONTEXT_NOINLINE gnu::noinline
#elif defined(__clang__)
#define CO_CONTEXT_NOINLINE clang::noinline
#else
#define CO_CONTEXT_NOINLINE gnu::noinline, clang::noinline, msvc::noinline
#endif
