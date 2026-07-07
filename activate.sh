#!/bin/bash
# Pixi activation script for aegir.
# Sourced automatically by `pixi run` / `pixi shell`.

export AEGIR_ROOT="$PIXI_PROJECT_ROOT"

# Locally built plugins first, then installed plugins from the pixi env.
export PHLEX_PLUGIN_PATH="$PIXI_PROJECT_ROOT/build:${CONDA_PREFIX}/lib${PHLEX_PLUGIN_PATH:+:$PHLEX_PLUGIN_PATH}"
export LD_LIBRARY_PATH="$PIXI_PROJECT_ROOT/build${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

# Route phlex allocations (including dlopened Geant4/plugin libraries)
# through tcmalloc: glibc malloc arena contention costs several percent of
# wall time in multi-threaded Geant4 runs (see docs/benchmarks.md). The
# preload is scoped to the phlex binary via a PATH shim rather than exported
# env-wide: a global LD_PRELOAD crashes binaries built against a different
# glibc (e.g. Nix store tools spawned by cmake). tcmalloc, not jemalloc:
# conda-forge's jemalloc uses general-dynamic TLS, so once phlex dlopens the
# Geant4 plugins its free() recurses through glibc's DTV update until the
# stack overflows (see docs/benchmarks.md). tcmalloc uses initial-exec TLS
# and is preload-safe.
# Set AEGIR_NO_TCMALLOC=1 to opt out (e.g. for allocator-specific profiling).
if [ -z "${AEGIR_NO_TCMALLOC:-}" ] && [ -f "$CONDA_PREFIX/lib/libtcmalloc.so.4" ]; then
    export PATH="$PIXI_PROJECT_ROOT/scripts/shims:$PATH"
fi

# shipgeometry installs geometry DBs under $CONDA_PREFIX/share/geometry/.
# geometry_geomodel_provider resolves bare db_file names via this variable.
export SHIPGEOMETRY_ROOT="${SHIPGEOMETRY_ROOT:-$CONDA_PREFIX}"

# SHiPFieldService resolves bare .cvf filenames via $SHIPFIELD_ROOT/share/field/.
# Kept distinct from SHIPGEOMETRY_ROOT so field maps and geometry can be
# versioned independently.
export SHIPFIELD_ROOT="${SHIPFIELD_ROOT:-$CONDA_PREFIX}"

# gmex (GeoModelExplorer) workaround: in conda-forge geomodel-visualization
# 6.27.0 the install prefix is baked in as a NUL-padded literal via binary
# prefix replacement, so derived paths truncate at $CONDA_PREFIX and gmex
# tries to ifstream the env directory itself -> SIGABRT. GXSHAREDIR is checked
# before the baked-in path, so pointing it at the real share dir restores
# normal startup. Drop once a fixed feedstock build is available.
export GXSHAREDIR="${GXSHAREDIR:-$CONDA_PREFIX/share/gmex}"
