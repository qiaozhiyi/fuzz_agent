# FuzzPilot Experiments

This directory is reserved for target-specific experiment manifests, run notes, and
small reproducibility checklists. Large AFL++ outputs should live outside git or
under an ignored work directory.

## M6 matrix

Use the CLI matrix generator to create auditable long-run plans:

```bash
./build/fuzzpilot m6-matrix \
  --config experiments/targets/cjson/config.yaml \
  --config experiments/targets/libpng/config.yaml \
  --out-dir results/m6_matrix \
  --work-dir work_m6 \
  --repeats 3 \
  --main-budget-sec 86400 \
  --micro-budget-sec 300
```

The generated matrix includes baseline AFL++, rule-only, no-static-analysis,
no-mutator, and full-agent runs for each target.

## Ubuntu/x86 preflight

Before moving long runs to an x86_64 Ubuntu cloud server, run:

```bash
scripts/docker_ubuntu_smoke.sh
```

The smoke builds under Ubuntu, checks CTest, verifies the Linux
`libfuzzpilot_mutator.so`, rebuilds the bundled target harnesses as ELF/x86-64,
and confirms the shared configs use the extensionless `libfuzzpilot_mutator`
path.

Before running the full cJSON/libpng matrix on a cloud server, initialize target
submodules and rebuild Linux target binaries:

```bash
git submodule update --init --recursive
scripts/build_ubuntu_targets.sh
```

If a submodule is unavailable, install the matching system development package
(`libcjson-dev` or `libpng-dev`) and rerun the build helper. Do not reuse the
checked-in macOS arm64 target binaries for Ubuntu/x86_64 long runs.
