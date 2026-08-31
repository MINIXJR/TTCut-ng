#!/bin/bash
# TTCut-ng Debian package build script
set -e

PACKAGE_NAME="ttcut-ng"
SOURCE_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_BASE_DIR="$(dirname "$SOURCE_DIR")"
DISTRO=$(lsb_release -cs)

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

echo "==> Building TTCut-ng Debian package"
echo "==> Version: ${VERSION}"
echo "==> Git date: ${GIT_DATE}"
echo "==> Git commit: ${GIT_HASH}"
echo "==> Git commit count: ${GIT_COMMIT_COUNT}"
echo "==> Git branch: $GIT_BRANCH"
echo "==> Distribution: $DISTRO"
echo ""

# Build version includes git info: VERSION+gitDATE-COUNT-HASH
BUILD_VERSION="${VERSION}+git${GIT_DATE}-${GIT_COMMIT_COUNT}-${GIT_HASH}"
BUILD_DIR="${BUILD_BASE_DIR}/${PACKAGE_NAME}-${BUILD_VERSION}"

# Package version always includes git info
PACKAGE_VERSION="${BUILD_VERSION}-1~${DISTRO}"

# Prompt for changelog description
read -p "Enter changelog description (or press Enter for 'Git snapshot'): " CHANGELOG_MSG

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
# Do NOT replace this with --filter=':- .gitignore'. That file also lists
# trans/*.qm, and nothing in the build regenerates it — the compiled
# translation would silently vanish from the package and the application would
# come up untranslated.
rsync -a --exclude='.git' --exclude='*.o' --exclude='ttcut-ng' \
         --exclude='/Makefile' --exclude='*.pro.user' \
         --exclude='/build/' --exclude='/build-deb/' --exclude='/build-asan/' \
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
dpkg-buildpackage -b -uc -us -j$(nproc)

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
