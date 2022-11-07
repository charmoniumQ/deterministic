import subprocess
import itertools
from pathlib import Path
import tempfile

import pytest


@pytest.fixture
def compiled_binary() -> Path:
    with tempfile.TemporaryDirectory() as _path:
        path = Path(_path) / "deterministic_random_preload.so"
        subprocess.run(
            [
                "gcc", "-Wall", "-Werror", "-fPIC", "-shared", "-o", path, "deterministic_random_preload.c"
            ],
            check=True,
        )
        yield path


commands = [
    ["head", "-c", "100", "/dev/urandom"],
    ["head", "-c", "100", "/dev/random"],
    # ["sh", "-c", "strings -s '' /dev/random | head -c 100"],
    # ["sh", "-c", "strings -s '' /dev/urandom | head -c 100"],
    # ["python", "-c", "import random; print(random.randint(0, 99))"],
    # ["python", "-c", "print(hash('hi'))"],
]


@pytest.mark.parametrize("use_determinism,command", list(itertools.product([True, False], commands)))
def test_command(compiled_binary: Path, use_determinism: bool, command: list[str]) -> None:
    if use_determinism:
        prefix = ["setarch", "--addr-no-randomize", "env", f"LD_PRELOAD={compiled_binary}"]
    else:
        prefix = []
    proc0 = subprocess.run(
        [*prefix, *command],
        check=True,
        capture_output=True,
    ).stdout
    proc1 = subprocess.run(
        [*prefix, *command],
        check=True,
        capture_output=True,
    ).stdout
    assert use_determinism == (proc0 == proc1)
