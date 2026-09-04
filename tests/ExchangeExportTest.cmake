# Runs a command-line export and checks the file it was supposed to write.
#
# The failure this guards against is silent: when the exporter is handed a mesh instead of
# the retained B-Rep it writes nothing at all and still exits successfully, so checking the
# process output alone would not notice. Any stale file is removed first for the same reason.
file(REMOVE "${OUTPUT}")
execute_process(COMMAND "${OPENSCAD}" --backend=opencascade -o "${OUTPUT}" "${INPUT}"
                RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "OpenSCAD exited with ${status}\n${stdout}\n${stderr}")
endif()
if(NOT EXISTS "${OUTPUT}")
  message(FATAL_ERROR "no file was written to ${OUTPUT}\n${stdout}\n${stderr}")
endif()
file(SIZE "${OUTPUT}" written)
if(written EQUAL 0)
  message(FATAL_ERROR "${OUTPUT} is empty")
endif()
if(DEFINED MARKER)
  file(READ "${OUTPUT}" contents)
  if(NOT contents MATCHES "${MARKER}")
    message(FATAL_ERROR "${OUTPUT} does not contain ${MARKER}: the geometry was tessellated "
                        "before it reached the exchange writer")
  endif()
endif()
