#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Offline test-bundle preparation checks; production inputs remain untouched."""
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('prepare', Path(__file__).with_name('prepare-test-runtime.py'))
prepare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prepare)


class PreparationTests(unittest.TestCase):
    def test_private_codec_only_and_fresh_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            formats = root / 'plugins/imageformats'
            formats.mkdir(parents=True)
            for name in ('libqgif.so', 'libqjpeg.so', 'libqtiff.so', 'libqwebp.so'):
                (formats / name).write_bytes(name.encode())
            codec = root / 'synthetic.so'
            codec.write_bytes(b'private-codec')
            helper = root / 'helper'
            helper.write_bytes(b'unchanged-helper')
            output = root / 'bundle'
            args = ['prepare', '--prepare', 'provider-prepare.py', '--helper', str(helper),
                    '--plugins', str(formats.parent), '--codec', str(codec), '--output', str(output)]

            def verify(command, **kwargs):
                staged = Path(command[command.index('--plugins') + 1]) / 'imageformats'
                self.assertEqual((staged / 'libqtiff.so').read_bytes(), b'private-codec')
                self.assertEqual(len(list(staged.iterdir())), 4)
                self.assertEqual(command[command.index('--helper') + 1], str(helper))
                self.assertTrue(kwargs['check'])

            with patch.object(sys, 'argv', args), patch.object(prepare.subprocess, 'run', side_effect=verify) as run:
                prepare.main()
                self.assertEqual(run.call_count, 1)
                output.mkdir()
                with self.assertRaises(SystemExit):
                    prepare.main()
                self.assertEqual(run.call_count, 1)
            self.assertEqual((formats / 'libqtiff.so').read_bytes(), b'libqtiff.so')
            self.assertEqual(helper.read_bytes(), b'unchanged-helper')


if __name__ == '__main__':
    unittest.main()
