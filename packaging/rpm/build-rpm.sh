#!/usr/bin/env bash
set -euo pipefail

NAME="AudioKontroller"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
RPM_DIR="$ROOT_DIR/packaging/rpm"
TOPDIR="$RPM_DIR/rpmbuild"
VERSION="${1:-$(git -C "$ROOT_DIR" log -1 --date=format:%Y.%m.%d --format=%ad 2>/dev/null || date +%Y.%m.%d)}"
RELEASE="${2:-1.git$(git -C "$ROOT_DIR" rev-parse --short HEAD 2>/dev/null || echo manual)}"
TMPDIR="$(mktemp -d)"
STAGE_DIR="$TMPDIR/$NAME-$VERSION"
SOURCE_TARBALL="$TOPDIR/SOURCES/$NAME-$VERSION.tar.gz"

cleanup() {
    rm -rf "$TMPDIR"
}
trap cleanup EXIT

rm -rf "$TOPDIR"
install -d "$TOPDIR/BUILD" "$TOPDIR/BUILDROOT" "$TOPDIR/RPMS" "$TOPDIR/SOURCES" "$TOPDIR/SPECS" "$TOPDIR/SRPMS"
install -d "$STAGE_DIR"

tar \
    --exclude-vcs \
    --exclude='./build' \
    --exclude='./packaging/rpm/rpmbuild' \
    --exclude='./*.rpm' \
    --exclude='./*.src.rpm' \
    -cf - \
    -C "$ROOT_DIR" . | tar -xf - -C "$STAGE_DIR"

tar -czf "$SOURCE_TARBALL" -C "$TMPDIR" "$NAME-$VERSION"
install -m 0644 "$RPM_DIR/AudioKontroller.spec" "$TOPDIR/SPECS/AudioKontroller.spec"

rpmbuild -ba "$TOPDIR/SPECS/AudioKontroller.spec" \
    --define "_topdir $TOPDIR" \
    --define "version $VERSION" \
    --define "release $RELEASE"

printf 'RPM artifacts:\n'
find "$TOPDIR/RPMS" "$TOPDIR/SRPMS" -type f -name '*.rpm' -print | sort
