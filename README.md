### Generating an iPod Touch 2G NAND Image (for QEMU-iOS)

This README contains the instructions on how to generate the NAND image for the iPod Touch 2G that can be read by [QEMU-iOS](https://github.com/devos50/qemu-ios).
For this, you must put the `filesystem-it2g-readonly.img` file of the filesystem that will be included in the NAND image in the root of this repository.


## Compiling Instructions
To compile, run this command to compile the binary:

```
gcc generate_nand.c -o generate_nand
```

## Converting the filesystem into an image
Converting the filesystem to an image is somewhat simple. You first need to convert the filesystem read-only with hdiutil:

```
hdiutil convert -format UDRO filesystem-it2g-writable.dmg -o filesystem-it2g-readonly.dmg
```

Before converting `filesystem-it2g-readonly.dmg` you will need to find what partition you need to convert. You can find this out easily by using dmg2img to list the disk partitions.

```
username@Macbook-Air qemu-ios-generate-nand %dmg2img -l filesystem-it2g-readonly.dmg

dmg2img v1.6.7 (c) vu1tur (to@vu1tur.eu.org)

filesystem-it2g-readonly.dmg --> (partition list)

partition 0: Driver Descriptor Map (DDM : 0)
partition 1:  (Apple_Free : 1)
partition 2: Apple (Apple_partition_map : 2)
partition 3: Macintosh (Apple_Driver_ATAPI : 3)
partition 4:  (Apple_Free : 4)
partition 5: Mac_OS_X (Apple_HFSX : 5)
partition 6:  (Apple_Free : 6)
```

For me, partition 5 is the one I need to convert. Now convert the image with the partition number like so:

```
dmg2img -p 5 filesystem-it2g-readonly.dmg
```

# Adding files from the original filesystem

`add_files.py` copies files into the writable DMG (or a raw `.img`) and makes
them owned by root:wheel without sudo, which launchd requires for launch
daemons. Run it before converting the DMG, with the original filesystem
mounted, e.g.:

```
python3 add_files.py filesystem-it2g-writable.dmg /Volumes/<original> \
    /System/Library/LaunchDaemons/com.apple.usbptpd.plist \
    /System/Library/LaunchDaemons/com.apple.syslogd.plist
```

The current image has these launch daemons added back:

- `usbptpd`: ptpd must register the PTP interface before the kernel goes on the
  USB bus.
- `syslogd`: needed by lockdown's syslog_relay service, e.g. `idevicesyslog`.
- `update`: syncs the filesystem every 30 seconds, so files survive when QEMU
  is killed.
- `securityd`: the keychain, started on demand (Safari, Mail).
- `mDNSResponder` and `mDNSResponderHelper`: DNS and Bonjour.
- `MobileFileIntegrity` (amfid): the kernel asks it about binaries that are not
  in the static trust cache, i.e. not about the stock system binaries.
- `mediaserverd`: the audio server (AudioQueue, RemoteIO, system sounds). No
  sound plays without it.
- `CrashHousekeeping`: runs once at boot and tidies the crash logs that
  `ReportCrash` writes.
- `AddressBook` (`ABDatabaseDoctor`): checks the Contacts database, on demand.
- `daily`: periodic cleanup, once a day.

`com.apple.aslmanager.plist` is left out: iOS 2.1.1 ships it without the
`/usr/sbin/aslmanager` binary.

The image also has an SSH server and a shell, from the Telesphoreo repository
(`apt.saurik.com/dists/tangelo-0.9`), which targets iPhoneOS 2:

- OpenSSH 5.2p1 (`sshd`, `ssh`, `scp`, `ssh-keygen`, `sftp-server`) with
  `libcrypto` from OpenSSL 0.9.8k, and `com.openssh.sshd.plist` starting
  `sshd -i` on demand. There is a pre-generated RSA host key; `sshd_config`
  only lists that key.
- bash 3.2 (also `/bin/sh`, which OpenSSH needs as the login shell of root and
  mobile), readline, ncurses (a few terminfo entries), `libintl` from gettext,
  coreutils 6.9 and a minimal `/etc/profile`.

Connect over USB with `iproxy 2222 22` and
`ssh -p 2222 -o HostKeyAlgorithms=+ssh-rsa -o KexAlgorithms=+diffie-hellman-group14-sha1 root@127.0.0.1`
(password `alpine`).

`/usr/lib/libgcc_s.1.dylib` is replaced by the one from Telesphoreo's `libgcc`
package. It exports everything the stock one does, plus `___floatundidf` and a
few other conversions that coreutils (`df`, `du`, `ls -h`, `sort`, `dd`) need.
iOS 2.1.1 has no dyld shared cache, so the file on disk is what gets loaded.

`filesystem-it2g-writable.pre-ssh.dmg` is the image before OpenSSH was added,
`filesystem-it2g-writable.pre-bash.dmg` the one before the shell and
`filesystem-it2g-writable.pre-libgcc.dmg` the one with the stock `libgcc_s`
and `filesystem-it2g-writable.pre-housekeeping.dmg` the one before
`CrashHousekeeping`, `AddressBook` and `daily`.
`filesystem-it2g-writable.pre-daemons.dmg` is the image with only `usbptpd` and
`syslogd` added. `filesystem-it2g-writable.pre-mediaserverd.dmg` is the
image before `mediaserverd` was added.

# Generating the NAND Image


Before generating the NAND image, make sure to remove any prior NAND data by running `rm -rf nand`.
Using the filesystem image we generated from earlier, run this command from the binary you compiled.

```
./generate_nand
```

Optionally, pass the filesystem image and the output directory: `./generate_nand <filesystem image> <output directory>`.

QEMU-iOS writes to the NAND directory while iOS is running, so keep a copy of a freshly generated image if you want to
start over later. Images generated by older versions of this tool place the filesystem in blocks that the FTL also uses
as free and context blocks, so iOS corrupts them once it writes to the NAND. Regenerate those images.

# Changing the filesystems of an existing NAND

`nand_disk.py` takes the disk out of a NAND that iOS wrote itself (for
example after a restore with `restore_boot.py` from it2g-usb-socket) and
puts it back:

```
python3 nand_disk.py extract nand disk
python3 add_files.py disk/partition1.img <source root> <path> ...
python3 nand_disk.py build disk nand.new
```

`extract` keeps the newest copy of every logical page. The FTL stores each
page's logical page number in the spare area, together with the write age of
its block. The disk is written to `disk/disk.img` and each MBR partition to
`disk/partition<n>.img`; a restored device has the system partition (1) and
the data partition (2, mounted at `/private/var`). The images are sparse.
`build` copies the partition images back into the disk and runs
`./generate_nand --disk disk/disk.img nand.new`. That writes a freshly
formatted NAND, with every logical block that holds data written in place.

`add_files.py` maps the image into memory instead of reading it, so it also
works on the 7 GB data partition.
