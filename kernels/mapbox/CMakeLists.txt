set(srcs MapboxKernel.cpp)
set(incs MapboxKernel.hpp)

PDAL_ADD_DRIVER(kernel mapbox "${srcs}" "${incs}" objects)
set(PDAL_TARGET_OBJECTS ${PDAL_TARGET_OBJECTS} ${objects} PARENT_SCOPE)
