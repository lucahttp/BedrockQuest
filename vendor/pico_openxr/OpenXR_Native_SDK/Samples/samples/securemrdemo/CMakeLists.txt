project(mnistwild)

file(GLOB_RECURSE SRC_FILES
        cpp/*.c
        cpp/*.cpp
)

add_library(mnistwild
        MODULE
        ${SRC_FILES}
)

target_include_directories(mnistwild
        PUBLIC
        ${ANDROID_NDK}/sources/android/native_app_glue
        src
)

target_compile_options(mnistwild
        PUBLIC
        -Werror
        -Wall
        -Wextra
        -Wno-unused-parameter
        -Wno-missing-field-initializers
        -Wno-unused-function
        -Wunused-private-field
        #        -fstack-protector-all
        -fno-stack-protector
)

target_link_libraries(mnistwild
        PRIVATE
        android
        app_glue
        EGL
        GLESv3
        log
        openxr-gfxwrapper
        sampleframework
)
set_target_properties(mnistwild PROPERTIES LINK_FLAGS "-u ANativeActivity_onCreate")