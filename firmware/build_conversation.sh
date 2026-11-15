#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")"
if ! command -v idf.py >/dev/null 2>&1; then
    echo 'Activate ESP-IDF 5.5.4 with its export.sh first.' >&2
    exit 1
fi
if [[ $# -eq 0 ]]; then set -- build; fi
idf.py -B build-conversation \
    -DSDKCONFIG="$PWD/sdkconfig.conversation" \
    '-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.ito.defaults;sdkconfig.conversation.defaults' \
    -DSTACKCHAN_LOCAL_ITO=ON -DSTACKCHAN_CONVERSATION=ON "$@"
