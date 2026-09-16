#!/usr/bin/env bash
#
# Downloads and unpacks the build prerequisites into external/, for a macOS
# build. The counterpart of scripts/get_sdk.ps1.
#
#     external/foobar2000_sdk/    foobar2000 SDK
#
# One dependency rather than two: WTL is a set of Win32 window classes and the
# macOS build has no use for it, so only the SDK is fetched here.
#
# The release, the URL and the checksum are not repeated in this file. They are
# read out of scripts/get_sdk.ps1, which stays their one home - the same rule
# the version already follows, and for the same reason. Two pins that have to
# be updated together are two pins that will not be: foo_rubato/CMakeLists.txt
# already reads that file to tell the about box which SDK it was built against,
# so a Mac build fetching a different one would make the about box wrong on one
# platform and right on the other, with nothing to say which.
#
# bsdtar - /usr/bin/tar on every supported macOS - reads the SDK's .7z, so
# there is no 7-Zip install in this. curl and shasum are likewise stock.
#
# Usage:
#     scripts/get_sdk.sh [-d <destination>] [-f]
#
#     -d  where to unpack. Default: external/ next to this repository.
#     -f  re-download and re-unpack even if everything is already there.

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
destination="$root/external"
force=0

while getopts ':d:fh' opt; do
    case "$opt" in
        d) destination="$OPTARG" ;;
        f) force=1 ;;
        h) sed -n '2,25p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "Usage: $0 [-d <destination>] [-f]" >&2; exit 2 ;;
    esac
done

pinned="$root/scripts/get_sdk.ps1"
if [[ ! -f "$pinned" ]]; then
    echo "error: $pinned is missing; it is where the SDK release is pinned." >&2
    exit 1
fi

# The SDK's block in get_sdk.ps1, read field by field. WTL's block follows the
# same shape, so the block is picked by its Dir rather than by position: awk
# collects each [pscustomobject] and prints the fields of the one that unpacks
# into foobar2000_sdk.
sdk_block="$(awk '
    /\[pscustomobject\]/ { block = ""; next }
    /^[[:space:]]*}/       { if (block ~ /Dir[[:space:]]*=[[:space:]]*.foobar2000_sdk./) print block; block = ""; next }
                           { block = block $0 "\n" }
' "$pinned")"

read_field() {
    printf '%s\n' "$sdk_block" \
        | sed -n "s/^[[:space:]]*$1[[:space:]]*=[[:space:]]*'\\(.*\\)'.*$/\\1/p" \
        | head -1
}

version="$(read_field Version)"
archive_name="$(read_field Archive)"
url="$(read_field Url)"
sha256="$(read_field Sha256 | tr '[:upper:]' '[:lower:]')"

if [[ -z "$version" || -z "$archive_name" || -z "$url" || -z "$sha256" ]]; then
    echo "error: could not read the SDK pin out of $pinned." >&2
    echo "       Expected Version, Archive, Url and Sha256 in its foobar2000_sdk block." >&2
    exit 1
fi

dir="$destination/foobar2000_sdk"
archive="$destination/$archive_name"
stamp="$dir/.foobar2000_sdk-$version.stamp"

if [[ -f "$stamp" && $force -eq 0 ]]; then
    echo "foobar2000 SDK $version already unpacked in $dir"
    exit 0
fi

mkdir -p "$destination"

# --- fetch -----------------------------------------------------------------
have_archive=0
if [[ -f "$archive" ]]; then
    if [[ "$(shasum -a 256 "$archive" | cut -d' ' -f1)" == "$sha256" ]]; then
        have_archive=1
        echo "Reusing $archive"
    else
        echo "Discarding $archive (checksum mismatch)"
        rm -f "$archive"
    fi
fi

if [[ $have_archive -eq 0 ]]; then
    echo "Downloading $url"
    if ! curl -fsSL --retry 2 -o "$archive" "$url"; then
        rm -f "$archive"
        echo "error: failed to download the foobar2000 SDK." >&2
        echo "       Fetch $url by hand, drop it in $destination, and re-run." >&2
        exit 1
    fi

    got="$(shasum -a 256 "$archive" | cut -d' ' -f1)"
    if [[ "$got" != "$sha256" ]]; then
        rm -f "$archive"
        echo "error: foobar2000 SDK: checksum mismatch." >&2
        echo "         expected $sha256" >&2
        echo "         got      $got" >&2
        echo "       The download was corrupted, or upstream changed the file." >&2
        exit 1
    fi
fi

# --- unpack ----------------------------------------------------------------
# Into a scratch directory first, so a half-finished unpack can never be
# mistaken for a usable dependency.
tmp="$destination/.unpack-foobar2000_sdk"
rm -rf "$tmp"
mkdir -p "$tmp"
trap 'rm -rf "$tmp"' EXIT

echo "Unpacking into $dir"
if ! tar -xf "$archive" -C "$tmp"; then
    echo "error: could not unpack $archive." >&2
    echo "       /usr/bin/tar reads .7z on macOS 11 and later; on an older one, unpack it by hand." >&2
    exit 1
fi

for need in foobar2000/SDK/foobar2000.h \
            foobar2000/helpers-mac/fb2k-platform.h \
            foobar2000/shared/shared-nix.cpp \
            pfc/pfc.h; do
    if [[ ! -e "$tmp/$need" ]]; then
        echo "error: unexpected archive layout in $archive_name: $need is missing." >&2
        exit 1
    fi
done

rm -rf "$dir"
mv "$tmp" "$dir"
trap - EXIT
printf '%s\n%s\n' "$version" "$url" > "$stamp"

echo "foobar2000 SDK $version ready in $dir"
