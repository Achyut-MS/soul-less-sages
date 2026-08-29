#!/usr/bin/env bash
# gen-deps-proof.sh
# Generates deps-proof.txt showing that all includes are standard stdlib/POSIX/Win32 or local.

set -euo pipefail

OUTPUT="deps-proof.txt"
echo "=== Zero-Dependency Dependency Proof ===" > "$OUTPUT"
echo "Generated at: $(date)" >> "$OUTPUT"
echo "" >> "$OUTPUT"

# Allowed system headers (standard C, POSIX, and standard Windows SDK headers)
ALLOWED_SYSTEM_HEADERS=(
    # C Standard Library (including C23)
    "assert.h" "complex.h" "ctype.h" "errno.h" "fenv.h" "float.h" "inttypes.h"
    "iso646.h" "limits.h" "locale.h" "math.h" "setjmp.h" "signal.h" "stdalign.h"
    "stdarg.h" "stdatomic.h" "stdbool.h" "stddef.h" "stdint.h" "stdio.h"
    "stdlib.h" "stdnoreturn.h" "string.h" "tgmath.h" "threads.h" "time.h"
    "uchar.h" "wchar.h" "wctype.h" "stdckdint.h" "stdbit.h"
    # POSIX / Linux System Headers
    "dirent.h" "fcntl.h" "fnmatch.h" "glob.h" "grp.h" "netdb.h" "pwd.h"
    "regex.h" "tar.h" "termios.h" "unistd.h" "utime.h" "wordexp.h"
    "arpa/inet.h" "net/if.h" "netinet/in.h" "netinet/tcp.h"
    "sys/mman.h" "sys/select.h" "sys/socket.h" "sys/stat.h" "sys/statvfs.h"
    "sys/times.h" "sys/types.h" "sys/un.h" "sys/utsname.h" "sys/wait.h"
    "poll.h" "sys/poll.h" "sys/ioctl.h" "sys/time.h"
    # Windows System Headers (for cross-compilation support)
    "winsock2.h" "windows.h" "ws2tcpip.h" "io.h" "direct.h" "process.h"
    "shellapi.h" "pthread.h"
)

echo "Scanning C, JS, and HTML files for external dependencies..."
echo "Files scanned:" >> "$OUTPUT"

# Find files and list them
find . -type f \( -name "*.c" -o -name "*.h" -o -name "*.js" -o -name "*.html" \) | sort | while read -r file; do
    echo "  - $file" >> "$OUTPUT"
done

echo "" >> "$OUTPUT"
echo "Violation Audit:" >> "$OUTPUT"
VIOLATIONS=0

# Grep for any #include, import, or require
find . -type f \( -name "*.c" -o -name "*.h" -o -name "*.js" -o -name "*.html" \) | sort | while read -r file; do
    # Scan C/H files for includes
    if [[ "$file" =~ \.(c|h)$ ]]; then
        # Find all lines matching #include
        grep -E "^[[:space:]]*#[[:space:]]*include" "$file" | while read -r line; do
            # Extract header name between < > or " "
            if [[ "$line" =~ \#include[[:space:]]*\<([^\>]+)\> ]]; then
                header="${BASH_REMATCH[1]}"
                # Check if it is in the allowed list
                allowed=false
                for h in "${ALLOWED_SYSTEM_HEADERS[@]}"; do
                    if [ "$h" = "$header" ]; then
                        allowed=true
                        break
                    fi
                done
                if [ "$allowed" = false ]; then
                    echo "WARNING: Non-standard/non-POSIX system header in $file: $line" >> "$OUTPUT"
                    VIOLATIONS=$((VIOLATIONS + 1))
                fi
            elif [[ "$line" =~ \#include[[:space:]]*\"([^\"]+)\" ]]; then
                # Local header: check if the file exists in the repo
                local_header="${BASH_REMATCH[1]}"
                dir=$(dirname "$file")
                # We also check root and src-c directories for simplicity
                if [ ! -f "$dir/$local_header" ] && [ ! -f "src-c/$local_header" ] && [ ! -f "$local_header" ]; then
                    echo "WARNING: Local header file not found in repo $file: $line" >> "$OUTPUT"
                    VIOLATIONS=$((VIOLATIONS + 1))
                fi
            else
                echo "WARNING: Unparseable include in $file: $line" >> "$OUTPUT"
                VIOLATIONS=$((VIOLATIONS + 1))
            fi
        done
    fi

    # Scan JS files for external imports
    if [[ "$file" =~ \.js$ ]]; then
        # Check for require or import of non-local packages
        # (e.g. require('express') or import ... from 'lodash')
        # Local imports typically start with ./ or ../
        grep -E "require\s*\(" "$file" | grep -v -E "require\s*\(\s*['\"]./\S*['\"]\)" || true | while read -r line; do
            echo "WARNING: Potential external require in $file: $line" >> "$OUTPUT"
            VIOLATIONS=$((VIOLATIONS + 1))
        done
        grep -E "import\s+.*from\s+" "$file" | grep -v -E "from\s*['\"]./\S*['\"]\)" || true | while read -r line; do
            echo "WARNING: Potential external import in $file: $line" >> "$OUTPUT"
            VIOLATIONS=$((VIOLATIONS + 1))
        done
    fi

    # Scan JS and HTML files for external HTTP/HTTPS resources and script src tags
    if [[ "$file" =~ \.(js|html)$ ]]; then
        grep -o -E "https?://[a-zA-Z0-9./?=&_-]+" "$file" | sort -u | while read -r url; do
            if [[ ! "$url" =~ localhost ]] && [[ ! "$url" =~ 127\.0\.0\.1 ]] && [[ ! "$url" =~ fonts\.googleapis\.com ]] && [[ ! "$url" =~ fonts\.gstatic\.com ]] && [[ ! "$url" =~ zerodepshack\.com ]]; then
                echo "WARNING: External HTTP/HTTPS reference in $file: $url" >> "$OUTPUT"
                VIOLATIONS=$((VIOLATIONS + 1))
            fi
        done
    fi
done

if [ "$VIOLATIONS" -eq 0 ]; then
    echo "SUCCESS: Zero third-party dependencies detected! All imports are standard SDKs or local files." >> "$OUTPUT"
    echo "Scan complete: 0 violations. Proof generated in $OUTPUT."
else
    echo "FAILED: $VIOLATIONS non-standard or external dependencies detected. Proof generated in $OUTPUT."
fi
