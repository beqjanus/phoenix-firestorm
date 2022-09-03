# -*- cmake -*-

set(PYTHONINTERP_FOUND)

if (WINDOWS)
  # On Windows, explicitly avoid Cygwin Python.

  if (DEFINED ENV{VIRTUAL_ENV})
    find_program(PYTHON_EXECUTABLE
      NAMES python.exe
      PATHS
      "$ENV{VIRTUAL_ENV}\\scripts"
      NO_DEFAULT_PATH
      )
  else()
    find_program(PYTHON_EXECUTABLE
      NAMES python3.exe
      )
  endif()
    include(FindPythonInterp)
else()
  find_program(PYTHON_EXECUTABLE python3)

  if (PYTHON_EXECUTABLE)
    set(PYTHONINTERP_FOUND ON)
  endif (PYTHON_EXECUTABLE)
endif (WINDOWS)

if (NOT PYTHON_EXECUTABLE)
  message(FATAL_ERROR "No Python interpreter found")
endif (NOT PYTHON_EXECUTABLE)

mark_as_advanced(PYTHON_EXECUTABLE)
