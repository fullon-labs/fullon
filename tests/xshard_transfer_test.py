#!/usr/bin/env python3

import copy
import time
import json
import os
import signal
import subprocess

from TestHarness import Account, Cluster, TestHelper, Utils, WalletMgr, CORE_SYMBOL
from TestHarness.Node import BlockType
from TestHarness.TestHelper import AppArgs

########################################################################
# shard_register_test
#
#  register shards test
#
########################################################################

Print=Utils.Print
errorExit=Utils.errorExit

appArgs=AppArgs()
args = TestHelper.parse_args({"-n", "--dump-error-details","--keep-logs","-v","--leave-running","--clean-run","--unshared"})
Utils.Debug=args.v
pnodes=3
totalNodes=args.n
if totalNodes<=pnodes+2:
    totalNodes=pnodes+2
cluster=Cluster(walletd=True,unshared=args.unshared)
dumpErrorDetails=args.dump_error_details
keepLogs=args.keep_logs
dontKill=args.leave_running
prodCount=1
killAll=args.clean_run
walletPort=TestHelper.DEFAULT_WALLET_PORT

walletMgr=WalletMgr(True, port=walletPort)
testSuccessful=False
killEosInstances=not dontKill
killWallet=not dontKill

WalletdName=Utils.EosWalletName
# ClientName="focli"

EOSIO_ACCT_PRIVATE_DEFAULT_KEY = "5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3"
EOSIO_ACCT_PUBLIC_DEFAULT_KEY = "FO6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5GDW5CV"

try:
    TestHelper.printSystemInfo("BEGIN")
    cluster.setWalletMgr(walletMgr)

    cluster.killall(allInstances=killAll)
    cluster.cleanup()
    Print("Stand up cluster")
    successDuration = 60
    failure_duration = 40
    extraNodeosArgs=" --transaction-finality-status-max-storage-size-gb 1 " + \
                   f"--transaction-finality-status-success-duration-sec {successDuration} --transaction-finality-status-failure-duration-sec {failure_duration}"
    extraNodeosArgs+=" --http-max-response-time-ms 990000"
    if cluster.launch(prodCount=prodCount, onlyBios=False, pnodes=pnodes, totalNodes=totalNodes, totalProducers=pnodes*prodCount,
                      topo="line", extraNodeosArgs=extraNodeosArgs) is False:
        Utils.errorExit("Failed to stand up eos cluster.")

    Print("Validating system accounts after bootstrap")
    cluster.validateAccounts(None)

    biosNode=cluster.biosNode
    prod0=cluster.getNode(0)
    prod1=cluster.getNode(1)
    testNode=cluster.getNode(totalNodes-1)

    Print("Kill the bios node")
    biosNode.kill(signal.SIGTERM)

    Print("Wait for node0's head block to become irreversible")
    cluster.waitOnClusterSync()

    Print("Creating account1")
    account1 = Account('account1')
    account1.ownerPublicKey = EOSIO_ACCT_PUBLIC_DEFAULT_KEY
    account1.activePublicKey = EOSIO_ACCT_PUBLIC_DEFAULT_KEY
    cluster.createAccountAndVerify(account1, cluster.eosioAccount, stakedDeposit=1000)

    Print("Creating account2")
    account2 = Account('account2')
    account2.ownerPublicKey = EOSIO_ACCT_PUBLIC_DEFAULT_KEY
    account2.activePublicKey = EOSIO_ACCT_PUBLIC_DEFAULT_KEY
    cluster.createAccountAndVerify(account2, cluster.eosioAccount, stakedDeposit=1000)

    Print("Validating accounts after bootstrap")
    cluster.validateAccounts([account1, account2])

    transferAmountStr = testNode.currencyIntToStr(100, CORE_SYMBOL)
    testNode.transferFunds(cluster.eosioAccount, account1, transferAmountStr, "initial fund")
    if Utils.Debug: Utils.Print("Initial funds of account %s transfered on transaction id %s." % (account1, testNode.getLastTrackedTransactionId()))

    testNode.regshard("shard1", account1, True, waitForTransBlock=True)
    if Utils.Debug: Utils.Print("Register shard '%s' on transaction id %s." % ("shard1", testNode.getLastTrackedTransactionId()))

    startingBlockNum = testNode.getInfo()["head_block_num"]

    def waitForShardRegistered(name):
        # default to the typical configuration of 21 producers, each producing 12 blocks in a row (every 1/2 second)
        timeout = 21 * 6 * 2
        start=time.perf_counter()
        startingBlockNum=testNode.getInfo()["head_block_num"]
        def isShardRegistered():
            return testNode.isShardExists(name)
        found = Utils.waitForBool(isShardRegistered, timeout)
        assert found, \
            f"Waited for {time.perf_counter()-start} sec but never found shard: {name}. Started with block num {startingBlockNum} and ended with {testNode.getInfo()['head_block_num']}"
        return found

    waitForShardRegistered("shard1")

    testNode.xTransferOut(account1, Utils.MainShardName, "shard1", Utils.SysTokenAccount, testNode.currencyIntToStr(10), waitForTransBlock=True)

    testSuccessful=True

finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful=testSuccessful, killEosInstances=killEosInstances, killWallet=killWallet, keepLogs=keepLogs, cleanRun=killAll, dumpErrorDetails=dumpErrorDetails)

exitCode = 0 if testSuccessful else 1
exit(exitCode)
