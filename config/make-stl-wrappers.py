# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os, re, string, sys

def find_in_path(file, searchpath):
    for dir in searchpath.split(os.pathsep):
        f = os.path.join(dir, file)
        if os.path.exists(f):
            return f
    return ''

def header_path(header, compiler):
    if compiler == 'gcc':
        # we use include_next on gcc
        return header
    elif compiler == 'msvc':
        return find_in_path(header, os.environ.get('INCLUDE', ''))
    else:
        # hope someone notices this ...
        raise NotImplementedError, compiler

def is_comment(line):
    return re.match(r'\s*#.*', line)

def main(outdir, compiler, template_file, header_list_file):
    if not os.path.isdir(outdir):
        os.mkdir(outdir)

    template = open(template_file, 'r').read()
    path_to_new = header_path('new', compiler)

    for header in open(header_list_file, 'r'):
        header = header.rstrip()
        if 0 == len(header) or is_comment(header):
            continue

        path = header_path(header, compiler)
        try:
            f = open(os.path.join(outdir, header), 'w')
            f.write(string.Template(template).substitute(HEADER=header,
                                                         HEADER_PATH=path,
                                                         NEW_HEADER_PATH=path_to_new))
        finally:
            f.close()


if __name__ == '__main__':
    if 5 != len(sys.argv):
        print >>sys.stderr, """Usage:
  python %s OUT_DIR ('msvc'|'gcc') TEMPLATE_FILE HEADER_LIST_FILE
"""% (sys.argv[0])
        sys.exit(1)

    main(*sys.argv[1:])
