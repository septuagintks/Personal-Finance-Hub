#!/usr/bin/env bash

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_root="${PFH_SANITIZER_BUILD_ROOT:-${root}/build/sanitizers}"
postgresql="${PFH_SANITIZER_POSTGRESQL:-OFF}"
parallel="${PFH_BUILD_PARALLELISM:-2}"

case "${postgresql}" in
    ON|OFF) ;;
    *)
        echo "PFH_SANITIZER_POSTGRESQL must be ON or OFF" >&2
        exit 2
        ;;
esac

case "${parallel}" in
    ''|*[!0-9]*|0)
        echo "PFH_BUILD_PARALLELISM must be a positive integer" >&2
        exit 2
        ;;
esac

run_matrix_entry() {
    local name="$1"
    local address="$2"
    local undefined="$3"
    local build_dir="${build_root}/${name}"

    cmake -S "${root}" -B "${build_dir}" -G Ninja \
        -DCMAKE_BUILD_TYPE=Debug \
        -DPFH_BUILD_POSTGRESQL="${postgresql}" \
        -DPFH_ENABLE_ADDRESS_SANITIZER="${address}" \
        -DPFH_ENABLE_UNDEFINED_SANITIZER="${undefined}"
    cmake --build "${build_dir}" --parallel "${parallel}"

    if [[ "${address}" == "ON" ]]; then
        ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1:halt_on_error=1:strict_string_checks=1}" \
            ctest --test-dir "${build_dir}" --output-on-failure
    else
        UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}" \
            ctest --test-dir "${build_dir}" --output-on-failure
    fi
}

run_matrix_entry address ON OFF
run_matrix_entry undefined OFF ON

echo "PFH sanitizer matrix: PASS postgresql=${postgresql}"
