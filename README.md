ioping
======

An tool to monitor I/O latency in real time.
It shows disk latency in the same way as ping shows network latency.

Homepage: https://github.com/koct9i/ioping/
(migrated from http://code.google.com/p/ioping/)

Please send your patches, issues and questions to
https://github.com/koct9i/ioping/issues/

Supported OS
------------

* GNU/Linux
* GNU/HURD
* Windows
* OS X
* FreeBSD
* DragonFlyBSD
* OpenBSD

Packages
--------

* Debian: https://packages.debian.org/unstable/main/ioping
* Ubuntu: https://launchpad.net/ubuntu/+source/ioping
* Fedora: https://apps.fedoraproject.org/packages/ioping
* ArchLinux: https://www.archlinux.org/packages/community/x86_64/ioping
* AltLinux: http://www.sisyphus.ru/en/srpm/Sisyphus/ioping
* Gentoo: https://packages.gentoo.org/package/app-benchmarks/ioping
* Nix: https://github.com/NixOS/nixpkgs/blob/master/pkgs/tools/system/ioping/default.nix
* FreeBSD: http://www.freshports.org/sysutils/ioping
* OS X: https://github.com/Homebrew/homebrew/blob/master/Library/Formula/ioping.rb

Examples
--------

Show disk I/O latency using the default values and the current directory, until interrupted

```
$ ioping .
4096 bytes from . (ext4 /dev/sda3): request=1 time=0.2 ms
4096 bytes from . (ext4 /dev/sda3): request=2 time=0.2 ms
4096 bytes from . (ext4 /dev/sda3): request=3 time=0.3 ms
4096 bytes from . (ext4 /dev/sda3): request=4 time=12.7 ms
4096 bytes from . (ext4 /dev/sda3): request=5 time=0.3 ms
^C
--- . (ext4 /dev/sda3) ioping statistics ---
5 requests completed in 4794.0 ms, 364 iops, 1.4 MiB/s
min/avg/max/mdev = 0.2/2.8/12.7/5.0 ms
```

Measure disk seek rate (iops, avg)

```
$ ioping -R /dev/sda

--- /dev/sda (device 465.8 GiB) ioping statistics ---
186 requests completed in 3004.6 ms, 62 iops, 0.2 MiB/s
min/avg/max/mdev = 6.4/16.0/26.8/4.7 ms
```

Measure disk sequential speed (MiB/s)

```
$ ioping -RL /dev/sda

--- /dev/sda (device 465.8 GiB) ioping statistics ---
837 requests completed in 3004.1 ms, 292 iops, 72.9 MiB/s
min/avg/max/mdev = 2.0/3.4/28.9/2.0 ms
```

Authors
-------

* Konstantin Khlebnikov <koct9i@gmail.com>
* Kir Kolyshkin <kir@openvz.org>

Licensed under GPLv3 (or later) <http://www.gnu.org/licenses/gpl-3.0.txt>
