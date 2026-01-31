#!/bin/bash
# TTCut-ng Debian package build script
set -e

PACKAGE_NAME="ttcut"
SOURCE_DIR="/usr/local/src/TTCut-ng"
BUILD_BASE_DIR="/usr/local/src"
DISTRO=$(lsb_release -cs)

cd "$SOURCE_DIR"

# Get version from source code
VERSION=$(grep -oP 'versionString = "TTCut - \K[0-9.]+' common/ttcut.cpp)

if [ -z "$VERSION" ]; then
    echo "ERROR: Could not determine version from common/ttcut.cpp"
    exit 1
fi

# Get git info
GIT_DATE=$(git log -1 --format=%cd --date=format:%Y%m%d)
GIT_HASH=$(git rev-parse --short HEAD)
GIT_BRANCH=$(git rev-parse --abbrev-ref HEAD)
GIT_COMMIT_COUNT=$(git rev-list --count HEAD)

echo "==> Building TTCut Debian package"
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

# Clean build artifacts
echo "==> Cleaning build artifacts..."
make clean 2>/dev/null || true
rm -f ttcut *.o moc/*.cpp ui_h/*.h res/*.cpp obj/*.o

# Remove old build directory
[ -d "$BUILD_DIR" ] && rm -rf "$BUILD_DIR"

# Copy source to build directory
echo "==> Copying source to build directory..."
rsync -a --exclude='.git' --exclude='*.o' --exclude='ttcut' \
         --exclude='moc/' --exclude='obj/' --exclude='ui_h/' --exclude='res/' \
         --exclude='Makefile' --exclude='*.pro.user' \
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
