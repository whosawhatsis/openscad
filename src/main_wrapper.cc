#include "openscad.h"

#include <cstdio>

#include "command_line.h"

#ifndef OPENSCAD_NOGUI
#include <QtCore/qresource.h>  // Bring in Q_INIT_RESOURCE
#endif

// Windows note:  wmain() is called first, translates from UTF-16 to UTF-8, and calls main().
int main(int argc, char **argv)
{
  // Note: when compiled directly into an executable, the static assignment causes these to be
  // initialized. But that doesn't get called when included in a library. So we must manually add an
  // entry for every qrc added as a target library.
#ifndef OPENSCAD_NOGUI
  Q_INIT_RESOURCE(common);
  Q_INIT_RESOURCE(icons_chokusen);
  Q_INIT_RESOURCE(icons_chokusen_dark);
#ifdef __APPLE__
  Q_INIT_RESOURCE(mac);
#endif
#endif
  /*
     Argument errors are raised rather than exit()ed, so that the same parsing code can be
     driven from inside a running process without killing it (see command_line.h). The CLI's
     own behaviour is unchanged: report the message and exit 1.
   */
  try {
    return openscad_main(argc, argv);
  } catch (const CommandLineError& e) {
    fprintf(stderr, "%s\n", e.what());
    return 1;
  }
}

#ifdef _WIN32

#include <boost/nowide/convert.hpp>
#include <cstddef>
#include <string>

// wmain gets arguments as wide character strings, which is the way that Windows likes to provide
// non-ASCII arguments.  Convert them to UTF-8 strings and call the traditional main().
int wmain(int argc, wchar_t **argv)
{
  char *argv8[argc + 1];
  std::string argvString[argc];

  for (int i = 0; i < argc; i++) {
    argvString[i] = boost::nowide::narrow(argv[i]);
    argv8[i] = argvString[i].data();
  }
  argv8[argc] = NULL;

  return (main(argc, argv8));
}
#endif
