#!/bin/bash
set -e

# Project details
NAME="katalyst-terminal"
VERSION="0.1.0"
TARNAME="${NAME}-${VERSION}"
RPMBUILD_DIR="${PWD}/build/rpmbuild"

echo "==> Preparing source tarball..."
mkdir -p build/tarball-stage/${TARNAME}

# Sync files (excluding build artifacts and cached module dirs)
rsync -a \
    --exclude=build \
    --exclude=gcm.cache \
    --exclude=.git \
    --exclude=katalyst-terminal.spec \
    --exclude=build-rpm.sh \
    ./ build/tarball-stage/${TARNAME}/

# Create tarball
cd build/tarball-stage
tar -czf ${TARNAME}.tar.gz ${TARNAME}
cd ../..

echo "==> Setting up rpmbuild directory structure..."
mkdir -p ${RPMBUILD_DIR}/{BUILD,BUILDROOT,RPMS,SOURCES,SPECS,SRPMS}
mv build/tarball-stage/${TARNAME}.tar.gz ${RPMBUILD_DIR}/SOURCES/

# Copy the spec file
cp katalyst-terminal.spec ${RPMBUILD_DIR}/SPECS/

echo "==> Building RPM..."
HOME=${RPMBUILD_DIR} rpmbuild \
    --define "_topdir ${RPMBUILD_DIR}" \
    -ba ${RPMBUILD_DIR}/SPECS/katalyst-terminal.spec

echo "==> Done! RPMs generated in ${RPMBUILD_DIR}/RPMS/"
find ${RPMBUILD_DIR}/RPMS -name "*.rpm"
