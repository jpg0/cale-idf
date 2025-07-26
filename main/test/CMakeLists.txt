set(COMPONENT_SRCS "test_battery_utils.cpp" "../battery_utils.cpp") # Include the .cpp to test its functions
set(COMPONENT_ADD_INCLUDEDIRS "..")
idf_component_register(SRCS "${COMPONENT_SRCS}"
                       INCLUDE_DIRS "${COMPONENT_ADD_INCLUDEDIRS}"
                       REQUIRES unity)
