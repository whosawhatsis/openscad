#pragma once

#include <string>

/*!
   One message on a compute worker's channel.

   Deliberately in a header of its own, with no dependencies. Both ends of the channel and the
   worker's own main loop need this type, but only the framing needs Boost.Asio -- and on Windows,
   pulling Asio into a translation unit that has already included <windows.h> fails outright with
   "WinSock.h has already been included", because <windows.h> brings in Winsock 1 and Asio wants
   Winsock 2. Keeping the vocabulary type separate is what lets openscad.cc use the channel without
   taking on that constraint.

   A message is named because a single preview sends several: the product list plus one payload per
   distinct leaf mesh, with the list referring to the leaves by name.
 */
struct IpcMessage {
  std::string name;
  std::string payload;
};
