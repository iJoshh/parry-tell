#!/bin/bash
# station-ssh.sh — wrappers for ssh/scp to claude@station via password auth.
#
# Reads the SSH password from /etc/ssh-credentials-station (root:600). The
# matching credentials file structure is `password=<value>` on its own line.
# The credentials file MUST stay root-only readable; on this VM the deploy
# scripts run as joshua.blattner and use sudo NOPASSWD to read it. That
# keeps the password out of the joshua.blattner-readable filesystem, out
# of bash history, and off the ps command line. (SSHPASS env var is the
# documented sshpass mechanism via the -e flag; /proc/PID/environ is only
# readable by the same uid + root, same trust boundary as the creds file.)
#
# Usage:
#   source tools/station-ssh.sh
#   station_ssh 'echo SSH_OK'
#   station_scp ./local.cpp 'claude@station:C:/Projects/elden-ring/probe/probe.cpp'
#
# Both helpers preserve the called command's exit code so the caller's
# `set -e` and conditional logic behave the same as bare ssh/scp.
#
# Host key pinning: this script uses StrictHostKeyChecking=yes. The station
# host key is expected to already be in ~/.ssh/known_hosts (it was pinned
# during the prior key-auth era — see the hashed entry resolving via
# `ssh-keygen -F station -f ~/.ssh/known_hosts`). If a future station OS
# reinstall rotates the host key, the operator must explicitly re-pin
# under a controlled trust path; this script will NOT silently accept a
# new host key, which would otherwise be a MITM password-capture vector.
#
# Endpoint enforcement: station_scp requires every remote-style argument
# (containing a colon before any slash) to start with `claude@station:`.
# Anything else aborts before sshpass runs. This prevents typos like
# `attacker@host:/path` from causing the station password to be offered
# to an arbitrary host.
#
# Option safelist: both station_ssh and station_scp reject any caller-
# supplied dash-prefixed argument. Connection-routing options like -J,
# -o ProxyCommand=..., -F, -S, -o HostName=..., -o UserKnownHostsFile=...
# can redirect the SSH session to an attacker-controlled endpoint while
# SSHPASS is in the environment, bypassing the claude@station endpoint
# check. The canonical -o flags this wrapper sets are hardcoded in the
# wrapper itself, so callers should never need to add more. If a future
# legitimate use requires a specific safe option, add it to a narrow
# allowlist here rather than relaxing the blanket rejection.
#
# Why password instead of key: per Josh's request 2026-05-11, the SSH key
# auth path was retired in favor of password auth so SMB and SSH share a
# single credential surface. Key auth required maintaining a separate
# private key on this VM; password lives only in a root-mode-600 file
# alongside the SMB credentials.

set -u

_STATION_CREDS=/etc/ssh-credentials-station
_STATION_EXPECTED_HOST="claude@station"

# station_pw — print the password to stdout. Requires sudo NOPASSWD for the
# minimal `cat /etc/ssh-credentials-station` operation (already configured
# for joshua.blattner on this VM).
station_pw() {
    sudo -n cat "$_STATION_CREDS" 2>/dev/null | grep '^password=' | cut -d= -f2-
}

# _station_check_creds — fail fast if credentials are unreadable.
_station_check_creds() {
    local pw
    pw=$(station_pw)
    if [ -z "$pw" ]; then
        echo "ERROR: cannot read SSH password from $_STATION_CREDS" >&2
        echo "  (file may be missing, unreadable, or malformed,"  >&2
        echo "   or sudo NOPASSWD not configured for this user)"   >&2
        return 1
    fi
    return 0
}

# _station_reject_dash_args — abort if any caller-supplied argument starts
# with a dash. Connection-routing options (-J, -o ProxyCommand, -F, -S,
# -o HostName, -o UserKnownHostsFile, etc.) can redirect the SSH session
# to an attacker-controlled endpoint while SSHPASS is in the environment,
# bypassing the claude@station endpoint check. The canonical flags this
# wrapper needs are set inside station_ssh / station_scp themselves;
# callers should never need to pass their own.
_station_reject_dash_args() {
    local arg
    for arg in "$@"; do
        case "$arg" in
            -*)
                echo "ERROR: station_ssh/station_scp reject caller-supplied dash-prefixed args: $arg" >&2
                echo "  (option-injection guard; see comment in tools/station-ssh.sh)"               >&2
                return 1
                ;;
        esac
    done
    return 0
}

# station_ssh — wrap ssh with password auth, key auth disabled. All args
# are forwarded to ssh as the REMOTE COMMAND only (no flags accepted from
# caller). Returns the ssh exit code.
station_ssh() {
    _station_check_creds || return 1
    _station_reject_dash_args "$@" || return 1
    SSHPASS=$(station_pw) sshpass -e ssh \
        -o PreferredAuthentications=password \
        -o PubkeyAuthentication=no \
        -o StrictHostKeyChecking=yes \
        -o ConnectTimeout=10 \
        -o ServerAliveInterval=30 \
        -o ServerAliveCountMax=3 \
        "$_STATION_EXPECTED_HOST" "$@"
}

# _station_validate_scp_args — abort if any arg looks like a remote path
# (contains ':' before the first '/') and does NOT start with
# `claude@station:`. Local paths (no colon, or with the colon after a
# slash) pass through unchanged. Combine with _station_reject_dash_args
# to cover flag-injection separately.
_station_validate_scp_args() {
    local arg
    for arg in "$@"; do
        case "$arg" in
            -*) continue ;;                       # flag handled separately
            *)
                # Strip everything from the first '/' onward; if what's
                # left contains ':', this is remote-style.
                local before_slash="${arg%%/*}"
                case "$before_slash" in
                    *:*)
                        # Remote-style. Must start with the expected host.
                        case "$arg" in
                            "${_STATION_EXPECTED_HOST}:"*) : ;;  # allowed
                            *)
                                echo "ERROR: scp argument has non-station remote host: $arg" >&2
                                echo "  only ${_STATION_EXPECTED_HOST}:<path> is allowed"     >&2
                                return 1
                                ;;
                        esac
                        ;;
                    *) : ;;                       # local path
                esac
                ;;
        esac
    done
    return 0
}

# station_scp — wrap scp with password auth, key auth disabled. All args
# are forwarded to scp as PATH OPERANDS only (no flags accepted from
# caller). Remote-style args MUST be claude@station:<path>.
station_scp() {
    _station_check_creds || return 1
    _station_reject_dash_args "$@" || return 1
    _station_validate_scp_args "$@" || return 1
    SSHPASS=$(station_pw) sshpass -e scp \
        -o PreferredAuthentications=password \
        -o PubkeyAuthentication=no \
        -o StrictHostKeyChecking=yes \
        -o ConnectTimeout=10 \
        "$@"
}

# station_scp_recursive — like station_scp but adds -r (recursive). Same
# guards as station_scp (no caller-supplied dash args, all remote-style
# operands must be claude@station:<path>). This lives in the wrapper —
# rather than callers inlining `scp -r` — so every recursive copy still
# routes through the validated chokepoint.
station_scp_recursive() {
    _station_check_creds || return 1
    _station_reject_dash_args "$@" || return 1
    _station_validate_scp_args "$@" || return 1
    SSHPASS=$(station_pw) sshpass -e scp \
        -o PreferredAuthentications=password \
        -o PubkeyAuthentication=no \
        -o StrictHostKeyChecking=yes \
        -o ConnectTimeout=10 \
        -r "$@"
}

# If sourced, exit here. If executed directly, treat args as: ssh <command>.
if [ "${BASH_SOURCE[0]}" = "$0" ]; then
    if [ "$#" -eq 0 ]; then
        echo "Usage:"
        echo "  source $0                # then use station_ssh / station_scp"
        echo "  $0 <command>             # one-shot: runs ssh claude@station <command>"
        exit 64
    fi
    station_ssh "$@"
fi
