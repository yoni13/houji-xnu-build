#!/usr/bin/env python3
"""Adapt modern IIG output to Darwin 22's four-field IORPC ABI.

Keep the kernel's runtime ABI unchanged. The newer generator caches a pointer
to the Mach message body in a fifth IORPC field. Resolve that pointer through
Darwin 22's existing validated decoder instead. Unexpected forms fail closed.
"""
import re
import subprocess
import sys
from pathlib import Path


def transform(text):
    def mach_pointer_casts(value):
        return re.sub(r'IORPCMessageFromMach\((rpc(?:\.rpc)?\.(?:message|reply)),',
                      r'IORPCMessageFromMach((IORPCMessageMach *)\1,', value)
    if 'kernelContent' not in text:
        return mach_pointer_casts(text)
    # Typed aliases in the generator's local invocation unions.
    text = re.sub(r'^\s*const struct \w+\s*\*\s*kernelContent;\n', '', text, flags=re.M)
    # The extra field is not present in Darwin 22's IORPC.
    text = re.sub(r',\s*\.kernelContent\s*=\s*&buf\.msg\.content\.__hdr', '', text)
    text = re.sub(r'^\s*rpc\.kernelContent\s*=\s*&msg->content\.__hdr;\n', '', text, flags=re.M)
    # Dispatch and typed message-body pointers use the existing Mach decoder.
    pattern = r'^(\s*((?:const\s+)?\w+)\s*\*\s*\w+\s*=\s*)rpc\.kernelContent;'
    text = re.sub(pattern, lambda m: m[1] + '(' + m[2] + ' *)IORPCMessageFromMach(rpc.message, false);', text, flags=re.M)
    text = text.replace('message = rpc.kernelContent;', 'message = IORPCMessageFromMach(rpc.reply, false);')
    text = text.replace('replyMessage = rpc.rpc.kernelContent;', 'replyMessage = IORPCMessageFromMach(rpc.rpc.reply, false);')
    if 'kernelContent' in text:
        raise ValueError('Unrecognized IIG kernelContent form; inspect output before compiling')
    return ('// Darwin 22 IORPC compatibility; runtime ABI is unchanged.\n'
            '#include <IOKit/IORPC.h>\n'
            'extern IORPCMessage *IORPCMessageFromMach(IORPCMessageMach *, bool);\n' + mach_pointer_casts(text))


def main():
    args = sys.argv[1:]
    if args and args[0] == '--transform':
        for name in args[1:]:
            p = Path(name)
            text = p.read_text()
            result = transform(text)
            if text != result:
                p.write_text(result)
        return
    real = subprocess.check_output(['xcrun', '--find', 'iig'], text=True).strip()
    subprocess.run([real, *args], check=True)
    if '--impl' in args:
        p = Path(args[args.index('--impl') + 1])
        p.write_text(transform(p.read_text()))


if __name__ == '__main__':
    main()
