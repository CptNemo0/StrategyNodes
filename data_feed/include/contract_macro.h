#ifndef DATA_FEED_CONTRACT_MACRO_H_
#define DATA_FEED_CONTRACT_MACRO_H_

// Macro indirection created to circumvent clang d errors. LLVM foundation is
// lagging behind on C++26 implementation in the middle of 2026 smh...
// "Just ChatGPT it bro."

#ifdef __cpp_contracts

#include <contracts>

#define PRE(...) pre(__VA_ARGS__)
#define POST(...) post(__VA_ARGS__)

#else

#define PRE(...)
#define POST(...)

#endif  // ! __cpp_contracts

#endif  // ! DATA_FEED_CONTRACT_MACRO_H_
