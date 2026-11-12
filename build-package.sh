#!/bin/bash
# TTCut-ng Debian package build script
# Usage: build-package.sh [--publish | --publish-only]
set -e

# --- APT publish block (same in every build-package.sh; keep in sync) -------
# After a successful build, optionally hand the new .debs to a reprepro APT
# repository on another host: scp into an upload directory, then run the
# repository's publish script there via ssh. The target is read from .env
# next to this script (gitignored; may be a symlink to a central file):
#   APT_PUBLISH_HOST=user@host
#   APT_PUBLISH_SCRIPT=/path/to/publish.sh      (on that host)
#   APT_PUBLISH_INCOMING=/path/to/upload/dir    (on that host)
# Default is NOT to publish, so a test build never reaches other machines:
# --publish publishes without asking; otherwise an interactive run asks after
# the build, and a run without a terminal never publishes. --publish-only
# skips the build and publishes the .debs an earlier run built from the same
# commit (release flow: build first, publish after the push is approved).
APT_PUBLISH_SUITE=sid
APT_PUBLISH=ask
for arg in "$@"; do
    case "$arg" in
        --publish) [ "$APT_PUBLISH" = only ] || APT_PUBLISH=yes ;;
        --publish-only) APT_PUBLISH=only ;;
        *) echo "Usage: $(basename "$0") [--publish | --publish-only]" >&2; exit 2 ;;
    esac
done

# Read the APT_PUBLISH_* keys from <dir>/.env. Parsed line by line instead of
# sourced, so the file is data and never executed.
apt_publish_load_env() {
    local env_file="$1/.env" key value
    if [ ! -r "$env_file" ]; then
        echo "==> ERROR: $env_file not readable; it must set APT_PUBLISH_HOST, APT_PUBLISH_SCRIPT and APT_PUBLISH_INCOMING" >&2
        return 1
    fi
    while IFS='=' read -r key value || [ -n "$key" ]; do
        case "$key" in
            APT_PUBLISH_HOST|APT_PUBLISH_SCRIPT|APT_PUBLISH_INCOMING) ;;
            *) continue ;;
        esac
        value="${value%\"}"; value="${value#\"}"
        printf -v "$key" '%s' "$value"
    done < "$env_file"
    for key in APT_PUBLISH_HOST APT_PUBLISH_SCRIPT APT_PUBLISH_INCOMING; do
        if [ -z "${!key}" ]; then
            echo "==> ERROR: $key missing in $env_file" >&2
            return 1
        fi
    done
}

# BatchMode: fail at once instead of prompting for a password, so a missing
# key is found before the build and not after it.
apt_publish_check_ssh() {
    if ! ssh -o BatchMode=yes -o ConnectTimeout=10 "$APT_PUBLISH_HOST" true; then
        echo "==> ERROR: passwordless ssh to $APT_PUBLISH_HOST failed" >&2
        return 1
    fi
}

# Upload and publish the given .debs. Returns 1 on failure; the caller keeps
# the build result, the .debs stay where they are.
apt_publish_debs() {
    local deb out rc remote=()
    for deb in "$@"; do
        remote+=("$APT_PUBLISH_INCOMING/$(basename "$deb")")
    done
    echo "==> Uploading to $APT_PUBLISH_HOST:$APT_PUBLISH_INCOMING/"
    # ssh hands the command to the remote shell as one string: quote it.
    if ! ssh -o BatchMode=yes "$APT_PUBLISH_HOST" "mkdir -p $(printf '%q' "$APT_PUBLISH_INCOMING")" ||
       ! scp -o BatchMode=yes "$@" "$APT_PUBLISH_HOST:$APT_PUBLISH_INCOMING/"; then
        echo "==> ERROR: upload failed. The build succeeded; the packages stay local." >&2
        return 1
    fi
    echo "==> Publishing to suite $APT_PUBLISH_SUITE"
    rc=0
    out=$(ssh -o BatchMode=yes "$APT_PUBLISH_HOST" \
          "$(printf '%q ' "$APT_PUBLISH_SCRIPT" -d "$APT_PUBLISH_SUITE" "${remote[@]}")" 2>&1) || rc=$?
    printf '%s\n' "$out"
    if [ "$rc" -ne 0 ]; then
        echo "==> ERROR: publishing failed (exit $rc). The build succeeded; the packages stay local." >&2
        return 1
    fi
    # reprepro refuses a version it already has, but still exits 0.
    if grep -q 'Skipping inclusion' <<<"$out"; then
        echo "==> NOTE: the repository already has this version, nothing new was published. Was the version bumped?"
        return 0
    fi
    echo "==> Published."
}

# Called after a successful build: <dir with .env> <deb>...
apt_publish_after_build() {
    local dir="$1" answer
    shift
    if [ $# -eq 0 ]; then
        echo "==> ERROR: no .deb files of this build found to publish" >&2
        return 1
    fi
    if [ "$APT_PUBLISH" = ask ]; then
        if [ ! -t 0 ]; then
            echo "==> Not published."
            return 0
        fi
        read -r -p "Publish to the APT repository (suite $APT_PUBLISH_SUITE)? [y/N] " answer || answer=""
        case "$answer" in
            y|Y|j|J) ;;
            *) echo "==> Not published."; return 0 ;;
        esac
        apt_publish_load_env "$dir" && apt_publish_check_ssh || return 1
    fi
    apt_publish_debs "$@"
}
# --- end of APT publish block -----------------------------------------------

PACKAGE_NAME="ttcut-ng"
SOURCE_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_BASE_DIR="$(dirname "$SOURCE_DIR")"
DISTRO=$(lsb_release -cs)

# Check the publish target now, not after a long build
if [ "$APT_PUBLISH" != ask ]; then
    apt_publish_load_env "$SOURCE_DIR" && apt_publish_check_ssh || exit 1
fi

cd "$SOURCE_DIR"

# dch modifies debian/changelog in-place. Restore the working copy on exit so
# a second run starts from a clean slate (otherwise dch sees the previous
# version entry and complains, or worse, accumulates stale entries).
# -C is required: the script cd's into $BUILD_DIR (an rsync copy without
# .git), where a plain "git checkout" finds no repository and does nothing.
trap 'git -C "$SOURCE_DIR" checkout -- debian/changelog 2>/dev/null || true' EXIT

# Get version from CMakeLists.txt (single source of truth)
VERSION=$(grep -oP '^project\(ttcut-ng VERSION \K[0-9.]+' CMakeLists.txt)

if [ -z "$VERSION" ]; then
    echo "ERROR: Could not determine version from CMakeLists.txt"
    exit 1
fi

# Get git info
GIT_DATE=$(git log -1 --format=%cd --date=format:%Y%m%d)
GIT_HASH=$(git rev-parse --short HEAD)
GIT_BRANCH=$(git rev-parse --abbrev-ref HEAD)
GIT_COMMIT_COUNT=$(git rev-list --count HEAD)

if [ "$APT_PUBLISH" = only ]; then
    echo "==> Publishing existing TTCut-ng Debian packages (no build)"
else
    echo "==> Building TTCut-ng Debian package"
fi
echo "==> Version: ${VERSION}"
echo "==> Git date: ${GIT_DATE}"
echo "==> Git commit: ${GIT_HASH}"
echo "==> Git commit count: ${GIT_COMMIT_COUNT}"
echo "==> Git branch: $GIT_BRANCH"
echo "==> Distribution: $DISTRO"
echo ""

# Build version includes git info: VERSION+gitDATE.COUNT.HASH
# Dots, not hyphens: debian/source/format is "3.0 (native)", and a native
# version must not contain a hyphen or a Debian revision. The old scheme
# (…-COUNT-HASH-1~distro) only went unnoticed because -b never invokes
# dpkg-source. Dot-separated versions sort above the old hyphenated ones
# (dpkg orders '-' before '.'), so upgrades from old packages work.
BUILD_VERSION="${VERSION}+git${GIT_DATE}.${GIT_COMMIT_COUNT}.${GIT_HASH}"
BUILD_DIR="${BUILD_BASE_DIR}/${PACKAGE_NAME}-${BUILD_VERSION}"

# Distro marker moves into the upstream version (no revision on native packages)
PACKAGE_VERSION="${BUILD_VERSION}~${DISTRO}"

# Exactly this version's .debs (main package and -dbgsym) -> PUBLISH_DEBS
find_version_debs() {
    shopt -s nullglob
    PUBLISH_DEBS=("${BUILD_BASE_DIR}/"*_"${PACKAGE_VERSION}"_*.deb)
    shopt -u nullglob
}

if [ "$APT_PUBLISH" = only ]; then
    find_version_debs
    if [ ${#PUBLISH_DEBS[@]} -eq 0 ]; then
        echo "==> ERROR: no .deb files for version $PACKAGE_VERSION in $BUILD_BASE_DIR; build this commit first" >&2
        exit 1
    fi
    apt_publish_debs "${PUBLISH_DEBS[@]}" || exit 3
    exit 0
fi

# Prompt for changelog description (|| catches EOF on non-interactive stdin,
# which would otherwise abort the script via set -e)
read -r -p "Enter changelog description (or press Enter for 'Git snapshot'): " CHANGELOG_MSG || CHANGELOG_MSG=""

# Default message if empty
if [ -z "$CHANGELOG_MSG" ]; then
    CHANGELOG_MSG="Git snapshot ${GIT_HASH}"
fi

echo "==> Creating changelog entry for version: $PACKAGE_VERSION"

# Update changelog with git version
DEBFULLNAME="MINIXJR" DEBEMAIL="35893755+MINIXJR@users.noreply.github.com" \
    dch --newversion "$PACKAGE_VERSION" --distribution "$DISTRO" \
    "$CHANGELOG_MSG"

# Remove old build directory
[ -d "$BUILD_DIR" ] && rm -rf "$BUILD_DIR"

# Copy source to build directory
echo "==> Copying source to build directory..."
# rsync copies the working tree, not the git index, so anything gitignored is
# invisible in `git status` yet still gets copied. Two things used to dominate
# the copy: the ~80 compiled harnesses in tools/diag (~60 MB each — every one
# statically links ttcut-core, with debug symbols) and build-asan, a leftover
# sanitizer build. Together roughly 1.9 GB of a 2.4 GB build directory, copied
# on every release, never used by the package, and left behind in every build
# directory kept for rollback.
#
# The harness BINARIES go, their sources stay: CMakeLists.txt does
# add_subdirectory(tools/diag), so dropping the directory outright fails
# configuration. The --include rules must precede the --exclude ones, since
# rsync applies the first matching rule.
#
# Editor and assistant state (.qtc_clangd, .cache anywhere, .superpowers,
# .claude) and core dumps in the root go too: measured 2026-10-02, they were
# 61 MB of a 77 MB copy, 1400 files, none of them tracked.
#
# --filter=':- .gitignore' was considered and rejected in favor of these
# explicit rules, which are byte-verified against a reference package. (An
# earlier version of this comment claimed the filter would silently drop the
# compiled translation — wrong: debian/rules regenerates trans/*.qm via
# lrelease. Any change here still warrants a package-content comparison.)
rsync -a --exclude='.git' --exclude='*.o' --exclude='ttcut-ng' \
         --exclude='/Makefile' --exclude='*.pro.user' \
         --exclude='/build/' --exclude='/build-deb/' --exclude='/build-asan/' \
         --exclude='/.qtc_clangd/' --exclude='.cache/' \
         --exclude='/.superpowers/' --exclude='/.claude/' --exclude='/core.*' \
         --exclude='/.env' \
         --include='/tools/diag/*.cpp' --include='/tools/diag/*.sh' \
         --include='/tools/diag/*.py' --include='/tools/diag/*.txt' \
         --exclude='/tools/diag/test_*' \
         --exclude='/tools/diag/dump_mpeg2_fields' \
         --exclude='/tools/diag/probe_copystart' \
         --exclude='/tools/diag/bench_playback_mux' \
         --exclude='/tools/test-videos/cache/' \
         --exclude='/docs/' \
         "$SOURCE_DIR/" "$BUILD_DIR/"

cd "$BUILD_DIR"

echo "==> Package version: $PACKAGE_VERSION"
echo "==> Building package..."

# Build package
dpkg-buildpackage -b -uc -us -j"$(nproc)"

# Check for created package
DEB_FILE="${BUILD_BASE_DIR}/${PACKAGE_NAME}_${PACKAGE_VERSION}_$(dpkg --print-architecture).deb"

if [ -f "$DEB_FILE" ]; then
    echo ""
    echo "==> SUCCESS! Package created:"
    echo "    $DEB_FILE"
    ls -lh "$DEB_FILE"
    echo ""
    echo "==> Package contents:"
    dpkg-deb -c "$DEB_FILE" | head -20
    echo ""
    echo "==> Package info:"
    dpkg-deb -I "$DEB_FILE"
else
    echo "==> ERROR: Package not found at $DEB_FILE"
    exit 1
fi

echo ""
find_version_debs
apt_publish_after_build "$SOURCE_DIR" "${PUBLISH_DEBS[@]}" || exit 3
