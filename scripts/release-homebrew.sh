#!/usr/bin/env bash
set -euo pipefail

if [[ ${1:-} == "" ]]; then
  echo "Usage: $0 <tag>" >&2
  echo "Example: $0 0.1.2" >&2
  exit 1
fi

tag="$1"
version="${tag#v}"
url="https://github.com/nanoscopic/dapico-tools/archive/refs/tags/${tag}.tar.gz"

tmp_file="$(mktemp)"
trap 'rm -f "$tmp_file"' EXIT

if ! curl -fsSL "$url" -o "$tmp_file"; then
  echo "Failed to download release tarball: $url" >&2
  exit 1
fi

sha256="$(shasum -a 256 "$tmp_file" | awk '{print $1}')"

template_path="Formula/dapico-tools.rb.in"
output_path="Formula/dapico-tools.rb"

if [[ ! -f "$template_path" ]]; then
  echo "Missing template: $template_path" >&2
  exit 1
fi

sed -e "s|@URL@|$url|g" \
    -e "s|@SHA256@|$sha256|g" \
    -e "s|@VERSION@|$version|g" \
    "$template_path" > "$output_path"

echo "Updated $output_path for tag $tag (version $version)."
