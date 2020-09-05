Build and install
=================

To build libosdp you must have cmake-3.0 (or above) and a C compiler installed.
This repository produces a ``libosdpstatic.a`` and ``libosdp.so``. You can link
these with your application as needed (-losdp or -losdpstatic). Have a look at
``sample/*`` for details on how to consume this library.

.. code:: sh

    mkdir build && cd build
    cmake ..
    make
    make check
    make DESTDIR=/your/install/path install

Compile-time configuration options
----------------------------------

LibOSDP can can be configured to enable/disable certain featured by passing the
``-DCONFIG_XXX=ON/OFF`` flag to cmake. Following table lists all such config
switches. For instance, if you want to also build static library, you can pass
the flag ``-DCONFIG_OSDP_BUILD_STATIC=ON`` to cmake.

+-------------------------------+-----------+-------------------------------------------+
| OPTION                        | Default   | Description                               |
+===============================+===========+===========================================+
| CONFIG\_OSDP\_PACKET\_TRACE   | OFF       | Enable raw packet trace for diagnostics   |
+-------------------------------+-----------+-------------------------------------------+
| CONFIG_OSDP_SC_ENABLE         | ON        | Enable secure channel communication       |
+-------------------------------+-----------+-------------------------------------------+

Add LibOSDP to your cmake project
---------------------------------

If you are familiar with cmake, then adding LibOSDP to your project is
super simple. First off, add the following to your CMakeLists.txt

.. code:: cmake

    include(ExternalProject)
    ExternalProject_Add(ext_libosdp
        GIT_REPOSITORY    https://github.com/cbsiddharth/libosdp.git
        GIT_TAG           master
        SOURCE_DIR        ${CMAKE_BINARY_DIR}/libosdp/src
        BINARY_DIR        ${CMAKE_BINARY_DIR}/libosdp/build
        CONFIGURE_COMMAND cmake ${CMAKE_BINARY_DIR}/libosdp/src
        BUILD_COMMAND     make
        INSTALL_COMMAND   make install DESTDIR=${CMAKE_BINARY_DIR}/libosdp/install
    )
    include_directories("${CMAKE_BINARY_DIR}/libosdp/install/include")
    link_directories("${CMAKE_BINARY_DIR}/libosdp/install/lib")

Next you must add ``ext_libosdp`` as a dependency to your target. That
it! now you can link your application to osdp library. Following example shows
how you can do this.

.. code:: cmake

    set(OSDP_APP osdp-app)
    list(APPEND OSDP_APP_SRC
        "src/main.c"
        "src/more_source_files.c"
        ...
    )
    add_executable(${OSDP_APP} ${OSDP_APP_SRC})
    add_dependencies(${OSDP_APP} ext_libosdp)
    target_link_libraries(${OSDP_APP} osdp)
