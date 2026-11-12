#!/bin/bash
#
# Copyright (c) 2026, Realtek Semiconductor Corporation
#
# SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
#
# Generate version.h from version_cfg.txt.
#
# This replaces the SDK-wide tool/Gadgets/version_gen_config.sh for this project
# only, so the version stays under the project's own control:
#
#   * version_cfg.txt is authoritative. It is committed and edited by the
#     integrator; git is only a fallback for the fields it leaves out.
#   * Every git call is pinned to this project directory, so the generated
#     version does not depend on the working directory the build system happens
#     to use (uVision runs from mdk/, west runs cmake from the SDK root -- the
#     latter used to yield 0.0.0.0 because the SDK repo has no dashboard tag).
#   * The file is rewritten only when a version field actually changed, so an
#     unchanged version keeps its build stamp and nothing recompiles for nothing.
#
# Usage: version_gen.sh [version.h path] [version_cfg.txt path]
#
# Both arguments are optional and default to the project root, so the script can
# be called from any directory.
#

set -u

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
PROJ_DIR=$(cd "$SCRIPT_DIR/.." && pwd)

# Guard against a bash that cannot resolve a Windows path (a WSL bash picked up
# from PATH, for instance): fail loudly instead of writing version.h somewhere
# unexpected.
if [ ! -d "$PROJ_DIR/mdk" ] || [ ! -d "$PROJ_DIR/gcc" ]; then
    echo "[VERSION] error: cannot resolve the project root from '$0'" >&2
    echo "[VERSION] hint: run this script with Git Bash, not a WSL bash" >&2
    exit 1
fi

VERSION_FILE=${1:-"$PROJ_DIR/version.h"}
CFG_FILE=${2:-"$PROJ_DIR/version_cfg.txt"}

# ---------------------------------------------------------------- config file
# Read "KEY = VALUE", ignore "#" comments, keep values free of surrounding space.
cfg_val() {
    [ -f "$CFG_FILE" ] || return 0
    awk -v k="$1" '
        {
            line = $0
            sub(/#.*/, "", line)
            i = index(line, "=")
            if (i == 0) next
            key = substr(line, 1, i - 1)
            val = substr(line, i + 1)
            gsub(/^[ \t\r]+|[ \t\r]+$/, "", key)
            gsub(/^[ \t\r]+|[ \t\r]+$/, "", val)
            if (key == k) { print val; exit }
        }
    ' "$CFG_FILE"
}

if [ ! -f "$CFG_FILE" ]; then
    echo "[VERSION] warning: $CFG_FILE not found, falling back to git"
fi

CFG_MAJOR=$(cfg_val VERSION_MAJOR)
CFG_MINOR=$(cfg_val VERSION_MINOR)
CFG_REVISION=$(cfg_val VERSION_REVISION)
CFG_BUILDNUM=$(cfg_val VERSION_BUILDNUM)
CFG_CUSTOMER=$(cfg_val CUSTOMER_NAME)

# ------------------------------------------------------------------- git info
HAVE_GIT=0
if command -v git > /dev/null 2>&1; then
    if git -C "$PROJ_DIR" rev-parse --git-dir > /dev/null 2>&1; then
        HAVE_GIT=1
    fi
fi

# MAJOR.MINOR.REVISION come from the tag when the config leaves them out. The
# tag format is "<prefix>-v<major>.<minor>.<revision>", e.g. dashboard-v3.14.2.
TAG_VERSION=""
if [ $HAVE_GIT -eq 1 ]; then
    FULL_VERSION=$(git -C "$PROJ_DIR" describe 2>/dev/null)
    TAG_VERSION=${FULL_VERSION##*-v}
fi

MAJOR=${CFG_MAJOR:-$(echo "$TAG_VERSION" | awk -F '.' '{print $1}')}
MINOR=${CFG_MINOR:-$(echo "$TAG_VERSION" | awk -F '.' '{print $2}')}
REVISION=${CFG_REVISION:-$(echo "$TAG_VERSION" | awk -F '.' '{print $3}' | sed 's/-.*//')}
MAJOR=${MAJOR:-0}
MINOR=${MINOR:-0}
REVISION=${REVISION:-0}

# BUILDNUM: explicit value, or "auto"/empty meaning "commits since the tag".
# A CI build number in the environment always wins over both.
BUILDNUM=${BUILD_NUMBER:-$CFG_BUILDNUM}
if [ -z "$BUILDNUM" ] || [ "$BUILDNUM" = "auto" ]; then
    BUILDNUM=0
    if [ $HAVE_GIT -eq 1 ]; then
        LAST_TAG=$(git -C "$PROJ_DIR" describe --abbrev=0 2>/dev/null)
        if [ -n "$LAST_TAG" ]; then
            BUILDNUM=$(git -C "$PROJ_DIR" rev-list --count "$LAST_TAG"..HEAD 2>/dev/null)
        fi
    fi
fi

# mdk/mp.ini documents the MP header version as four byte fields (0.0.0.0 to
# 255.255.255.255), while the SDK generator tolerated a build number up to 2048.
# Keep the SDK's hard limit so a project migrating from it never starts failing,
# but warn once the value leaves the documented byte range.
if [ "$BUILDNUM" -gt 2048 ] 2>/dev/null; then
    echo "[VERSION] error: VERSION_BUILDNUM=$BUILDNUM exceeds 2048" >&2
    exit 1
fi
if [ "$BUILDNUM" -gt 255 ] 2>/dev/null; then
    echo "[VERSION] warning: VERSION_BUILDNUM=$BUILDNUM exceeds the 0-255 range" \
         "documented for the MP header version field" >&2
fi

GCID=0x00000000
COMMIT=000000000000
BUILDER=unknown
if [ $HAVE_GIT -eq 1 ]; then
    GCID=0x$(git -C "$PROJ_DIR" log --pretty=oneline -1 | cut -c 1-8)
    COMMIT=$(git -C "$PROJ_DIR" log --pretty=oneline -1 | cut -c 1-12)
    BUILDER_MAIL=$(git -C "$PROJ_DIR" config user.email 2>/dev/null)
    if [ -n "${BUILDER_MAIL:-}" ]; then
        BUILDER=${BUILDER_MAIL%@*}
    fi
fi
BUILDER_STR=${BUILDER//./_}

CUSTOMER_NAME=${CFG_CUSTOMER:-sdk-v1}
# The name is also split into CN_1..CN_8 characters for the MP header, padded
# with '#' when shorter than eight characters.
CUSTOMER_NAME=$(echo "$CUSTOMER_NAME" | cut -c 1-8)
CN_1=$(echo "$CUSTOMER_NAME" | cut -c 1); CN_1=${CN_1:-#}
CN_2=$(echo "$CUSTOMER_NAME" | cut -c 2); CN_2=${CN_2:-#}
CN_3=$(echo "$CUSTOMER_NAME" | cut -c 3); CN_3=${CN_3:-#}
CN_4=$(echo "$CUSTOMER_NAME" | cut -c 4); CN_4=${CN_4:-#}
CN_5=$(echo "$CUSTOMER_NAME" | cut -c 5); CN_5=${CN_5:-#}
CN_6=$(echo "$CUSTOMER_NAME" | cut -c 6); CN_6=${CN_6:-#}
CN_7=$(echo "$CUSTOMER_NAME" | cut -c 7); CN_7=${CN_7:-#}
CN_8=$(echo "$CUSTOMER_NAME" | cut -c 8); CN_8=${CN_8:-#}

# ------------------------------------------------------------------- emit file
# Optional: keep mdk/mp.ini's MP header version in step with version.h. The
# upgrade tool compares that field, so letting it drift from the file name is
# how a device ends up reporting a different version than the image it runs.
# Off by default because it rewrites a tracked file on every version change.
sync_mp_ini() {
    [ "$(cfg_val SYNC_MP_INI)" = "1" ] || return 0
    mp_ini="$PROJ_DIR/mdk/mp.ini"
    [ -f "$mp_ini" ] || return 0
    want="Version=$MAJOR.$MINOR.$REVISION.$BUILDNUM"
    grep -qxF "$want" "$mp_ini" && return 0
    sed -i "s/^Version=.*/$want/" "$mp_ini" && echo "[VERSION] mp.ini: $want"
}

sync_mp_ini

emit() {
    cat <<EOF
/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#define VERSION_MAJOR            $MAJOR
#define VERSION_MINOR            $MINOR
#define VERSION_REVISION         $REVISION
#define VERSION_BUILDNUM         $BUILDNUM
#define VERSION_GCID             $GCID
#define CUSTOMER_NAME            $CUSTOMER_NAME
#define CN_1                     '$CN_1'
#define CN_2                     '$CN_2'
#define CN_3                     '$CN_3'
#define CN_4                     '$CN_4'
#define CN_5                     '$CN_5'
#define CN_6                     '$CN_6'
#define CN_7                     '$CN_7'
#define CN_8                     '$CN_8'
#define BUILDING_TIME            $1
#define NAME2STR(a)              #a
#define CUSTOMER_NAME_S          NAME2STR(CUSTOMER_NAME)
#define NUM4STR(a,b,c,d)         #a "." #b "." #c "." #d
#define VERSIONBUILDSTR(a,b,c,d) NUM4STR(a,b,c,d)
#define VERSION_BUILD_STR        VERSIONBUILDSTR(VERSION_MAJOR,VERSION_MINOR,VERSION_REVISION,VERSION_BUILDNUM)
#define COMMIT                   $COMMIT
#define BUILDING_TIME_STR        $2
#define BUILDER                  $BUILDER
#define BUILDER_STR              $BUILDER_STR
#define TO_STR(R) NAME2STR(R)
#define GENERATE_VERSION_MSG(MSG, VERSION, COMMIT, BUILDING_TIME, BUILDER) \\
    GENERATE_VERSION_MSG_(MSG, VERSION, COMMIT, BUILDING_TIME, BUILDER)
#define GENERATE_VERSION_MSG_(MSG, VERSION, COMMIT, BUILDING_TIME, BUILDER) \\
    MSG##_##VERSION##_##COMMIT##_##BUILDING_TIME##_##BUILDER

#define GENERATE_BIN_VERSION(MSG, VERSION) \\
    typedef char GENERATE_VERSION_MSG(MSG, VERSION, COMMIT, BUILDING_TIME_STR, BUILDER_STR);
EOF
}

# Keep the previous build stamp when nothing version-related changed, so an
# unchanged version does not look like a fresh build.
OLD_STAMP=""
OLD_STR=""
if [ -f "$VERSION_FILE" ]; then
    OLD_STAMP=$(awk '/^#define[[:space:]]+BUILDING_TIME[[:space:]]+/ {sub(/^#define[[:space:]]+BUILDING_TIME[[:space:]]+/, ""); print; exit}' "$VERSION_FILE")
    OLD_STR=$(awk '/^#define[[:space:]]+BUILDING_TIME_STR[[:space:]]+/ {sub(/^#define[[:space:]]+BUILDING_TIME_STR[[:space:]]+/, ""); print; exit}' "$VERSION_FILE")
fi

if [ -n "$OLD_STAMP" ] && [ -n "$OLD_STR" ]; then
    if [ -f "$VERSION_FILE" ] && [ "$(emit "$OLD_STAMP" "$OLD_STR")" = "$(cat "$VERSION_FILE")" ]; then
        echo "[VERSION] unchanged: $MAJOR.$MINOR.$REVISION.$BUILDNUM-$GCID ($CUSTOMER_NAME)"
        exit 0
    fi
fi

BUILDING_TIME=$(LC_ALL=C date '+%a %b %e %H:%M:%S %Y')
BUILDING_TIME_STR=$(LC_ALL=C date '+%a_%Y_%m_%d_%H_%M_%S')

emit "$BUILDING_TIME" "$BUILDING_TIME_STR" > "$VERSION_FILE.tmp" && mv -f "$VERSION_FILE.tmp" "$VERSION_FILE" || {
    echo "[VERSION] error: cannot write $VERSION_FILE" >&2
    rm -f "$VERSION_FILE.tmp"
    exit 1
}

echo "[VERSION] generated $MAJOR.$MINOR.$REVISION.$BUILDNUM-$GCID ($CUSTOMER_NAME) -> $VERSION_FILE"
if [ $HAVE_GIT -eq 0 ]; then
    echo "[VERSION] note: not a git checkout, GCID/COMMIT are placeholders"
fi
