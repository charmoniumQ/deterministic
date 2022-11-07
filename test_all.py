import subprocess
import itertools
from pathlib import Path
import sys
import tempfile

import pytest


@pytest.fixture
def compiled_binary() -> Path:
    with tempfile.TemporaryDirectory() as _path:
        path = Path(_path) / "deterministic_random_preload.so"
        subprocess.run(
            [
                "gcc", "-O2", "-Wall", "-Werror", "-fPIC", "-shared", "-o", path, "deterministic_random_preload.c"
            ],
            check=True,
        )
        yield path


commands = [
    "print(hash('hi'))",
    "import random; print(random.randint(0, 99))",
    "import secrets; print(secrets.randbits(10))",
    "import numpy; print(numpy.random.random(10))",
    "print(id(object()))"
]


@pytest.mark.parametrize("command", commands)
def test_python_scripts(compiled_binary: Path, command: list[str]) -> None:
    prefix = ["setarch", "--addr-no-randomize", "env", f"LD_PRELOAD={compiled_binary}"]
    proc0 = subprocess.run(
        [*prefix, sys.executable, "-c", command],
        check=True,
        capture_output=True,
    ).stdout
    proc1 = subprocess.run(
        [*prefix, sys.executable, "-c", command],
        check=True,
        capture_output=True,
    ).stdout
    assert proc0 == proc1
