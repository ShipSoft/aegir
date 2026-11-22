#!/bin/bash
# Extra activation for the coverage environment. pixi sources a feature's
# scripts before the workspace's activate.sh, which reads AEGIR_BUILD_DIR to
# put the instrumented build in build-coverage/ on PHLEX_PLUGIN_PATH.

export AEGIR_BUILD_DIR=build-coverage
