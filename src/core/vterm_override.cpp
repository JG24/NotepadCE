/*
  Overrides libstdc++'s default terminate handler. The stock
  __verbose_terminate_handler prints the demangled name of an uncaught
  exception to stderr, which pulls the entire C++ demangler (~57 KB) into
  the static link — and a -mwindows GUI app has no stderr to print to.
  Aborting directly keeps the process behaviour identical minus the
  invisible message. try/catch and all exception handling are unaffected;
  this only replaces the handler run after an exception nobody caught.
*/

#ifdef __GNUC__

#include <cstdlib>

namespace __gnu_cxx
{
void __verbose_terminate_handler();
}

void __gnu_cxx::__verbose_terminate_handler()
{
    std::abort();
}

#endif
