# Changes that need to be made to 022-3601-4.dmg
## Decrypting the rootfs dmg
The first step is to extract the dmg, this is done using xpwntool's version of `dmg` <br>
`cd` into the xpwn directory (<b>you must use this version</b> as it has a simple patch to enable the program to compile) <br>
then run `mdkir compile && cd compile` <br>
`cmake .. && make` <br>
`cp dmg/dmg ../../ && cd ../../`

Then run `./dmg extract 022-3601-4.dmg filesystem-writable.dmg -k 6f021b478cc21ff77f775850c0efc2e66fd015f6a6894be079ee1351dce9af069f915f3d` <br><br>
This generates a writable rootfs which we will modify to enable booting on qemu

## Patching the filesystem
Double click filesystem-writable.dmg to mount it in Finder <br><br>
Click on the volume name that just appeared and navigate to `private/etc/` <br>
Open the file `fstab` in the text editor and <b>delete the second line</b>, then change `ro` to `rw` in the first line. <br>

Navigate to `private/var/root` and create the folders `AddressBook`, `Lockdown` and `Preferences`. <br>
Inside the folder `AddressBook` copy `AddressBook.sqlitedb` from the `resources` folder. <br>
Now go to the `Lockdown` folder which was created earlier and paste `data_ark.plist` from the `resources` folder. <br>
Then create a folder called activation_records and paste `pod_record.plist` from the `resources` folder. <br>
In the `Preferences` folder paste `com.apple.springboard.plist`. <br><br>

Go to `System/Library/LaunchDaemons/` now run the command `sudo plutil -convert xml1 com.apple.SpringBoard.plist` <br>
Run `sudo open com.apple.SpringBoard.plist` which should open it in an editor, under the first `<dict>` paste the following text <br>
` <key>EnvironmentVariables</key>` <br>
`        <dict>` <br>
`                <key>LK_ENABLE_MBX2D</key>` <br>
`                <string>0</string>` <br>
`        </dict>` <br>
`</key>` <br><br>

then save and close it.
Now run `sudo plutil -convert binary1 com.apple.SpringBoard.plist`<br>
Now delete all `.plist` files except for <br> `com.apple.AddressBook.plist` <br> `com.apple.CommCenter.plist` <br> `com.apple.configd.plist` <br> `com.apple.mobile.lockdown.plist` <br>
`com.apple.notifyd.plist` <br> `com.apple.SpringBoard.plist`

Eject the disk and the patches are all appplied and the program can be used as normal!


