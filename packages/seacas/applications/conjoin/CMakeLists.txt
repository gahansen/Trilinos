INCLUDE(TribitsSubPackageMacros)
INCLUDE(TribitsAddExecutable)
INCLUDE(TribitsLibraryMacros)

TRIBITS_SUBPACKAGE(Conjoin)

INCLUDE_DIRECTORIES(
  "${CMAKE_CURRENT_SOURCE_DIR}"
)

SET(CODE conjoin)
SET(HEADERS "")
APPEND_GLOB(SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/*.C)

TRIBITS_ADD_EXECUTABLE(
  ${CODE}
  NOEXEPREFIX
  NOEXESUFFIX
  SOURCES ${SOURCES}
  COMM serial mpi
  )
install_executable(${CODE})

TRIBITS_SUBPACKAGE_POSTPROCESS()

