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

## Portable Docker preflight

Before moving long runs to any Docker-capable host, run:

```bash
scripts/fuzzpilot_docker.sh smoke
```

The smoke builds the image for the selected platform, checks CTest during image
build, validates target configs inside the container, rebuilds target harnesses
inside the image, and runs a short cJSON baseline campaign.

For paper-comparable data, force the canonical platform:

```bash
FUZZPILOT_DOCKER_PLATFORM=linux/amd64 scripts/fuzzpilot_docker.sh smoke
```

Native runs are still possible, but then you must initialize submodules and
rebuild target binaries on that machine. Do not reuse checked-in binaries across
CPU/OS boundaries.
