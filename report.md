# A case study of non-determinism in Python programs

## The program

The program implements an algorithm called [kriging](https://en.wikipedia.org/wiki/Kriging). The program can be found [here](https://github.com/cniddodi/NOGGIN).

It took me about 4 hours to set up the software environment. A Dockerfile for that environment can be found in Appendix A.

## Sources of non-determinism

Here are the sites of non-determinism. I will refer to these as NDS1 (Non-Determinism Source #1), NDS2, etc.

1. `print(ds.attrs)` at `Krige/DataField.py:789` appears to depend on pointer values.

2. `for i,v in boxes.items()` at `Krige/noggin_krige.py` depends on arbitrary iteration order. Empirically, iteration order seems to depend on `PYTHONHASHSEED` and the address-space layout (see Appendix B).

3. `np.random.uniform` at `Krige/core.py:103` and `np.random.permutation` at `990` depends on the state of Numpy's PRNG.

4. Some lines in the output contain the current date and time.

There could be others, but mitigating these was sufficient to mitigate those in my experiment.

It took me about 4 hours to track down these sources of non-determinism. It took me about 3 hours to "minimize" my modifications to the fewest required for determinism.

## Mitigations of non-determinism

I rely mostly on an OS-level approach:

1. Use `libfaketime` to mitigate NDS4. [Libfaketime](https://github.com/wolfcw/libfaketime) intercepts libc calls to get the current time and returns a predefined value. This involves installing `libfaketime` and adding two environment variables: `LD_PRELOAD=/path/to/libfaketime.so` and `FAKETIME=2022-01-01 00:00:00`

2. Set `PYTHONHASHSEED` environment variable.

3. Disable address-space layout randomization. Barring other sources of non-determinism, disabling ASLR gives pointers a deterministic value. ASLR in Linux can be disabled with `setarch $(arch) --addr-no-randomize $command`.

Notice that setting `PYTHONHASHSEED` and disabling ASLR yields a deterministic iteration order in my experiments (see Appendix B), which mitigates NDS2.

Here is the full command:

```shell
env \
	PYTHONHASHSEED=0 \
	FAKETIME="2022-01-01 00:00:00" \
	LD_PRELOAD=/usr/lib/x86_64-linux-gnu/faketime/libfaketime.so.1 \
	setarch $(arch) --addr-no-randomize \
	$rest_of_command
```

I tried to patch the source code as little as possible. I only need two changes
at the source-code level:

1. Remove `ds.attrs`. This mitigates NDS1. I was hoping that disabling
   address-space randomization would take care of the non-deterministic pointer
   that appear in the output of this line, but it did not, so instead I am
   forced to modify the source code.

   ```diff
   diff --git a/Krige/DataField.py b/Krige/DataField.py
   index fdb2a9f..f86a8df 100644
   --- a/Krige/DataField.py
   +++ b/Krige/DataField.py
   @@ -789 +789 @@ custom_loader=None.  A callable(self) that allows a user to write a
   -            print('ds.attrs: ',ds.attrs)
   +            #print('ds.attrs: ',ds.attrs)
   ```

2. Seed the numpy PRNG. This mitigates NDS3

   ```diff
   diff --git a/Krige/noggin_krige.py b/Krige/noggin_krige.py
   index ee322cb..648c5d4 100644
   --- a/Krige/noggin_krige.py
   +++ b/Krige/noggin_krige.py
   @@ -38,0 +39,4 @@ You should have received a copy of the GNU General Public License along with thi
   +import os
   +import numpy
   +numpy.random.seed(int(os.environ.get("NUMPY_RANDOM_SEED", "0")))
   +
   ```

## Three general approaches to mitigating non-determinism in Python programs

### Source-code level

If one can change the source code, this is the most reliable option.  One has no need for OS-specific functionality, no need for hacky monkey-patching.  One keeps the security benefits of address layout randomization.

- `blah.items()` -> `sorted(blah.items())`

- Could seed PRNG object globally, but this is [not best   practice](https://builtin.com/data-science/numpy-random-seed). Instead, create   a dedicated PRNG object for your program or subroutine and seed that.

- Don't write raw `id(...)` values in the output.

### Operating system level

The operating system level works for programs besides Python.  This is necessary if the Python program creates another process, and that process exhibits non-determinism.  However, these workarounds involve tinkering with the OS in unusual ways.  This could be implemented as a wrapper around the interpreter: `deterministic_python script.py`.

- `libfaketime` mitigates non-determinism in the current time. This only works in  platforms supported by libfaketime (most UNIX platforms).

- Bind mount `/dev/{,u}random` with seeded PRNG data in a chroot. This only   works on UNIX platforms with a `chroot`. Since Numpy and Python seed their PRNG from `/dev/{,u}random` (see Appendix B), this would transparently yield deterministic PRNG without changing the source code of a program.

- Run with `setarch --addr-no-randomize`. This only works in Linux. Furthermore,   disabling address-layout randomization reduces security and can cause unpredictable performance.

- Run in a container without internet access.

- Use a CDE-like container to make sure the filesystem state is identical to previous runs.

### Python VM level

Most of these options can be implemented in a module that the user would import as the first line of their script.  The module would modify the Python VM to make subsequent evaluations more deterministic.  However, these may not help if the non-determinism is coming from a C-extension of Python, e.g. a program that uses a C random-number generator.  This could be implemented as an `import patch_nondeterminism` or a wrapper around the interpreter `deterministic_python script.py`.

- Monkeypatch `id(obj)`

  ```python
  import builtins
  real_id = builtins.id
  fake_id_table = {}
  def fake_id(obj):
      if real_id(obj) not in fake_id_table:
          fake_id_table[real_id(obj)] = len(fake_id_table)
      return fake_id_table[real_id(obj)]
  builtins.id = fake_id
  
  # Unsuspecting consumer
  print(id(foo)) # prints 1
  bar = foo
  print(id(bar)) # prints 1
  print(id(baz)) # prints 2
  ```

This essentially uses a deterministic counter as the ID instead of the pointer value. Objects with the `real_id(obj)` will map to the same spot in `fake_id_table` and thus the same `fake_id(obj)`, so code won't be able to tell the difference.

- Monkeypatch `datetime.datetime.now()`

  ```python
  import datetime
  datetime.datetime.now = lambda: datetime.datetime(year=2022, month=1, day=1)
  
  # Unspsecting consumer
  import datetime
  # The Python interpreter returns the module object if one already exists.
  print(datetime.datetime.now()) # prints 2022-01-01
  ```

- Seed PRNGs globally to an environment variable or 0 if that variable is unset.

- Set environment variable `PYTHONHASHSEED`. This lies outside the Python interpreter, but it is supported on every operating system.

## Future work

One could do a more systematic analysis like this one, with more a priori justification and more case-studies. Future work could do this and implement a `deterministic_python` wrapper around the Python interpreter.

For this report, I didn't go through the work of bind-mounting deterministic pseudo-random data to `/dev/{,u}random`. If I had, I might not even need to seed the Numpy PRNG or set `PYTHONHASHSEED`, which draw from `/dev/{,u}random`. Moreover, unlike the Numpy PRNG and `PYTHONHASHSEED`, `/dev/{,u}random` is not Python-specific. If other programs or interpreters use the same trick to seed their PRNG state, this solution would transparently carry over to them. Ideally, one could create a script that runs command in a totally deterministic way, with CDE, `libfaketime`, fixed `/dev{,u}random`, fixed environment variables, single-processor schedule, inside a container,
etc. However, it would tradeoff performance, security, and storing auxiliary data. I would draw on prior work from [Guo and Engler 2011](https://www.usenix.org/legacy/event/lisa11/tech/full_papers/Guo.pdf) and [Davison 2012](https://ieeexplore.ieee.org/document/6180156).

```
deterministic-first-run      ./non_deterministic_script args
deterministic-subsequent-run ./non_deterministic_script args
```

Getting the same computation on different machines is a related but more difficult challenge than getting the same computation on the same machine. This process would be useful for scientific publishing of computational experiments.

# Appendix A: Environment to run original code

Save this as `Dockerfile`:

```dockerfile
FROM ubuntu:16.04

RUN apt-get update && apt-get install -y \
	m4 \
	gcc \
	g++ \
	pkg-config \
	wget \
	make \
	git \
	libhdf4-dev \
	libgeos-dev \
	libproj-dev \
	proj-data \
	proj-bin \
	python3 \
	python3-pip \
	python3-tk \
	time \
	libhdf5-dev \
	libfaketime

RUN python3 -m pip install \
	pkgconfig \
	pip==20.2.4 \
	numpy==1.18.5 \
	Cython==0.29.21 \
	setuptools==50.3.2 \
	scipy==1.4.1 \
	pyshp==2.1.2 \
	kiwisolver==1.1.0 \
	shapely==1.7.1 \
	h5py==2.10.0

RUN python3 -m pip install \
	matplotlib==3.0.3 \
	git+https://github.com/matplotlib/basemap.git@v1.2.1rel

RUN python3 -m pip install \
	git+https://github.com/michaelleerilee/PyKrige@master_noggin \
	cftime==1.2.1 \
	pyhdf==0.10.2 \
	cartopy==0.19.0.post1

WORKDIR /workdir

CMD env \
	PYTHONPATH=/workdir/data/NOGGIN \
	python3 \
	/workdir/data/NOGGIN/Krige/noggin_krige.py \
	-d /workdir/data/OMI_Aura_L2_OMO3PR/ \
	-n HDFEOS/SWATHS/O3Profile/Data\ Fields/O3 \
	-m gamma_rayleigh_nuggetless_variogram_model \
	-v
```

Save this as `docker_script.sh`

```bash
#!/usr/bin/bash

set -x -e

date=$(date +%s)

docker build docker --tag noggin
docker run --interactive --tty --rm --volume ${PWD}:/workdir noggin > stdout-${date}-0.txt
mv noggin_krige.hdf noggin_krige-${date}-0.hdf
docker run --interactive --tty --rm --volume ${PWD}:/workdir noggin > stdout-${date}-1.txt
mv noggin_krige.hdf noggin_krige-${date}-1.hdf

diff       stdout-${date}-0.txt       stdout-${date}-1.txt >       stdout-${date}.diff
diff noggin_krige-${date}-0.txt noggin_krige-${date}-1.txt > noggin_krige-${date}.diff
nix shell nixpkgs#hdf5 --command h5diff noggin_krige-${date}-0.hdf noggin_krige-${date}-1.hdf /HDFEOS > noggin_krige-${date}.h5diff
```

Finally, run `bash ./docker_script.sh`.

# Appendix B: Experiments on the factors influencing non-deterministic behavior

- Here is the source code which shows that if `PYTHONHASHSEED` is not set explicitly, the Python interpreter uses `getrandom`, `getentropy` if that fails and `/dev/urandom` if that fails.

1. https://github.com/python/cpython/blob/8563966be4f171ccf615105ef9d3a5aa65a1de68/Python/initconfig.c#L1606
2. https://github.com/python/cpython/blob/8563966be4f171ccf615105ef9d3a5aa65a1de68/Python/bootstrap_hash.c#L568
3. https://github.com/python/cpython/blob/ea1a54506b4ac38b712ba63ec884292025f16111/Python/bootstrap_hash.c#L473 (`pyurandom`)
  - If `PY_GETRANDOM` or `PY_GETENTROPY`
    1. https://github.com/python/cpython/blob/ea1a54506b4ac38b712ba63ec884292025f16111/Python/bootstrap_hash.c#L85
    2. Which calls functions defined by Glibc or `syscall(SYS_getrandom, ...)`
	3. https://www.gnu.org/software/gnulib/manual/html_node/Glibc-sys_002frandom_002eh.html
  - Otherwise
    1. https://github.com/python/cpython/blob/ea1a54506b4ac38b712ba63ec884292025f16111/Python/bootstrap_hash.c#L300
	2. Which reads `/dev/urandom`

- Here is the source code which shows that if `random.seed` is not called, Python's stdlib uses `/dev/urandom`, `getrandom`, `getentropy` and {`pid`, current time} if that fails.

1. https://github.com/python/cpython/blob/ea1a54506b4ac38b712ba63ec884292025f16111/Lib/random.py#L119
2. https://github.com/python/cpython/blob/ea1a54506b4ac38b712ba63ec884292025f16111/Lib/random.py#L163
3. https://github.com/python/cpython/blob/ea1a54506b4ac38b712ba63ec884292025f16111/Modules/_randommodule.c#L354
4. https://github.com/python/cpython/blob/ea1a54506b4ac38b712ba63ec884292025f16111/Modules/_randommodule.c#L271
  - remember `PyObject *arg` is `Py_None`
4. https://github.com/python/cpython/blob/ea1a54506b4ac38b712ba63ec884292025f16111/Modules/_randommodule.c#L280
  - Which calls the `pyurandom`
    1. https://github.com/python/cpython/blob/ea1a54506b4ac38b712ba63ec884292025f16111/Modules/_randommodule.c#L240
    2. https://github.com/python/cpython/blob/ea1a54506b4ac38b712ba63ec884292025f16111/Python/bootstrap_hash.c#L541
    3. Which I traced already for `PYTHONHASHSEED`
  - And if that fails, it uses the PID and time
    1. https://github.com/python/cpython/blob/ea1a54506b4ac38b712ba63ec884292025f16111/Modules/_randommodule.c#L285
	2. https://github.com/python/cpython/blob/ea1a54506b4ac38b712ba63ec884292025f16111/Modules/_randommodule.c#L252

- Here is the source code which shows that if `numpy.random.random` is not explicitly seeded, it uses `/dev/{,u}random` or the current time for a seed.

1. https://github.com/numpy/numpy/blob/2524a53ba30c1207770a27a513eb18a33837a145/numpy/random/__init__.py#L190
2. https://github.com/numpy/numpy/blob/2524a53ba30c1207770a27a513eb18a33837a145/numpy/random/mtrand.pyx#L4752
3. https://github.com/numpy/numpy/blob/2524a53ba30c1207770a27a513eb18a33837a145/numpy/random/mtrand.pyx#L4720
4. https://github.com/numpy/numpy/blob/2524a53ba30c1207770a27a513eb18a33837a145/numpy/random/mtrand.pyx#L148
5. https://github.com/numpy/numpy/blob/2524a53ba30c1207770a27a513eb18a33837a145/numpy/random/_mt19937.pyx#L128
6. https://github.com/numpy/numpy/blob/2524a53ba30c1207770a27a513eb18a33837a145/numpy/random/bit_generator.pyx#L507
7. https://github.com/numpy/numpy/blob/2524a53ba30c1207770a27a513eb18a33837a145/numpy/random/bit_generator.pyx#L297
8. https://github.com/python/cpython/blob/ea1a54506b4ac38b712ba63ec884292025f16111/Lib/secrets.py#L23
9. https://github.com/python/cpython/blob/ea1a54506b4ac38b712ba63ec884292025f16111/Lib/random.py#L765
10. https://github.com/python/cpython/blob/ea1a54506b4ac38b712ba63ec884292025f16111/Modules/posixmodule.c#L12993
11. https://github.com/python/cpython/blob/ea1a54506b4ac38b712ba63ec884292025f16111/Python/bootstrap_hash.c#L527
12. which calls back from `pyurandom`, which I traced above for `PYTHONHASHSEED`.

- Here is an experiment which shows that `id(...)` and iteration order depend on the address-space layout and `PYTHONHASHSEED`.

```python
import sys
import subprocess

def test(set_phs, set_aslr, script):
    command = [sys.executable, "-c", script]
    if set_phs:
        command = ["env", "PYTHONHASHSEED=0", *command]
    if not set_aslr:
        command = ["setarch", "--addr-no-randomize", *command]
    return subprocess.run(
        command,
        check=True,
        capture_output=True,
    ).stdout

scripts = {
    "id_values": "print(id([]))",
    "iteration_order": "print(set('abc'))",
}

max_iter = 50
for set_phs in [True, False]:
    for set_aslr in [True, False]:
        for script_name, script in scripts.items():
            first = test(set_phs, set_aslr, script)
            deterministic = True
            for _ in range(max_iter):
                current = test(set_phs, set_aslr, script)
                if first != current:
                    deterministic = False
                    break
            print(
                f"test(set_phs={set_phs!s}, set_aslr={set_aslr!s}, script=scripts[{script_name!r}])",
                "deterministic" if deterministic else "non-deterministic",
            )
        print()
```

My results were as follows, which shows that combinations other than `(set_phs=True, set_aslr=False)` are non-deterministic.

```
test(set_phs=True, set_aslr=True, script=scripts['id_values']) non-deterministic
test(set_phs=True, set_aslr=True, script=scripts['iteration_order']) deterministic

test(set_phs=True, set_aslr=False, script=scripts['id_values']) deterministic
test(set_phs=True, set_aslr=False, script=scripts['iteration_order']) deterministic

test(set_phs=False, set_aslr=True, script=scripts['id_values']) non-deterministic
test(set_phs=False, set_aslr=True, script=scripts['iteration_order']) non-deterministic

test(set_phs=False, set_aslr=False, script=scripts['id_values']) non-deterministic
test(set_phs=False, set_aslr=False, script=scripts['iteration_order']) non-deterministic
```
