### Generating an iPod Touch 2G NAND Image (for QEMU-iOS)

This README contains the instructions on how to generate the NAND image for the iPod Touch 2G that can be read by [QEMU-iOS](https://github.com/devos50/qemu-ios).
For this, you must put the `filesystem-it2g-readonly.img` file of the filesystem that will be included in the NAND image in the root of this repository.

First, compile the binary with the following command:

```
gcc generate_nand.c -o generate_nand
```

Before generating the NAND image, make sure to remove any prior NAND data by running `rm -rf nand`.
Then, generate your NAND image by running the compiled binary:

```
./generate_nand
```
