#!/usr/bin/env bash
cd "$(dirname "$0")/../.." || exit 1
exec /usr/bin/python3 sysmon_widget.py "$@"
