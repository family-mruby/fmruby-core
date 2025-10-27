# ESP-IDFのLinuxターゲットについて

POSIX/Linux Simulator Approach
The FreeRTOS POSIX/Linux simulator is available on ESP-IDF as a preview target already. This simulator allows ESP-IDF components to be implemented on the host, making them accessible to ESP-IDF applications when running on host. Currently, only a limited number of components are ready to be built on Linux. Furthermore, the functionality of each component ported to Linux may also be limited or different compared to the functionality when building that component for a chip target. For more information about whether the desired components are supported on Linux, please refer to Component Linux/Mock Support Overview.

Note that this simulator relies heavily on POSIX signals and signal handlers to control and interrupt threads. Hence, it has the following limitations:

Functions that are not async-signal-safe, e.g. printf(), should be avoided. In particular, calling them from different tasks with different priority can lead to crashes and deadlocks.

Calling any FreeRTOS primitives from threads not created by FreeRTOS API functions is forbidden.

FreeRTOS tasks using any native blocking/waiting mechanism (e.g., select()), may be perceived as ready by the simulated FreeRTOS scheduler and therefore may be scheduled, even though they are actually blocked. This is because the simulated FreeRTOS scheduler only recognizes tasks blocked on any FreeRTOS API as waiting.

APIs that may be interrupted by signals will continually receive the signals simulating FreeRTOS tick interrupts when invoked from a running simulated FreeRTOS task. Consequently, code that calls these APIs should be designed to handle potential interrupting signals or the API needs to be wrapped by the linker.

Since these limitations are not very practical, in particular for testing and development, we are currently evaluating if we can find a better solution for running ESP-IDF applications on the host machine.

Note furthermore that if you use the ESP-IDF FreeRTOS mock component (tools/mocks/freertos), these limitations do not apply. But that mock component will not do any scheduling, either.

Note

The FreeRTOS POSIX/Linux simulator allows configuring the Amazon SMP FreeRTOS version. However, the simulation still runs in single-core mode. The main reason allowing Amazon SMP FreeRTOS is to provide API compatibility with ESP-IDF applications written for Amazon SMP FreeRTOS.