#
# Python plugin CMake configuration
#

include(${PDAL_CMAKE_DIR}/python.cmake)
include_directories(${ROOT_DIR}/vendor/pdalboost)

add_subdirectory(filters)
if (WITH_TESTS)
    PDAL_ADD_TEST(plangtest FILES ./test/PLangTest.cpp
        LINK_WITH ${PDAL_PLANG_LIB_NAME})
    if (WITH_APPS)
      PDAL_ADD_TEST(python_pipeline_test FILES ./test/PythonPipelineTest.cpp LINK_WITH ${PDAL_PLANG_LIB_NAME})
    endif()
endif()
