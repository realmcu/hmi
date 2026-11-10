file(GLOB SOURCES "*.c" "*.cpp")
file(GLOB HEADERS "*.h" "*.hpp")

if(SOURCES)
    target_sources(${APP_TARGET_NAME} PRIVATE ${SOURCES})
endif()

target_include_directories(${APP_TARGET_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${HMI_APP_ROOT}/../ui/src/user
)
