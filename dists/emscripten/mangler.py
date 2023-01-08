#!/usr/bin/env python
# -*- coding: utf-8 -*-

from subprocess import (
    Popen,
    PIPE
)

import getopt
import os
import re
try:
    import readline
    readline
except ImportError:
    pass
import sys


ASM_TEMPLATE = (
    '.global %s\n'
    '%s:\n'
    '   pushq %%rbp\n'
    '   movq %%rsp, %%rbp\n'
    '   # Reserve space for stack-frame, keep right alignment!\n'
    '   sub $ , %%rsp\n\n'
    '   leave\n'
    '   ret'
)

# inspired to bcolors class
# http://bit.ly/2FzFfoz
class console:
    BOLD = '\033[1m'
    UNDERLINE = '\033[4m'

    HEADER = '\033[95m'
    OKBLUE = '\033[94m'
    OKGREEN = '\033[92m'
    WARNING = '\033[93m'
    FAIL = '\033[91m'
    ENDC = '\033[0m'

    @staticmethod
    def decorator_ok(decorator):
        return decorator in (
            console.BOLD, console.UNDERLINE,
            console.HEADER, console.OKBLUE,
            console.OKGREEN, console.WARNING,
            console.FAIL, console.ENDC
        )

    @staticmethod
    def write(text, decorators=[]):
        for decorator in decorators:
            if console.decorator_ok(decorator):
                text = decorator + text
        print text + console.ENDC,

    @staticmethod
    def flush():
        print console.ENDC

    @staticmethod
    def writef(text, decorators=[]):
        console.write(text, decorators)
        console.flush()


def mangle(text):
    FUNCT = '%s %s;'
    STRUCT = 'struct %s{%s};'

    try:
        ret_type, rest = text.split(' ', 1)
    except ValueError:
        ret_type, rest = 'void', text
        text = '%s %s' % (ret_type, text)

    rest = rest.split('::')[::-1]

    out = ''
    if len(rest) > 1:
        out = FUNCT % (ret_type, rest[0])
        for val in rest[1:]:
            out = STRUCT % (val, out)

    out += text + '{}'

    gpp = Popen(
        ['g++', '-x', 'c++', '-S', '-', '-o', '-'],
        stdin=PIPE,
        stdout=PIPE,
        stderr=PIPE
    )
    out = gpp.communicate(out)[0]

    mangled = re.findall(r'(_.*):', out)
    return mangled[0] if len(mangled) > 0 else None


def interactive_mode():
    console.writef('Interactive mode', [console.BOLD])
    print 'To quit use commands `exit` or `quit` or `q`'
    print 'For info use `h`, `help`'
    print 'Use `asmode` to disable/enable assembly output'

    asmode = True
    while True:
        input_text = raw_input('>>> ').strip()
        if input_text == '':
            continue
        elif input_text in ('q', 'q!', 'quit', 'exit'):
            break
        elif input_text in ('asmode'):
            asmode = not asmode
            console.writef('Enabled!' if asmode else 'Disabled!')
            continue
        elif input_text in ('h', 'help'):
            help_message()
            continue

        out = mangle(input_text)
        if out is None:
            console.write('Error: ', [console.BOLD, console.FAIL])
            console.write('function not valid!', [console.BOLD])
            console.flush()
            continue
        if asmode:
            console.writef(
                ASM_TEMPLATE % (out, out),
                [console.BOLD]
            )
            continue
        console.writef(out)

    sys.exit(0)


def help_message():
    console.writef('TODO')


def main(argv):
    try:
        opts, args = getopt.getopt(
            argv,
            'hi:',
            ['help', 'interactive', 'asmode', 'input=']
        )
    except getopt.GetoptError:
        print 'Error while parsing args'
        sys.exit(1)

    text = None
    asmode = False
    for opt, arg in opts:
        if opt in ('-h', '-help'):
            help_message()
        elif opt in ('--asmode'):
            asmode = True
        elif opt in ('--interactive'):
            interactive_mode()
        elif opt in ('-i', '--input'):
            text = arg

    if len(opts) == 0:
        interactive_mode()
        return

    out = mangle(text).strip()
    if asmode is True:
        print ASM_TEMPLATE % (out, out)
    else:
        print out


if __name__ == "__main__":
    os.system('')  # enable VT100 Escape Sequence for WINDOWS 10 Ver. 1607
    try:
        main(sys.argv[1:])
    except KeyboardInterrupt:
        console.flush()
        sys.exit(0)
