#!/usr/bin/env python
#
# MIT License
#
# Copyright (c) 2021 Caio Alves Garcia Prado
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.
#
#############################################################################
#
# makedepend.py
#
# Creates a dependency file (makefile snippet) for:
#   - linking executables for C/C++ (lists .o files)
#   - headers and modules for Fortran
#
# Run:
#
#     $ ./makedepend.py --help
#
# for more information!
#
#############################################################################

import re
import os
import sys
import argparse
import textwrap

srcext = ['.c', '.cpp', '.f90', '.f']
incext = ['.h', '.hpp', '.inc']
srcdir = ''
incdir = ''
programs = []


def main():
    output = parse_arguments()

    # Fill list of dependencies
    depend_table = {}
    fortran_headers = {}
    for item in programs:
        depend_list = set()
        if is_fortran(item):
            fill_header_deplist(depend_list, item)
            fortran_headers[get_source(basename(item))] = depend_list
        else:
            item = basename(item)
            fill_object_deplist(depend_list, item)
            depend_table[get_program(item)] = depend_list

    # Write output
    write_depend_table(output, depend_table, fortran_headers)


def parse_arguments():
    # Parse command line arguments.
    parser = argparse.ArgumentParser(
        formatter_class=argparse.RawDescriptionHelpFormatter,
        description=textwrap.dedent("""
Creates a dependency file (makefile snippet) for:
    - linking executables for C/C++ (lists .o files)
    - headers and modules for Fortran

C/C++:
    GCC output header dependencies when compiling C/C++ code but no linking
    dependency is resolved.  This script assumes that every included header
    with an associated implementation file should be linked in the final
    executable.

Fortran:
    In case of Fortran code, gfortran will not generate header dependencies
    automatically, this script tries to fill role.  However, linking is much
    more involved since Fortran language doesn't enforce subroutines and
    functions to be previously declared.  Therefore, it would be necessary to
    parse the whole program looking for all possible calls and then blindly for
    their definition in other files.  This script will not do that.  Linking
    dependencies for Fortran code should be done MANUALLY in the makefile.


The script assumes a certain work flow in order to work properly:
    - Header files (.h|.hpp|.inc) are put in INCDIR (set in makefile).
    - Implementation files associated with a header must share the same
      basename and put in SRCDIR (set in makefile).
      Ex: $(INCDIR)/Class.hpp | $(SRCDIR)/Class.cpp.
    - Source files containing 'main()' function (C/C++) or 'PROGRAM' subroutine
      (Fortran) should be named as 'main_name.*', which will generate the final
      executable file 'bin/name'.
    - Fortran modules (.mod) are supported as long as one Fortran file defines
      only one module.
      Ex: module 'FooMod' should be defined in the 'foomod.f90' file.
    - Only free-form Fortran code is supported.
        """))
    parser.add_argument(
        '-I',
        action='store',
        type=str,
        default='include',
        help='Where to look for header files. [include/]',
        metavar='incdir')
    parser.add_argument(
        '-s',
        action='store',
        type=str,
        default='src',
        help='Where to look for source files. [src/]',
        metavar='srcdir')
    parser.add_argument(
        '-o',
        nargs='?',
        type=argparse.FileType('w'),
        default=sys.stdout,
        help='Output file.  Print to stdout if not given.',
        metavar='output')
    parser.add_argument(
        'filelist',
        type=str,
        nargs='+',
        help='Source file(s) to build dependencies for.',
        metavar='file')
    args = parser.parse_args()

    global srcdir, incdir, programs
    curdir = os.path.dirname(sys.argv[0])
    srcdir = os.path.join(curdir, args.s, '')
    incdir = os.path.join(curdir, args.I, '')
    programs = {os.path.basename(name) for name in args.filelist}
    return args.o


def is_fortran(filename):
    return filename.endswith(('.f', '.f90', '.inc'))


def basename(filename):
    return os.path.splitext(filename)[0]


def get_program(basename):
    return basename.replace('main_', '$(BINDIR)/')


def get_object(srcname):
    return os.path.splitext(srcname)[0] + '.o'


def get_module(basename):
    basename = basename.lower()
    for ext in ['.f90', '.f']:
        if os.path.isfile(srcdir + basename + ext):
            return '$(SRCDIR)/' + basename + '.mod'


def get_source(basename):
    for ext in srcext:
        filename = basename + ext
        if os.path.isfile(srcdir + filename):
            return '$(SRCDIR)/' + filename
    return None


def get_include(basename):
    for ext in incext:
        filename = basename + ext
        if os.path.isfile(incdir + filename):
            return '$(INCDIR)/' + filename
    return None


def get_realfile(filename):
    filename = filename.replace('$(SRCDIR)', srcdir)
    filename = filename.replace('$(INCDIR)', incdir)
    return filename


def fill_header_deplist(depend_list, filename):
    if filename.endswith('.inc'):
        filename = '$(INCDIR)/' + filename
    else:
        filename = '$(SRCDIR)/' + filename

    if filename not in depend_list:
        depend_list.add(filename)
        filename = get_realfile(filename)
        if not os.path.isfile(filename):
            return

        # parse content of file
        contents = ''
        with open(filename) as fd:
            for line in fd:
                contents += line

        # recursively scan file for headers
        includepattern = r'''^ *include *["'](.*)['"]'''
        includepattern = re.compile(includepattern, re.M | re.IGNORECASE)
        for included in re.findall(includepattern, contents):
            fill_header_deplist(depend_list, included)

        if not filename.endswith('.inc'):
            # if it is a source file we look for module
            # we only want direct module dependency, so no recursion is done
            modulepattern = r'''^ *use +([^ \n]*)$'''
            modulepattern = re.compile(modulepattern, re.M | re.IGNORECASE)
            for module in re.findall(modulepattern, contents):
                module = get_module(module)
                if module:
                    depend_list.add(module)


def fill_object_deplist(depend_list, basename):
    files = filter(None, [get_include(basename), get_source(basename)])

    for file in files:
        if file not in depend_list:
            depend_list.add(file)
            file = get_realfile(file)

            # parse content of file
            contents = ''
            with open(file) as fd:
                for line in fd:
                    contents += line

            # recursively scan file
            includepattern = re.compile(r'^#include *[<"](.*)[">]', re.M)
            for included in re.findall(includepattern, contents):
                fill_object_deplist(depend_list, os.path.splitext(included)[0])


def write_depend_table(output, depend_table, fortran_headers):
    self_depend = set()

    for program, deplist in depend_table.items():
        output.write(program + ':')
        for item in deplist:
            self_depend.add(item)
            if item.startswith('$(SRCDIR)'):
                output.write(' ' + get_object(item))
        output.write('\n\n')

    for source, deplist in fortran_headers.items():
        output.write(get_object(source) + ':')
        self_depend.add(source)
        for item in deplist:
            if source != item:
                output.write(' ' + item)
        output.write('\n\n')

    depend_filename = output.name
    output.write(depend_filename + ': ' + ' '.join(self_depend) + '\n\n')
    for item in self_depend:
        output.write(item + ':\n\n')


if __name__ == "__main__":
    main()
else:
    raise ImportError("This module is not supposed to be imported...")
