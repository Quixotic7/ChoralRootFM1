"""Stand-in for fm-1-research-lab's fm1fw.Firmware, as FM-1-transporter tools/fm1t.py package_image uses it.

`raw` is the package's logical image (the .fwsc without its 20 identity marker bytes). The type-0 entry is
flash.bin as firmware/src/ota.c ota_ufw finds it in the ciphered UFW header (data_off = fl_off, 0x400 for
fm1pkg_make.py packages; size = fl_len): flash address X is raw[fl_off + X]. skew is 0.
"""
from _fm1pkg import logical_image, product_of, ufw_flash


class Firmware:
    def __init__(self, package):
        package = bytes(package)
        self.package = package
        self.product = product_of(package)
        self.raw = logical_image(package)
        self.skew = 0
        fl_off, fl_len = ufw_flash(self.raw)          # ValueError on bad CRCs / no flash.bin
        self.entries = [{"type": 0, "data_off": fl_off, "size": fl_len}]

    def image0(self):
        e = self.entries[0]
        return self.raw[e["data_off"] + self.skew:e["data_off"] + self.skew + e["size"]]
