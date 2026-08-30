#pragma once

#include <cstddef>
#include <iosfwd>
#include <memory>
#include <string>

class Geometry;
class PolySet;

// Binary geometry transport between a window and its private compute worker.
//
// Native byte order, native doubles, no versioned compatibility promise, and no attempt at
// portability: the writer and reader are always the same executable on the same machine, since the
// worker is launched from the parent's own path. That is what makes the raw layout safe, and it is
// also why nothing here pins byte order or struct layout -- a change to either lands in both ends
// at once. Do not reuse this format for anything a user can keep or move between machines; that is
// what the OFF/3MF exporters are for.
//
// Measured against a full-precision ASCII OFF payload, this is 44-94x faster per payload.
//
// Everything a PolySet or Polygon2d carries has to survive the round trip. Anything dropped here is
// a visible regression against evaluating the geometry in-process, not a smaller first version --
// per-face color and convexity in particular, which cost a few lines each and are what make a
// preview look right.

// Writer side matches the other exporters (geometry in, ostream out, nothing returned) so it can be
// reached through the ordinary FileFormat dispatch.
void export_ipc_geometry(const std::shared_ptr<const Geometry>& geom, std::ostream& output);

/*!
   Decodes a payload from bytes already in memory. `name` only says which payload failed.
   Returns a bare PolySet or Polygon2d for a single body, a GeometryList when the payload carries
   several, and null for a payload that is truncated, corrupt, or not one of ours.
 */
std::shared_ptr<const Geometry> import_ipc_geometry_buffer(const char *data, std::size_t size,
                                                           const std::string& name);

/*!
   The single-body case, decoded without the list wrapper so the caller keeps a mutable PolySet it
   can adjust and no mesh has to be copied. Preview leaves are always written as a single body.
   Null if the payload is unusable or carries anything other than one PolySet.
 */
std::unique_ptr<PolySet> import_ipc_polyset_buffer(const char *data, std::size_t size,
                                                   const std::string& name);
