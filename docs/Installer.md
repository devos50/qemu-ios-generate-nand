# Adding Installer.app to the filesystem
`sudo diskutil enableOwnership {path to mounted image}` <br>
Copy the `Installer.app` folder from resources to the mounted `filesystem-writable.dmg` `/Applications` folder. <br>
Copy the `sbpatcher` file from resources to the `bin` folder on the dmg <br>
Copy `killall` from `binaries/usr/bin` to `usr/bin` in the image as root <br>
`chmod +x {path to image}/sbpatcher` <br>
Navigate to the folder `System/Library/LaunchDaemons/` folder and run `sudo nano com.zoe.installer.plist` <br>
Inside the file paste: <br>

```xml
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs$
<plist version="1.0">
<dict>
        <key>Label</key>
        <string>com.zoe.installer</string>
        <key>ProgramArguments</key>
        <array>
                <string>/bin/bash</string>
                <string>/bin/installer.sh</string> 
        </array>
        <key>RunAtLoad</key>
        <true/>
</dict>
</plist>
```
Now type `ctrl+o` then `enter` then `ctrl+x` <br>
Navigate to the `bin` folder and paste bash from the resources folder <br>
Then create a new file with `sudo nano installer.sh` <br>
Inside the file paste:

```bash
/bin/sbpatcher && /usr/bin/killall SpringBoard
```
As usual `ctrl+o` `enter` `ctrl+x` <br>
Now go ahead and unmount the image and create the nand image with `./generate-nand` as usual.

### So what does this actually do? 
Well... not much right now! It just gives you installer.app on your home screen which can be launched. However you can also paste any other `.app` files you have extracted from pxl bundles
and they will show up and be able to run too. I'm planning on making something like Installer in the future where you can select a local pxl and it will unpack and install it. Right now 
installer can't be used properly due to the fact the emulator doesn't have any support for networking (but I have tested several pxl apps and they work just fine!

<img width="346" alt="Screenshot 2023-07-21 at 20 05 42" src="https://github.com/zoe-vb/generate-ipod-touch-1g-nand/assets/96234996/bed2c42e-2f34-4c5e-a013-ac650dd2de88">



