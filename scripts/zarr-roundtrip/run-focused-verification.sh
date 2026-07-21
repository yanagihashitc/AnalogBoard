#!/usr/bin/env bash
set -euo pipefail

readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly REPOSITORY_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
readonly BUILD_HELPER="${REPOSITORY_ROOT}/.claude/skills/msvc-build/scripts/build.sh"
readonly PROTOTYPE_BUILD_ROOT="${REPOSITORY_ROOT}/.deps/p0-s/prototype"
readonly GCSA_SNAPSHOT_ROOT="${REPOSITORY_ROOT}/.deps/p0-s/gcsa-20689a991697217518ec2ff15aaaa2533b169eb0"
readonly ACCEPTED_KAT="${GCSA_SNAPSHOT_ROOT}/src/gcsa/store/data/aead_v1_kat.json"
readonly EXPECTED_KAT_SHA256="cd0ee69428b483ddff4a10a84d15732ed9a7aabd2b85c99adbb97168f8fe60aa"
readonly GCSA_CONTAINER="gcsa-dev"
readonly EXPECTED_GCSA_CONTAINER_ID="d141d00e5edb0bd17ee37836340a4315343019d32db4f9197322e9a3a5c9e1d8"
readonly EXPECTED_GCSA_IMAGE_ID="sha256:e65e9f8b0ffafef5b5d2b9711c9a3411649ae80fd036cc79f0febb80b4c0b06e"
readonly GCSA_CONTAINER_SNAPSHOT="/tmp/gcsa-validation-20689a99-codex"
readonly GCSA_CONTAINER_REPOSITORY_ROOT="/home/jupyter/AnalogBoard"
readonly GCSA_CONTAINER_VALIDATOR_DIR="/home/jupyter/AnalogBoard/prototypes/zarr-store-roundtrip/scripts"
readonly EXPECTED_GCSA_CONTRACT_ID="gcsa-store-a4a-rc1"
readonly EXPECTED_GCSA_PACKAGE_SHA256="c63c79c4add3a8034cd1486921470818ad71d024ace1e8e356ae4f8dbf396d14"
readonly JOINT_GOLDEN="${REPOSITORY_ROOT}/docs/reference/zarr-store-contract/phase0-roundtrip/joint-roundtrip-golden.json"
readonly JOINT_ARTIFACT_PARENT="${REPOSITORY_ROOT}/artifacts/phase0-zarr-roundtrip/focused"

usage() {
  echo "usage: $0 batch1|cpp|python|gcsa-kat|joint" >&2
}

require_identity() {
  local actual
  actual="$(sha256sum "${ACCEPTED_KAT}" | awk '{print $1}')"
  if [[ "${actual}" != "${EXPECTED_KAT_SHA256}" ]]; then
    echo "accepted gcsa KAT SHA-256 mismatch: ${actual}" >&2
    return 1
  fi
}

run_cpp() {
  local configuration
  local build_root
  local windows_build_root
  for configuration in \
      release-approved debug-approved release-reproduced debug-reproduced; do
    build_root="${PROTOTYPE_BUILD_ROOT}/${configuration}"
    if [[ ! -f "${build_root}/CMakeCache.txt" ]]; then
      echo "configured prototype build is absent: ${build_root}" >&2
      return 1
    fi
    windows_build_root="$(wslpath -w "${build_root}")"
    "${BUILD_HELPER}" raw -- cmake --build "${windows_build_root}" --verbose
    "${BUILD_HELPER}" raw -- ctest --test-dir "${windows_build_root}" \
      --no-tests=error -V
  done
}

run_python() {
  PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover \
    -s "${REPOSITORY_ROOT}/prototypes/zarr-store-roundtrip/tests" \
    -p 'test_*.py' -v
}

require_gcsa_container_identity() {
  local container_id
  local image_id
  container_id="$(docker inspect --format '{{.Id}}' "${GCSA_CONTAINER}")"
  image_id="$(docker inspect --format '{{.Image}}' "${GCSA_CONTAINER}")"
  if [[ "${container_id}" != "${EXPECTED_GCSA_CONTAINER_ID}" ||
        "${image_id}" != "${EXPECTED_GCSA_IMAGE_ID}" ]]; then
    echo "gcsa validation container identity mismatch" >&2
    return 1
  fi
}

run_gcsa_kat_checks() {
  docker exec "${GCSA_CONTAINER}" sh -lc \
    "cd '${GCSA_CONTAINER_SNAPSHOT}' && \
     test \"\$(sha256sum src/gcsa/store/data/aead_v1_kat.json | awk '{print \$1}')\" = '${EXPECTED_KAT_SHA256}' && \
     PYTHONPATH=src:'${GCSA_CONTAINER_VALIDATOR_DIR}' PYTHONDONTWRITEBYTECODE=1 \
       python -c \"from validate_gcsa_roundtrip import gcsa_snapshot_root, require_accepted_gcsa_snapshot; from gcsa.store.schema import CONTRACT_RC_ID; actual = require_accepted_gcsa_snapshot(gcsa_snapshot_root()); assert actual == '${EXPECTED_GCSA_PACKAGE_SHA256}', actual; assert CONTRACT_RC_ID == '${EXPECTED_GCSA_CONTRACT_ID}', CONTRACT_RC_ID\" && \
     PYTHONPATH=src PYTHONDONTWRITEBYTECODE=1 python -m pytest \
       -p no:cacheprovider -q \
       tests/test_zarr_aead.py::TestAeadCandidateWire::test_wire_exact_kat_encrypts_and_decrypts \
       tests/test_zarr_aead.py::TestAeadCandidateWire::test_kat_plaintext_is_the_declared_inner_blosc_frame"
}

run_gcsa_kat() {
  require_gcsa_container_identity
  run_gcsa_kat_checks
}

run_joint() (
  if [[ ! -f "${JOINT_GOLDEN}" ]]; then
    echo "tracked joint evidence golden is absent: ${JOINT_GOLDEN}" >&2
    return 1
  fi
  local build_root="${PROTOTYPE_BUILD_ROOT}/release-approved"
  local windows_build_root
  windows_build_root="$(wslpath -w "${build_root}")"
  "${BUILD_HELPER}" raw -- cmake --build "${windows_build_root}" \
    --target p0s_store_generator --verbose

  mkdir -p "${JOINT_ARTIFACT_PARENT}"
  local run_root
  run_root="$(mktemp -d "${JOINT_ARTIFACT_PARENT}/joint.XXXXXX")"
  trap 'rm -rf -- "${run_root}"' EXIT

  local finalized_a="${run_root}/finalized-a"
  local finalized_b="${run_root}/finalized-b"
  local open_store="${run_root}/open"
  local generator="${build_root}/p0s_store_generator.exe"
  local windows_generator
  local windows_kat
  windows_generator="$(wslpath -w "${generator}")"
  windows_kat="$(wslpath -w "${ACCEPTED_KAT}")"

  "${BUILD_HELPER}" raw -- "${windows_generator}" "${windows_kat}" \
    "$(wslpath -w "${finalized_a}")" --sharding round-robin
  "${BUILD_HELPER}" raw -- "${windows_generator}" "${windows_kat}" \
    "$(wslpath -w "${finalized_b}")" --sharding round-robin
  "${BUILD_HELPER}" raw -- "${windows_generator}" "${windows_kat}" \
    "$(wslpath -w "${open_store}")" --open --sharding round-robin

  local container_run_root
  local repository_relative_run_root
  repository_relative_run_root="${run_root#"${REPOSITORY_ROOT}/"}"
  container_run_root="${GCSA_CONTAINER_REPOSITORY_ROOT}/${repository_relative_run_root}"
  docker exec \
    -e "PYTHONPATH=${GCSA_CONTAINER_SNAPSHOT}/src:${GCSA_CONTAINER_VALIDATOR_DIR}" \
    -e PYTHONDONTWRITEBYTECODE=1 \
    "${GCSA_CONTAINER}" python \
    "${GCSA_CONTAINER_VALIDATOR_DIR}/validate_gcsa_roundtrip.py" \
    --open-store "${container_run_root}/open" \
    --finalized-store-a "${container_run_root}/finalized-a" \
    --finalized-store-b "${container_run_root}/finalized-b" \
    --expected-evidence \
      "${GCSA_CONTAINER_REPOSITORY_ROOT}/docs/reference/zarr-store-contract/phase0-roundtrip/joint-roundtrip-golden.json"
)

main() {
  if [[ $# -ne 1 ]]; then
    usage
    return 2
  fi
  require_identity
  case "$1" in
    batch1)
      run_python
      run_gcsa_kat
      run_cpp
      ;;
    cpp)
      run_cpp
      ;;
    python)
      run_python
      ;;
    gcsa-kat)
      run_gcsa_kat
      ;;
    joint)
      run_python
      require_gcsa_container_identity
      run_gcsa_kat_checks
      run_joint
      ;;
    *)
      usage
      return 2
      ;;
  esac
}

main "$@"
