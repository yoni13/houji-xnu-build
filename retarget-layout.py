#!/usr/bin/env python3
"""Retarget the pinned houji storage layout to this phone's current stock GPT.

Values come from the LUN0 GPT read back from this device on 2026-11-01
(stock OS3.0.303.0.WNCTWXM): userdata moved from LBA 2260480 to 2362880 and the
LUN reports 16 MiB less, so every pinned GPT identity changes.
Usage: retarget-layout.py ROOT [ROOT...]   (kernel overlay tree and/or UEFI tree)
"""
import sys
from pathlib import Path

KERNEL = {
    'iokit/Kernel/houji-ufs-root/HoujiRootPolicy.h': [
        ('kHoujiRootFirst=2260480U;', 'kHoujiRootFirst=2362880U;'),
    ],
    'iokit/Kernel/houji-ufs-root/HoujiGptCheck.h': [
        ('{0x4d,0x48,0xd1,0xe7,0x8a,0xc5,0x30,0xaa,0x98,0xc1,0xb1,0x10,0xb4,0xbc,0x5e,0xc4}',
         '{0xff,0xe4,0x33,0x27,0xf8,0x31,0x78,0xf6,0x9e,0x84,0x69,0x8e,0xcd,0x01,0x4e,0xae}'),
        ('houjiLe64(h+32)!=0x7AABFFF', 'houjiLe64(h+32)!=0x7AAAFFF'),
        ('0x631FC7EA', '0xCC9F7606'),
        ('0x9F1BB496', '0xE47F2C34'),
    ],
}
UEFI = {
    'Platforms/Xiaomi/houjiPkg/Application/HoujiStorageProbe/LogFsTest.h': [
        ('{0xE7D1484D,0xC58A,0xAA30,{0x98,0xC1,0xB1,0x10,0xB4,0xBC,0x5E,0xC4}}',
         '{0x2733E4FF,0x31F8,0xF678,{0x9E,0x84,0x69,0x8E,0xCD,0x01,0x4E,0xAE}}'),
        ('{0x04BB810B,0x92B4,0x38CA,{0x8E,0xF5,0x9D,0xD0,0x5A,0x04,0xFE,0x5C}}',
         '{0x4CACECDD,0x046F,0x895A,{0xB9,0xC3,0xD1,0x79,0x02,0xEA,0x88,0xD9}}'),
        ('M->LastBlock != 0x7AABFFF', 'M->LastBlock != 0x7AAAFFF'),
        ('0x631FC7EA', '0xCC9F7606'),
        ('0x9F1BB496', '0xE47F2C34'),
    ],
}


def apply(root):
    done = 0
    for table in (KERNEL, UEFI):
        for rel, edits in table.items():
            path = root / rel
            if not path.exists():
                continue
            text = path.read_text()
            for old, new in edits:
                if new in text and old not in text:
                    continue
                if old not in text:
                    raise SystemExit(f'{path}: expected text not found: {old}')
                text = text.replace(old, new)
            path.write_text(text)
            done += 1
            print('retargeted', path)
    if not done:
        raise SystemExit(f'nothing to retarget under {root}')


for arg in sys.argv[1:]:
    apply(Path(arg))
