# Modifying the filesystem for use with single user mode.
#### If you have not yet checked out and built the single user mode fork of the nor repo please do so before doing the following steps.

<b>Important: </b> Please copy `libncurses5.4.dylib` from the `{path to mounted image}/usr/lib/` to a safe location<br>
`sudo diskutil enableOwnership {path to mounted image}`<br>
`cd` into the `resources/binaries` folder. <br>
Run `sudo cp -rv * {path to mounted image}/` <br>
Paste the libncurses5.4.dylib back into `{path to mounted image}/usr/lib/` <b> If you DO NOT do this then this will fail and although it will appear to work it will not function as intended</b><br>
Convert the image into the nand folder as instructed at the bottom of Changes.md <br>
Now next time you boot the emulator you will be greated with a single user prompt. <br>
<b>Happy Hacking! ~ Zoe</b>
