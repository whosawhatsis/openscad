// Tests for run_command_line(), the in-process entry point behind the Advanced Export spike.
//
// The point of that function is to evaluate a command line exactly as a CLI invocation would,
// but inside a running process (the GUI, or a worker) so that the warm geometry caches are
// reused. Two properties matter enough to pin down with tests:
//
//   1. it really runs the normal pipeline, producing the same output a CLI run would; and
//   2. it reports bad input by *returning* rather than by calling exit(), because the CLI's
//      argument helpers exit() directly and that would terminate the GUI on a typo.
//
// Property 2 is partly self-demonstrating: if it regresses, this test binary dies instead of
// failing, which is a loud enough signal.

#include <catch2/catch_all.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "command_line.h"
#include "core/Builtins.h"
#include "geometry/GeometryCache.h"
#include "platform/PlatformUtils.h"

namespace fs = std::filesystem;

namespace {

/*!
   run_command_line() expects to run inside an already-initialised OpenSCAD process. Both
   openscad_main() and the GUI register the application path and initialise the builtins
   before anything parses a file; a bare test binary has done neither, so stand in for a real
   host here rather than making run_command_line() guess at process setup it does not own.

   This is itself a finding of the Advanced Export spike: the function is only usable from a
   fully initialised process, which the GUI is.
 */
void ensureProcessInitialised()
{
  static const bool once = [] {
    // std::filesystem, to avoid dragging boost::filesystem's link deps into the test binary.
    PlatformUtils::registerApplicationPath(fs::current_path().string());
    Builtins::initialize();
    return true;
  }();
  (void)once;
}

//! Writes a trivial model to a temp file and returns its path.
fs::path writeScad(const std::string& name, const std::string& contents)
{
  ensureProcessInitialised();
  const auto path = fs::temp_directory_path() / name;
  std::ofstream out(path);
  out << contents;
  out.close();
  return path;
}

}  // namespace

TEST_CASE("run_command_line exports a file just as the CLI would", "[commandline]")
{
  const auto input = writeScad("advexport_basic.scad", "cube([1, 2, 3]);\n");
  const auto output = fs::temp_directory_path() / "advexport_basic.off";
  fs::remove(output);

  std::string error;
  const int rc = run_command_line({"openscad", "-o", output.string(), input.string()}, error);

  CAPTURE(error);
  REQUIRE(error.empty());
  REQUIRE(rc == 0);
  REQUIRE(fs::exists(output));
  REQUIRE(fs::file_size(output) > 0);
}

TEST_CASE("run_command_line reports a bad option instead of exiting", "[commandline]")
{
  // If this regresses to exit(), the test binary terminates here rather than failing --
  // which is exactly the GUI failure mode this function exists to avoid.
  ensureProcessInitialised();
  std::string error;
  const int rc = run_command_line({"openscad", "--no-such-option", "whatever.scad"}, error);

  REQUIRE(rc != 0);
  REQUIRE_FALSE(error.empty());
}

TEST_CASE("run_command_line reports a malformed option value instead of exiting", "[commandline]")
{
  const auto input = writeScad("advexport_badarg.scad", "cube(1);\n");
  const auto output = fs::temp_directory_path() / "advexport_badarg.png";

  // --imgsize wants w,h. The CLI helper for this calls exit(1) on malformed input.
  std::string error;
  const int rc =
    run_command_line({"openscad", "--imgsize=not-a-size", "-o", output.string(), input.string()}, error);

  REQUIRE(rc != 0);
  REQUIRE_FALSE(error.empty());
}

TEST_CASE("run_command_line accepts the options the export spike depends on", "[commandline]")
{
  const auto input = writeScad("advexport_opts.scad", "cube([$size, $size, $size]);\n");
  const auto output = fs::temp_directory_path() / "advexport_opts.off";
  fs::remove(output);

  // -D is the interesting one: it is the option that invalidates cached geometry, so the
  // spike needs it to work in order to explore where the cache boundary actually falls.
  std::string error;
  const int rc =
    run_command_line({"openscad", "-D", "$size=4", "-o", output.string(), input.string()}, error);

  CAPTURE(error);
  REQUIRE(error.empty());
  REQUIRE(rc == 0);
  REQUIRE(fs::exists(output));
}

TEST_CASE("run_command_line can be called repeatedly in one process", "[commandline]")
{
  // The whole premise of the spike is repeated in-process invocation against warm caches,
  // so a second call must behave like the first rather than tripping over global state left
  // behind by it.
  const auto input = writeScad("advexport_twice.scad", "sphere(r = 2, $fn = 12);\n");
  const auto first = fs::temp_directory_path() / "advexport_twice_1.off";
  const auto second = fs::temp_directory_path() / "advexport_twice_2.off";
  fs::remove(first);
  fs::remove(second);

  std::string error;
  const int rc1 = run_command_line({"openscad", "-o", first.string(), input.string()}, error);
  CAPTURE(error);
  REQUIRE(rc1 == 0);
  REQUIRE(run_command_line({"openscad", "-o", second.string(), input.string()}, error) == 0);

  REQUIRE(fs::exists(first));
  REQUIRE(fs::exists(second));
  REQUIRE(fs::file_size(first) == fs::file_size(second));
}

TEST_CASE("a second identical run is served from the warm geometry cache", "[commandline]")
{
  // This is the premise of the whole Advanced Export spike: running in-process reuses what has
  // already been evaluated, instead of starting from zero the way a fresh CLI process must.
  // GeometryCache is a process-global singleton keyed on the node tree's id string, so an
  // unchanged model must not add new entries on a second run.
  const auto input = writeScad("advexport_cache.scad", "sphere(r = 3, $fn = 24);\n");
  const auto out1 = fs::temp_directory_path() / "advexport_cache_1.off";
  const auto out2 = fs::temp_directory_path() / "advexport_cache_2.off";

  std::string error;
  REQUIRE(run_command_line({"openscad", "-o", out1.string(), input.string()}, error) == 0);
  const size_t afterFirst = GeometryCache::instance()->size();
  REQUIRE(afterFirst > 0);

  REQUIRE(run_command_line({"openscad", "-o", out2.string(), input.string()}, error) == 0);
  const size_t afterSecond = GeometryCache::instance()->size();

  // Same model, same node ids: every lookup is a hit, so nothing new is cached.
  CHECK(afterSecond == afterFirst);
}

TEST_CASE("changing a -D variable misses the cache only for what it affects", "[commandline]")
{
  // The other half of the premise: reuse is partial, not all-or-nothing. Passing a different
  // -D value must add entries (the changed subtree is genuinely recomputed) rather than either
  // reusing stale geometry or invalidating everything.
  const auto input = writeScad("advexport_partial.scad", "sphere(r = $r, $fn = 24); cube([1, 1, 1]);\n");
  const auto out1 = fs::temp_directory_path() / "advexport_partial_1.off";
  const auto out2 = fs::temp_directory_path() / "advexport_partial_2.off";

  std::string error;
  REQUIRE(run_command_line({"openscad", "-D", "$r=5", "-o", out1.string(), input.string()}, error) == 0);
  const size_t afterFirst = GeometryCache::instance()->size();

  REQUIRE(run_command_line({"openscad", "-D", "$r=7", "-o", out2.string(), input.string()}, error) == 0);
  const size_t afterSecond = GeometryCache::instance()->size();

  CHECK(afterSecond > afterFirst);
  // ...and the two exports genuinely differ, i.e. the cache did not serve stale geometry.
  CHECK(fs::file_size(out1) != fs::file_size(out2));
}

TEST_CASE("run_command_lines executes independent command strings with one warm cache", "[commandline]")
{
  const auto input = writeScad("batch export input.scad", "sphere(r = 3, $fn = 24);\n");
  const auto first = fs::temp_directory_path() / "batch export first.off";
  const auto second = fs::temp_directory_path() / "batch export second.off";
  fs::remove(first);
  fs::remove(second);

  const auto quote = [](const fs::path& path) { return "\"" + path.string() + "\""; };
  std::string error;
  const int rc = run_command_lines(
    {"-o " + quote(first) + " " + quote(input), "-o " + quote(second) + " " + quote(input)}, error);

  CAPTURE(error);
  REQUIRE(rc == 0);
  REQUIRE(error.empty());
  REQUIRE(fs::exists(first));
  REQUIRE(fs::exists(second));
  CHECK(fs::file_size(first) == fs::file_size(second));
}

TEST_CASE("run_command_lines expands newlines, files, and stdin into commands", "[commandline]")
{
  const auto input = writeScad("batch_sources.scad", "cube(2);\n");
  const auto quote = [](const fs::path& path) { return "\"" + path.string() + "\""; };
  const auto command = [&](const fs::path& output) {
    fs::remove(output);
    return "-o " + quote(output) + " " + quote(input);
  };
  std::string error;

  const auto literalFirst = fs::temp_directory_path() / "batch_literal_first.off";
  const auto literalSecond = fs::temp_directory_path() / "batch_literal_second.off";
  REQUIRE(run_command_lines({command(literalFirst) + "\n" + command(literalSecond)}, error) == 0);
  CHECK(fs::exists(literalFirst));
  CHECK(fs::exists(literalSecond));

  const auto fileOutput = fs::temp_directory_path() / "batch_file.off";
  const auto commandFile = fs::temp_directory_path() / "openscad_batch_commands.txt";
  std::ofstream(commandFile) << command(fileOutput) << '\n';
  REQUIRE(run_command_lines({commandFile.string()}, error) == 0);
  CHECK(fs::exists(fileOutput));

  const auto stdinOutput = fs::temp_directory_path() / "batch_stdin.off";
  std::istringstream stdinCommands(command(stdinOutput) + "\n");
  auto *oldStdin = std::cin.rdbuf(stdinCommands.rdbuf());
  const int stdinRc = run_command_lines({"-"}, error);
  std::cin.rdbuf(oldStdin);
  REQUIRE(stdinRc == 0);
  CHECK(fs::exists(stdinOutput));
}
