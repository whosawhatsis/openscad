#pragma once

#include <stdexcept>
#include <string>
#include <vector>

/*!
   Thrown instead of calling exit() when a command line cannot be understood.

   The CLI's argument handling historically called exit(1) directly on bad input. That is fine
   for a process whose only job is that one command line, but it terminates the whole
   application when the same code is driven from inside a running GUI. Throwing lets main()
   keep its exact previous behaviour (catch, report, exit 1) while an in-process caller can
   recover and show the message.
 */
class CommandLineError : public std::runtime_error
{
public:
  explicit CommandLineError(const std::string& what) : std::runtime_error(what) {}
};

/*!
   Runs a command line in the current process, exactly as if it had been invoked from a shell,
   and returns the exit code it would have produced. `args` includes argv[0].

   Because this shares the process, it also shares the geometry caches: any subtree whose node
   id string is unchanged is served from GeometryCache/CGALCache rather than recomputed. That
   is the point of the function -- an export driven from the GUI reuses whatever the GUI has
   already evaluated, instead of starting from zero the way a fresh CLI process must.

   On failure, `error` receives a human-readable message and the return value is non-zero.
   Nothing here calls exit().

   NOTE (spike): this mutates process-global state the way the CLI does -- feature flags from
   --enable, the geometry backend from --backend, library paths and the working directory. A
   caller that intends to keep running afterwards is responsible for whether it can tolerate
   that. See the feature page for row 02.
 */
int run_command_line(const std::vector<std::string>& args, std::string& error);

/*!
   Runs shell-style command strings sequentially in the current process. Each string is an
   independent OpenSCAD invocation without argv[0]. Execution stops at the first failure.
 */
int run_command_lines(const std::vector<std::string>& commands, std::string& error);
