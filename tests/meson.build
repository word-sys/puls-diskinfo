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

desktop_file_validate = find_program('desktop-file-validate', required: false)
if desktop_file_validate.found()
  test('validate-desktop',
    desktop_file_validate,
    args: [meson.project_source_root() / 'data' / 'io.github.word_sys.puls_diskinfo.desktop'],
  )
endif

appstreamcli = find_program('appstreamcli', required: false)
if appstreamcli.found()
  test('validate-metainfo',
    appstreamcli,
    args: ['validate', '--no-net', '--pedantic', meson.project_source_root() / 'data' / 'io.github.word_sys.puls_diskinfo.metainfo.xml'],
  )
endif
