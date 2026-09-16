#!/usr/bin/env python3
"""Generate shared region_info initializers from Clang's expanded AST.

Usage: generate_regions.py --clang clang-18 --output region_metadata.h \
           [--depfile regions.d] --sources main.c other.c -- <compile flags>
"""

import argparse
import ast
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


class MetadataError(Exception):
    pass


WRAPPERS = {'ImplicitCastExpr', 'ParenExpr', 'ConstantExpr', 'PredefinedExpr'}


def unwrap(node):
    while node.get('kind') in WRAPPERS and len(node.get('inner', [])) == 1:
        node = node['inner'][0]
    return node


def string_value(node, label='name'):
    node = unwrap(node)
    if node.get('kind') != 'StringLiteral':
        raise MetadataError(f'region {label} must be a constant string')
    try:
        # Clang spells non-ASCII bytes as C octal escapes, even for UTF-8
        # identifiers. Decode bytes first so metadata matches __func__.
        value = ast.literal_eval('b' + node['value']).decode('utf-8')
    except (ValueError, SyntaxError, UnicodeError, AttributeError) as error:
        raise MetadataError(f'unsupported region {label} string literal') from error
    if not value or (label == 'name' and ';' in value) or any(ord(c) < 32 or ord(c) == 127 for c in value):
        raise MetadataError(f'region {label} must be nonempty and contain no control characters'
                            + (' or semicolon' if label == 'name' else ''))
    return value


def integer_value(node):
    # __LINE__ is an IntegerLiteral after preprocessing. Clang also emits a
    # evaluated ConstantExpr for some language-level constant expressions.
    if node.get('kind') in {'IntegerLiteral', 'ConstantExpr'} and 'value' in node:
        try:
            return int(node['value'])
        except ValueError:
            pass
    if node.get('kind') in WRAPPERS and len(node.get('inner', [])) == 1:
        return integer_value(node['inner'][0])
    raise MetadataError('region line must be a constant integer (normally __LINE__)')


def id_token(node):
    node = unwrap(node)
    if node.get('kind') == 'DeclRefExpr':
        declaration = node.get('referencedDecl', {})
        name = declaration.get('name', '')
        if declaration.get('kind') == 'EnumConstantDecl' and re.fullmatch(r'[A-Za-z_][A-Za-z_0-9]*', name):
            return name
    elif node.get('kind') == 'IntegerLiteral':
        value = int(node['value'])
        if 0 <= value <= 2147483647:
            return str(value)
    raise MetadataError('region ID must be a constant enum identifier or nonnegative integer literal')


def extract_regions(tree, source, entries, names):
    # Clang omits a location's file when it is the same as the preceding
    # serialized location. Visit locations in JSON order; includedFrom and
    # presumedFile describe other files and must not change physical identity.
    last_file = str(source.resolve())

    def location(value):
        nonlocal last_file
        if 'spellingLoc' in value:
            location(value['spellingLoc'])
            return location(value['expansionLoc'])
        if 'file' in value:
            last_file = value['file']
        return last_file, value.get('offset')

    def walk(node):
        site = None
        for key, value in node.items():
            if key == 'loc':
                location(value)
            elif key == 'range':
                site = location(value.get('begin', {}))
                location(value.get('end', {}))
            elif isinstance(value, dict):
                walk(value)
            elif isinstance(value, list):
                for child in value:
                    if isinstance(child, dict):
                        walk(child)
        if node.get('kind') != 'CallExpr':
            return
        children = node.get('inner', [])
        if not children:
            return
        callee = unwrap(children[0]).get('referencedDecl', {})
        function = callee.get('name')
        if callee.get('kind') != 'FunctionDecl' or function not in {'region_parallel_start', 'region_for_start'}:
            return
        if len(children) != 6:
            raise MetadataError(f'{source}: unsupported {function} argument count')
        try:
            token = id_token(children[2])
            file = string_value(children[3], 'file')
            name = string_value(children[4])
            line = integer_value(children[5])
            if line <= 0 or line > 2147483647:
                raise MetadataError('region line must be a positive int')
        except MetadataError as error:
            raise MetadataError(f'{source}: {error}') from error
        key = f'{name}:{line}'
        if site is None or site[1] is None or site[0].startswith('<'):
            raise MetadataError(f'{source}: cannot determine physical source location for {key}')
        physical = (str(Path(site[0]).resolve()), site[1])
        previous = names.get(key)
        if previous is not None and previous != (token, physical):
            raise MetadataError(f'ambiguous region name {key}: '
                                f'{previous[1][0]} (ID {previous[0]}, offset {previous[1][1]}) and '
                                f'{physical[0]} (ID {token}, offset {physical[1]})')
        entry = (name, file, line, physical)
        previous = entries.get(token)
        if previous is not None and previous != entry:
            raise MetadataError(f'region ID {token} refers to different source locations or metadata')
        entries[token] = entry
        names[key] = (token, physical)

    walk(tree)


def atomic_write(path, text):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8', dir=path.parent,
                                     prefix=path.name + '.', delete=False) as output:
        temporary = Path(output.name)
        try:
            output.write(text)
            output.close()
            os.replace(temporary, path)
        finally:
            temporary.unlink(missing_ok=True)


def make_quote(path):
    return (str(path).replace('\\', '\\\\').replace('$', '$$')
            .replace('#', '\\#').replace(' ', '\\ ').replace('\t', '\\\t').replace(':', '\\:'))


def main(argv=None):
    arguments = list(sys.argv[1:] if argv is None else argv)
    separator = arguments.index('--') if '--' in arguments else len(arguments)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--clang', default='clang-18')
    parser.add_argument('--output', required=True)
    parser.add_argument('--depfile')
    parser.add_argument('--sources', nargs='+', required=True, type=Path)
    args = parser.parse_args(arguments[:separator])
    flags = arguments[separator + 1:]
    entries = {}
    names = {}
    dependencies = []
    try:
        with tempfile.TemporaryDirectory(prefix='region-metadata-') as temporary:
            temporary = Path(temporary)
            bootstrap = temporary / 'region_metadata.h'
            bootstrap.write_text('#define REGION_INFO(id, parent, combined, nowait) '
                                 '{0, (parent), (combined), 0, 0, (nowait)}\n')
            for index, source in enumerate(args.sources):
                dump = temporary / 'ast.json'
                dep = temporary / f'{index}.d'
                command = [args.clang, '-I' + str(temporary), *flags,
                           '-DREGION_METADATA_SCAN=1', '-fsyntax-only', '-Xclang', '-ast-dump=json']
                if args.depfile:
                    command += ['-MMD', '-MP', '-MF', str(dep), '-MQ', args.output]
                command.append(str(source))
                with dump.open('w') as output:
                    process = subprocess.run(command, stdout=output, stderr=subprocess.PIPE,
                                             text=True, check=False)
                if process.returncode:
                    raise MetadataError(f'Clang failed for {source}:\n{process.stderr.rstrip()}')
                if process.stderr:
                    print(process.stderr, end='', file=sys.stderr)
                # Keep only one translation unit in memory, and do not pipe
                # large AST dumps through subprocess's in-memory stdout.
                with dump.open() as input_file:
                    tree = json.load(input_file)
                extract_regions(tree, source, entries, names)
                del tree
                if args.depfile:
                    # The bootstrap is private to this scan and disappears on
                    # return. The build owns the generated header dependency.
                    quoted = make_quote(bootstrap)
                    dependencies.append(dep.read_text().replace(quoted + ':\n', '').replace(quoted, ''))
                    # -MP adds phony header rules, but omits the main source.
                    # A removed source must not block rebuilding metadata
                    # with the build system's updated translation-unit list.
                    dependencies.append(make_quote(source) + ':\n')
        generated = ('/* Generated by region_control/generate_regions.py; do not edit. */\n'
                     '#ifndef REGION_METADATA_H\n#define REGION_METADATA_H\n\n'
                     '#define REGION_INFO(id, parent, combined, nowait) '
                     'REGION_INFO_EXPAND(id, parent, combined, nowait)\n'
                     '#define REGION_INFO_EXPAND(id, parent, combined, nowait) '
                     'REGION_INFO_##id(parent, combined, nowait)\n\n')
        for token in sorted(entries):
            name, file, line, _ = entries[token]
            generated += (f'#define REGION_INFO_{token}(parent, combined, nowait) '
                          '{ ' + json.dumps(name, ensure_ascii=False)
                          + ', (parent), (combined), ' + json.dumps(file, ensure_ascii=False)
                          + f', {line}, (nowait) }}\n')
        generated += '\n#endif\n'
        if args.depfile:
            atomic_write(args.depfile, ''.join(dependencies))
        atomic_write(args.output, generated)
    except (MetadataError, OSError, json.JSONDecodeError) as error:
        print(f'region metadata: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
