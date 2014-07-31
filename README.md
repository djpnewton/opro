opro
====

Overhead profiler

Use to calculate the overhead of an image in a running process

eg:

    ./testapp # run as a sample process to profile, record the pid of this process

    ./testapp libload.so <pid> # profile the other process and calculate the overhead of the libload.so image 

Either libunwind or libcorkscrew (for Android) is used to inspect the thread callstacks.

A standalone version of libcorkscrew for building with the NDK can be found here: https://github.com/djpnewton/libcorkscrew_ndk
