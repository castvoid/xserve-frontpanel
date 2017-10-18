# xserve-frontpanel
This project contains a small program that demonstrates how to drive the Intel Xserve's front panel CPU activity LEDs. I've only tested it on a 2009 Dual Xeon Xserve, but it should work on any Intel-based Xserve.

It's a work in progress that's currently on hold, but should serve as a good start point for a proper implementation.

Eventually, I hope to be able to drive all features of the Xserve (IPMI interaction over BMC, CPU-activity linked front panel LEDs). On OS X Lion Server-era systems, this was done by the hwmond daemon, which made use of the _PlatformHardwareManagement_ framework.

---
## Building
Currently, this project should build & run under macOS and Linux (but I might have broken something – please let me know!)

```bash
cmake . && make
```

## Running
```bash
./hwmond
```
