import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location('observer_build_se', Path(__file__).resolve().parents[1]/'tools/build_se.py')
build = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(build)


class ModernBuildTests(unittest.TestCase):
    def test_outputs_refuse_any_git_checkout(self):
        with tempfile.TemporaryDirectory() as t:
            root=Path(t);(root/'.git').mkdir()
            with self.assertRaises(ValueError):build.outside_build(root/'nested'/'output')
    def test_header_inventory_detects_mutation_and_membership(self):
        with tempfile.TemporaryDirectory() as t:
            root=Path(t);(root/'a.h').write_text('first')
            before=build.header_inventory([root]);(root/'a.h').write_text('second')
            self.assertNotEqual(before,build.header_inventory([root]))
            (root/'b.hpp').write_text('added');self.assertEqual(len(build.header_inventory([root])),2)
            (root/'a.h').unlink();self.assertEqual(len(build.header_inventory([root])),1)
    def test_pin_refuses_modified_or_missing_library(self):
        with tempfile.TemporaryDirectory() as t:
            p=Path(t)/'library.lib';p.write_bytes(b'exact');pin={'path':str(p),'sha256':hashlib.sha256(b'exact').hexdigest()}
            self.assertEqual(build.pinned_file(pin),p)
            p.write_bytes(b'other')
            with self.assertRaises(ValueError):build.pinned_file(pin)
            p.unlink()
            with self.assertRaises(ValueError):build.pinned_file(pin)
    def test_include_paths_require_headers_and_unambiguous_directory(self):
        with tempfile.TemporaryDirectory() as t:
            with self.assertRaises(ValueError):build.header_inventory([Path(t)])
            with self.assertRaises(ValueError):build.header_inventory([Path(t)/'missing'])


if __name__=='__main__':unittest.main()
