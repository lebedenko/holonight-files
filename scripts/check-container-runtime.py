#!/usr/bin/env python3
"""Use the prepared qualified offline acceptance path for installed runtime checks."""
from pathlib import Path
import subprocess
import sys
sys.exit(subprocess.call([sys.executable, str(Path(__file__).parent / 'ci/run.py'), '--lane', 'build']))
