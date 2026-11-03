test_c_args = [
  '-DTEST_FIXTURES_DIR="@0@"'.format(meson.current_source_dir() / 'fixtures')
]

test_basic = executable('test-basic',
  'test-basic.c',
  c_args: test_c_args,
  dependencies: puls_dep,
)

test('test-basic', test_basic)

test_smart_parser = executable('test-smart-parser',
  'test-smart-parser.c',
  c_args: test_c_args,
  dependencies: puls_dep,
)

test('test-smart-parser', test_smart_parser)
