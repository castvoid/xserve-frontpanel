# xserve-frontpanel
This project contains a small program that demonstrates how to drive the Intel Xserve's front panel CPU activity LEDs. I've only tested it on a 2009 Dual Xeon Xserve, but it should work on any Intel-based Xserve.

It's a work in progress. Currently, only the CPU activity LEDs are working.

Eventually, I hope to be able to drive all features of the Xserve (IPMI interaction over BMC, CPU-activity linked front panel LEDs).

On OS X Lion Server-era systems, all of this was done by the hwmond daemon, which made use of the _PlatformHardwareManagement_ framework. These binaries cannot (easily) be ported to newer OSes, so I started work on a re-implementation.

---
## Building
Currently, this project should build & run under macOS and Linux (but I might have broken something – please let me know!). Make sure you have `libusb` installed.

```bash
cmake . && make
```

## Running
```bash
./hwmond
```

---
## Auto loading with Launchd
The following optional instructions will walk you through using the included plist to auto load the hwmond file at boot and keep it running while the machine is running.

Start by editing com.xserve-frontpanel.daemon.plist 

```<string>/xserve-frontpanel/hwmond</string>
```

portion of the com.xserve-frontpanel.daemon.plist to have the location of where you have installed the file, IE. 

```<string>/PATH-TO-HWMOND/hwmond</string>
```

One you have set the path to the hwmond. Open the terminal and cd to the containing directory. Once in the directory set the  permissions for the com.xserve-frontpanel.daemon.plist to 644 using the following command.

```chmod 644 com.xserve-frontpanel.daemon.plist
```

Rather than physcially placing the plist into the LaunchDaemons directory it is better to create a symlink as follows, however if you prefer you can physically copy the file to the LaunchDaemons directory.

```sudo ln -s com.xserve-frontpanel.daemon.plist /Library/LaunchDaemons/com.xserve-frontpanel.daemon.plist
```

You now need to load the plist into Launchd ( the process that loads and keeps stuff running )

```sudo launchctl load -w /Library/LaunchDaemons/com.xserve-frontpanel.daemon.plist
```

The “-w” part of the last command tells the computer this process has to ALWAYS run. So even if you kill the HWmond process Launchd will start it back up. This ensure the front panel lights stay functional as long as the system is running.
