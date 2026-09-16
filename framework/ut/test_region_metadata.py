#!/usr/bin/env python3
"""Exercise shared metadata extraction with real Clang preprocessing and ASTs."""

import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest


FRAMEWORK = Path(__file__).resolve().parents[1]
GENERATOR = FRAMEWORK / 'region_control/generate_regions.py'
CLANG = 'clang-18'


class MetadataTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='region metadata test ')
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.output = self.directory / 'generated/region_metadata.h'
        self.depfile = self.directory / 'regions.d'

    def source(self, name, contents):
        path = self.directory / name
        path.write_text(contents)
        return path

    def generate(self, sources, *flags, success=True):
        result = subprocess.run(
            [sys.executable, str(GENERATOR), '--clang', CLANG,
             '--output', str(self.output), '--depfile', str(self.depfile),
             '--sources', *map(str, sources), '--', '-I' + str(FRAMEWORK / 'region_control'),
             '-I' + str(self.output.parent), '-fopenmp', *flags], text=True, capture_output=True)
        if success:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        return result

    def entries(self):
        pattern = (r'^#define REGION_INFO_(\w+)\(parent, combined, nowait\) '
                   r'\{ (".*?"), \(parent\), \(combined\), (".*?"), (\d+), \(nowait\) \}$')
        return {token: (json.loads(name), json.loads(file), int(line))
                for token, name, file, line in re.findall(pattern, self.output.read_text(), re.M)}

    def compile_and_run(self, source, *flags):
        program = self.directory / 'reader'
        result = subprocess.run(
            [CLANG, '-Werror', '-Wall', '-Wextra', '-I' + str(FRAMEWORK / 'region_control'),
             '-I' + str(self.output.parent), *flags, str(source), '-o', str(program)],
            text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        result = subprocess.run([str(program)], text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return program

    def test_macros_line_directives_conditionals_and_for_regions(self):
        source = self.source('hooks.c', '''#include "region_control.h"
enum { PARALLEL_ID, FOR_ID, ENABLED_ID, DISABLED_ID };
#define WRAPPED(c, id) PARALLEL_START(c, id)
static void unused(region_control *c) {
#line 43 "reported.c"
    WRAPPED(c, PARALLEL_ID);
    FOR_START(c, FOR_ID);
}
void conditional(region_control *c) {
#if ENABLE_REGION
#line 70 "reported.c"
    PARALLEL_START(c, ENABLED_ID);
#else
#line 80 "reported.c"
    PARALLEL_START(c, DISABLED_ID);
#endif
}
''')
        self.generate([source], '-std=c11', '-DENABLE_REGION=1')
        self.assertEqual(self.entries(), {
            'PARALLEL_ID': ('unused', 'reported.c', 43),
            'FOR_ID': ('unused', 'reported.c', 44),
            'ENABLED_ID': ('conditional', 'reported.c', 70),
        })
        self.generate([source], '-std=c11', '-DENABLE_REGION=0')
        self.assertNotIn('ENABLED_ID', self.entries())
        self.assertEqual(self.entries()['DISABLED_ID'], ('conditional', 'reported.c', 80))
        self.assertIn('region_control.h', self.depfile.read_text())
        self.assertIn('hooks.c', self.depfile.read_text())

    def test_cpp_predefined_function_name_and_direct_constant_call(self):
        source = self.source('hooks.cpp', '''#include "region_control.h"
namespace example {
struct Task {
    void work(region_control *c) {
#line 123
        PARALLEL_START(c, 0);
    }
};
}
void direct(region_control *c) {
    region_parallel_start(c, 1, __FILE__, "custom", 456);
}
''')
        self.generate([source], '-std=c++17')
        self.assertEqual(self.entries()['0'], ('work', str(source), 123))
        self.assertEqual(self.entries()['1'], ('custom', str(source), 456))

    def test_disabled_instrumentation_keeps_metadata_but_compiles_no_hooks(self):
        source = self.source('disabled.c', '''#include "region_control.h"
#include <region_metadata.h>
#include <string.h>
static region_info regions[] = { REGION_INFO(0, -1, 1, 0) };
int main(void) {
#line 43
    PARALLEL_START((region_control *)0, 0);
    return strcmp(regions[0].name, "main") || regions[0].line != 43 ||
           regions[0].parent != -1 || regions[0].combined != 1;
}
''')
        self.generate([source], '-DREGION_INSTRUMENT=0')
        self.assertEqual(self.entries()['0'], ('main', str(source), 43))
        program = self.compile_and_run(source, '-std=c11', '-DREGION_INSTRUMENT=0')
        symbols = subprocess.run(['nm', str(program)], check=True, capture_output=True, text=True).stdout
        self.assertNotIn('region_parallel_start', symbols)
        self.assertNotIn('offline', symbols)

    def test_empty_translation_unit_generates_valid_empty_header(self):
        empty = self.source('empty.c', 'int plain_function(void) { return 42; }\n')
        self.generate([empty])
        self.assertEqual(self.entries(), {})
        reader = self.source('reader.c', '#include <region_metadata.h>\nint main(void) { return 0; }\n')
        self.compile_and_run(reader, '-std=c11')

    def test_utf8_function_name_matches_runtime_bytes(self):
        source = self.source('unicode.c', '''#include "region_control.h"
void café(region_control *c) {
#line 43
    PARALLEL_START(c, 0);
}
''')
        self.generate([source], '-std=c11')
        self.assertEqual(self.entries()['0'][0], 'café')

    def test_same_header_site_in_multiple_translation_units_is_deduplicated(self):
        header = self.source('shared.h', '''#include "region_control.h"
static void shared(region_control *c) {
#line 99 "reported.h"
    PARALLEL_START(c, 0);
}
''')
        sources = [self.source(name, '#include "shared.h"\n') for name in ('a.c', 'b.c')]
        self.generate(sources)
        self.assertEqual(self.entries(), {'0': ('shared', 'reported.h', 99)})
        self.assertIn(str(header).replace(' ', '\\ '), self.depfile.read_text())

    def test_different_physical_sites_with_same_key_are_rejected(self):
        contents = '''#include "region_control.h"
static void same(region_control *c) {
#line 43 "same-presumed-file.c"
    PARALLEL_START(c, 0);
}
'''
        sources = [self.source(name, contents) for name in ('a.c', 'b.c')]
        result = self.generate(sources, success=False)
        self.assertIn('ambiguous region name same:43', result.stderr)
        self.assertFalse(self.output.exists())

    def test_two_sites_on_the_same_line_are_rejected(self):
        source = self.source('same-line.c', '''#include "region_control.h"
void same(region_control *c) {
#line 43
    PARALLEL_START(c, 0); FOR_START(c, 1);
}
''')
        result = self.generate([source], success=False)
        self.assertIn('ambiguous region name same:43', result.stderr)

    def test_same_id_at_different_sites_is_rejected(self):
        source = self.source('reused.c', '''#include "region_control.h"
enum { SAME_ID };
void first(region_control *c) { PARALLEL_START(c, SAME_ID); }
void second(region_control *c) { PARALLEL_START(c, SAME_ID); }
''')
        result = self.generate([source], success=False)
        self.assertIn('region ID SAME_ID refers to different source locations', result.stderr)

    def test_dynamic_id_name_file_or_line_is_not_silently_omitted(self):
        for index, values in enumerate([
            ('id', '__FILE__', '"known"', '43'),
            ('0', 'file', '"known"', '43'),
            ('0', '__FILE__', 'name', '43'),
            ('0', '__FILE__', '"known"', 'line'),
        ]):
            with self.subTest(values=values):
                source = self.source(f'dynamic{index}.c', '''#include "region_control.h"
void dynamic(region_control *c, int id, const char *name, const char *file, int line) {
    region_parallel_start(c, ''' + ', '.join(values) + ''');
}
''')
                self.output.parent.mkdir(exist_ok=True)
                self.output.write_text('previous metadata\n')
                result = self.generate([source], success=False)
                self.assertIn('must be a constant', result.stderr)
                self.assertEqual(self.output.read_text(), 'previous metadata\n')

    def test_clang_error_preserves_previous_metadata_and_depfile(self):
        source = self.source('invalid.c', 'this is not C;\n')
        self.output.parent.mkdir()
        self.output.write_text('previous metadata\n')
        self.depfile.write_text('previous dependencies\n')
        result = self.generate([source], success=False)
        self.assertIn('Clang failed', result.stderr)
        self.assertEqual(self.output.read_text(), 'previous metadata\n')
        self.assertEqual(self.depfile.read_text(), 'previous dependencies\n')

    def test_depfile_does_not_block_removed_sources_or_headers(self):
        header = self.source('temporary.h', 'void declared(void);\n')
        source = self.source('removed.c', '#include "temporary.h"\n')
        self.generate([source])
        makefile = self.source('Makefile', 'all:\ninclude regions.d\n')
        source.unlink()
        header.unlink()
        process = subprocess.run(['make', '-f', str(makefile), '-n', str(self.output)],
                                 cwd=self.directory, text=True, capture_output=True)
        self.assertEqual(process.returncode, 0, process.stdout + process.stderr)

    def test_fresh_build_bootstrap_and_cpp_static_metadata(self):
        source = self.source('hooks.cpp', '''#include "region_control.h"
#include <region_metadata.h>
enum { PARALLEL_ID, FOR_ID };
region_info regions[] = {
    REGION_INFO(PARALLEL_ID, -1, 0, 0),
    REGION_INFO(FOR_ID, PARALLEL_ID, 0, 1),
};
void check(region_control *c) {
#line 43 "origin.cpp"
    PARALLEL_START(c, PARALLEL_ID);
    FOR_START(c, FOR_ID);
}
''')
        self.assertFalse(self.output.exists())
        self.generate([source], '-std=c++17')
        self.assertEqual(self.entries(), {
            'PARALLEL_ID': ('check', 'origin.cpp', 43),
            'FOR_ID': ('check', 'origin.cpp', 44),
        })
        self.assertNotIn('region-metadata-', self.depfile.read_text())
        reader = self.source('reader.cpp', '''#include "region_control.h"
#include <region_metadata.h>
#include <cstring>
enum { PARALLEL_ID, FOR_ID };
static region_info regions[] = {
    REGION_INFO(PARALLEL_ID, -1, 0, 0),
    REGION_INFO(FOR_ID, PARALLEL_ID, 0, 1),
};
int main() {
    return std::strcmp(regions[0].name, "check") || regions[0].line != 43 ||
           std::strcmp(regions[0].file, "origin.cpp") || regions[0].parent != -1 ||
           std::strcmp(regions[1].name, "check") || regions[1].line != 44 ||
           regions[1].parent != PARALLEL_ID || regions[1].nowait != 1;
}
''')
        self.compile_and_run(reader, '-std=c++17', '-lstdc++')
        # A corrupt old header must not prevent regenerating metadata.
        self.output.write_text('this is not a C header;\n')
        self.generate([source], '-std=c++17')
        self.compile_and_run(reader, '-std=c++17', '-lstdc++')

    def test_macro_integer_id_expands_in_generated_initializers(self):
        source = self.source('numeric.c', '''#include "region_control.h"
#define MY_REGION 2
void check(region_control *c) { PARALLEL_START(c, MY_REGION); }
''')
        self.generate([source])
        self.assertEqual(set(self.entries()), {'2'})
        reader = self.source('reader.c', '''#include "region_control.h"
#include <region_metadata.h>
#define MY_REGION 2
static region_info value = REGION_INFO(MY_REGION, -1, 1, 0);
int main(void) { return value.line != 3 || value.parent != -1 || value.combined != 1; }
''')
        self.compile_and_run(reader, '-std=c11')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument('--clang', default=CLANG)
    arguments, remaining = parser.parse_known_args()
    CLANG = arguments.clang
    unittest.main(argv=[sys.argv[0], *remaining])
