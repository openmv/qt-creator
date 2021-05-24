.. _micropython_lib:

MicroPython libraries
=====================

.. warning::

   Important summary of this section

   * MicroPython implements a subset of Python functionality for each module.
   * To ease extensibility, MicroPython versions of standard Python modules
     usually have ``u`` ("micro") prefix.
   * Any particular MicroPython variant or port may miss any feature/function
     described in this general documentation (due to resource constraints or
     other limitations).


This chapter describes modules (function and class libraries) which are built
into MicroPython. There are a few categories of such modules:

* Modules which implement a subset of standard Python functionality and are not
  intended to be extended by the user.
* Modules which implement a subset of Python functionality, with a provision
  for extension by the user (via Python code).
* Modules which implement MicroPython extensions to the Python standard libraries.
* Modules specific to a particular :term:`MicroPython port` and thus not portable.

Note about the availability of the modules and their contents: This documentation
in general aspires to describe all modules and functions/classes which are
implemented in MicroPython project. However, MicroPython is highly configurable, and
each port to a particular board/embedded system makes available only a subset
of MicroPython libraries. For officially supported ports, there is an effort
to either filter out non-applicable items, or mark individual descriptions
with "Availability:" clauses describing which ports provide a given feature.

With that in mind, please still be warned that some functions/classes
in a module (or even the entire module) described in this documentation **may be
unavailable** in a particular build of MicroPython on a particular system. The
best place to find general information of the availability/non-availability
of a particular feature is the "General Information" section which contains
information pertaining to a specific :term:`MicroPython port`.

On some ports you are able to discover the available, built-in libraries that
can be imported by entering the following at the REPL::

    help('modules')

Beyond the built-in libraries described in this documentation, many more
modules from the Python standard library, as well as further MicroPython
extensions to it, can be found in `micropython-lib`.

Python standard libraries and micro-libraries
---------------------------------------------

The following standard Python libraries have been "micro-ified" to fit in with
the philosophy of MicroPython.  They provide the core functionality of that
module and are intended to be a drop-in replacement for the standard Python
library.  Some modules below use a standard Python name, but prefixed with "u",
e.g. ``ujson`` instead of ``json``. This is to signify that such a module is
micro-library, i.e. implements only a subset of CPython module functionality.
By naming them differently, a user has a choice to write a Python-level module
to extend functionality for better compatibility with CPython (indeed, this is
what done by the `micropython-lib` project mentioned above).

On some embedded platforms, where it may be cumbersome to add Python-level
wrapper modules to achieve naming compatibility with CPython, micro-modules
are available both by their u-name, and also by their non-u-name.  The
non-u-name can be overridden by a file of that name in your library path (``sys.path``).
For example, ``import json`` will first search for a file ``json.py`` (or package
directory ``json``) and load that module if it is found.  If nothing is found,
it will fallback to loading the built-in ``ujson`` module.

.. toctree::
   :maxdepth: 3

   builtins.rst
   cmath.rst
   gc.rst
   math.rst
   uarray.rst
   uasyncio.rst
   ubinascii.rst
   ucollections.rst
   uerrno.rst
   uhashlib.rst
   uheapq.rst
   uio.rst
   ujson.rst
   uos.rst
   ure.rst
   uselect.rst
   usocket.rst
   ussl.rst
   ustruct.rst
   usys.rst
   utime.rst
   uzlib.rst
   _thread.rst


MicroPython-specific libraries
------------------------------

Functionality specific to the MicroPython implementation is available in
the following libraries.

.. toctree::
   :maxdepth: 3

   btree.rst
   framebuf.rst
   machine.rst
   micropython.rst
   network.rst
   ubluetooth.rst
   ucryptolib.rst
   uctypes.rst
   mutex.rst


Libraries specific to the OpenMV Cam
------------------------------------

The following libraries are specific to the OpenMV Cam.

.. toctree::
   :maxdepth: 3

   pyb.rst
   omv.sensor.rst
   omv.image.rst
   omv.tf.rst
   omv.gif.rst
   omv.mjpeg.rst
   omv.audio.rst
   omv.micro_speech.rst
   omv.lcd.rst
   omv.fir.rst
   omv.tv.rst
   omv.cpufreq.rst
   omv.buzzer.rst
   omv.imu.rst
   omv.rpc.rst
   omv.rtsp.rst
   omv.omv.rst

Third-party libraries on the OpenMV Cam
---------------------------------------

The following third-party libraries are built-in to your OpenMV Cam's firmware:

:mod:`ulab` --- `numpy-like array manipulation library <https://micropython-ulab.readthedocs.io/en/latest/>`_
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. module:: ulab
   :synopsis: numpy-like array manipulation library

:mod:`pid` --- `Proportional/Integral/Derivative Control <https://github.com/openmv/openmv/blob/master/scripts/libraries/pid.py>`_
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. module:: pid
   :synopsis: Proportional/Integral/Derivative Control

:mod:`bno055` --- `IMU Driver <https://github.com/openmv/openmv/blob/master/scripts/libraries/bno055.py>`_
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. module:: bno055
   :synopsis: IMU Driver

Examples scripts are located in OpenMV IDE under the ``IMU Shield`` examples folder.

:mod:`ssd1306` --- `OLED Driver <https://github.com/openmv/openmv/blob/master/scripts/libraries/ssd1306.py>`_
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. module:: ssd1306
   :synopsis: OLED Driver

:mod:`tb6612` --- `Stepper Motor Driver <https://github.com/openmv/openmv/blob/master/scripts/libraries/tb6612.py>`_
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. module:: tb6612
   :synopsis: Stepper Motor Driver

Examples scripts are located in OpenMV IDE under the ``Motor Shield`` examples folder.

:mod:`vl53l1x` --- `ToF Distance Sensor Driver <https://github.com/openmv/openmv/blob/master/scripts/libraries/vl53l1x.py>`_
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. module:: vl53l1x
   :synopsis: ToF Distance Sensor Driver

Examples scripts are located in OpenMV IDE under the ``Distance Shield`` examples folder.

:mod:`modbus` --- `modbus protocol library <https://github.com/openmv/openmv/blob/master/scripts/libraries/modbus.py>`_
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. module:: modbus
   :synopsis: modbus protocol library

Examples scripts are located in OpenMV IDE under the ``Modbus`` examples folder.

:mod:`mqtt` --- `mqtt protocol library <https://github.com/openmv/openmv/blob/master/scripts/libraries/mqtt.py>`_
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. module:: mqtt
   :synopsis: mqtt protocol library

Examples scripts are located in OpenMV IDE under the ``WiFi Shield`` examples folder.
