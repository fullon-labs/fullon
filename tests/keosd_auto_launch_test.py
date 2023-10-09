#!/usr/bin/env python3

# This script tests that gaxcli launches gaxkey automatically when gaxkey is not
# running yet.

import subprocess


def run_cleos_wallet_command(command: str, no_auto_keosd: bool):
    """Run the given gaxcli command and return subprocess.CompletedProcess."""
    args = ['./programs/gaxcli/gaxcli']

    if no_auto_keosd:
        args.append('--no-auto-gaxkey')

    args += 'wallet', command

    return subprocess.run(args,
                          check=False,
                          stdout=subprocess.DEVNULL,
                          stderr=subprocess.PIPE)


def stop_keosd():
    """Stop the default gaxkey instance."""
    run_cleos_wallet_command('stop', no_auto_keosd=True)


def check_cleos_stderr(stderr: bytes, expected_match: bytes):
    if expected_match not in stderr:
        raise RuntimeError("'{}' not found in {}'".format(
            expected_match.decode(), stderr.decode()))


def keosd_auto_launch_test():
    """Test that keos auto-launching works but can be optionally inhibited."""
    stop_keosd()

    # Make sure that when '--no-auto-gaxkey' is given, gaxkey is not started by
    # gaxcli.
    completed_process = run_cleos_wallet_command('list', no_auto_keosd=True)
    assert completed_process.returncode != 0
    check_cleos_stderr(completed_process.stderr, b'Failed http request to gaxkey')

    # Verify that gaxkey auto-launching works.
    completed_process = run_cleos_wallet_command('list', no_auto_keosd=False)
    if completed_process.returncode != 0:
        raise RuntimeError("Expected that gaxkey would be started, "
                           "but got an error instead: {}".format(
                               completed_process.stderr.decode()))
    check_cleos_stderr(completed_process.stderr, b'launched')


try:
    keosd_auto_launch_test()
finally:
    stop_keosd()
