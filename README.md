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

# Generating the NAND Image


Before generating the NAND image, make sure to remove any prior NAND data by running `rm -rf nand`.
Using the filesystem image we generated from earlier, run this command from the binary you compiled.

```
./generate_nand
```
