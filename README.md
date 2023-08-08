# generate-ipod-touch-1g-nand
## Usage:
Copy filesystem-readonly.img to this directory <br>
`gcc generate_nand.c -o generate_nand` <br>
`./generate-nand`
This generates a nand folder which can be used with qemu. <br>

<b>Refer to docs/changes.md for the changes that need to made to a rootfs from an ipsw<b>
