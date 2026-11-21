test_c_args = [
  '-DTEST_FIXTURES_DIR="@0@"'.format(meson.current_source_dir() / 'fixtures'),
  '-DTEST_HELPER_BIN="@0@"'.format(helper_exe.full_path()),
  '-DTEST_POLICY_FILE="@0@"'.format(meson.project_source_root() / 'data' / 'io.github.word_sys.puls_diskinfo.policy'),
]

test_basic = executable('test-basic',
  'test-basic.c',
  c_args: test_c_args,
  dependencies: puls_dep,
)

test('test-basic', test_basic, depends: helper_exe)

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

python3 = find_program('python3', required: false)
if python3.found()
  polkit_validate_script = '''
import sys, xml.etree.ElementTree as ET
root = ET.parse(sys.argv[1]).getroot()
assert root.tag == "policyconfig", f"bad root {root.tag}"
actions = root.findall("action")
assert len(actions) >= 3, f"expected >=3 actions, got {len(actions)}"
XML_LANG = "{http://www.w3.org/XML/1998/namespace}lang"
exec_paths = []
for a in actions:
    aid = a.attrib.get("id", "")
    assert aid.startswith("io.github.word_sys.puls_diskinfo."), f"bad id {aid}"
    dlangs = {d.attrib.get(XML_LANG) for d in a.findall("description") if (d.text or "").strip()}
    mlangs = {m.attrib.get(XML_LANG) for m in a.findall("message") if (m.text or "").strip()}
    assert {None, "en", "tr"}.issubset(dlangs), f"missing description langs in {aid}: {dlangs}"
    assert {None, "en", "tr"}.issubset(mlangs), f"missing message langs in {aid}: {mlangs}"
    defs = a.find("defaults")
    assert defs is not None and defs.findtext("allow_active") == "auth_admin_keep", f"bad allow_active in {aid}"
    assert defs.findtext("allow_any") != "yes" and defs.findtext("allow_inactive") != "yes", f"unsafe defaults in {aid}"
    for ann in a.findall("annotate"):
        if ann.attrib.get("key") == "org.freedesktop.policykit.exec.path":
            exec_paths.append(ann.text)
assert exec_paths == ["/usr/libexec/puls-diskinfo-helper"], f"unexpected exec.path list: {exec_paths}"
print("Polkit policy validation OK:", [a.attrib["id"] for a in actions])
'''
  test('validate-polkit-policy',
    python3,
    args: [
      '-c',
      polkit_validate_script,
      meson.project_source_root() / 'data' / 'io.github.word_sys.puls_diskinfo.policy',
    ],
  )
endif
