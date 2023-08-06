#!/usr/libexec/cydia_/bash
export PATH=/usr/libexec/cydia_

if grep -Ew 'noexec|ro' /etc/fstab >/dev/null; then
    sed -i -e 's/\<ro\>/rw/;s/,noexec\>//' /etc/fstab
    exit 1
else
    exit 0
fi
