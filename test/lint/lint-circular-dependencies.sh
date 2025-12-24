#!/usr/bin/env bash
#
# Copyright (c) 2018 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
# Check for circular dependencies

export LC_ALL=C

EXPECTED_CIRCULAR_DEPENDENCIES=(
    "blockprune -> file_io -> blockprune"
    "blockprune -> validation -> blockprune"
    "blockprune -> validation -> chainstate -> blockprune"
    "chainparams -> federationparams -> key_io -> chainparams"
    "chainparams -> federationparams -> primitives/block -> chainparams"
    "chainparams -> federationparams -> pubkey -> chainparams"
    "chainparams -> federationparams -> validation -> chainparams"
    "chainparams -> util -> chainparams"
    "chainstate -> file_io -> validation -> chainstate"
    "checkpoints -> validation -> checkpoints"
    "coloridentifier -> primitives/transaction -> coloridentifier"
    "coloridentifier -> script/script -> coloridentifier"
    "coloridentifier -> script/standard -> coloridentifier"
    "coloridentifier -> script/standard -> script/interpreter -> coloridentifier"
    "core_io -> script/sign -> policy/policy -> validation -> core_io"
    "federationparams -> validation -> federationparams"
    "federationparams -> xfieldhistory -> federationparams"
    "file_io -> index/txindex -> index/base -> file_io"
    "file_io -> validation -> file_io"
    "index/txindex -> validation -> index/txindex"
    "logging -> utiltime -> timeoffsets -> sync -> logging"
    "policy/fees -> policy/policy -> validation -> policy/fees"
    "policy/fees -> txmempool -> policy/fees"
    "policy/packages -> validation -> policy/packages"
    "policy/policy -> validation -> policy/policy"
    "policy/policy -> xfieldhistory -> policy/policy"
    "policy/rbf -> txmempool -> validation -> policy/rbf"
    "primitives/transaction -> script/script -> primitives/transaction"
    "protocol -> util -> tapyrusmodes -> protocol"
    "qt/addressbookpage -> qt/tapyrusgui -> qt/walletview -> qt/addressbookpage"
    "qt/addressbookpage -> qt/tapyrusgui -> qt/walletview -> qt/receivecoinsdialog -> qt/addressbookpage"
    "qt/addressbookpage -> qt/tapyrusgui -> qt/walletview -> qt/sendcoinsdialog -> qt/sendcoinsentry -> qt/addressbookpage"
    "qt/addressbookpage -> qt/tapyrusgui -> qt/walletview -> qt/signverifymessagedialog -> qt/addressbookpage"
    "qt/addresstablemodel -> qt/walletmodel -> qt/addresstablemodel"
    "qt/bantablemodel -> qt/clientmodel -> qt/bantablemodel"
    "qt/clientmodel -> qt/peertablemodel -> qt/clientmodel"
    "qt/guiutil -> qt/walletmodel -> qt/optionsmodel -> qt/guiutil"
    "qt/guiutil -> qt/walletmodel -> qt/optionsmodel -> qt/intro -> qt/guiutil"
    "qt/paymentserver -> qt/walletmodel -> qt/paymentserver"
    "qt/recentrequeststablemodel -> qt/walletmodel -> qt/recentrequeststablemodel"
    "qt/sendcoinsdialog -> qt/walletmodel -> qt/sendcoinsdialog"
    "qt/tapyrusgui -> qt/utilitydialog -> qt/tapyrusgui"
    "qt/tapyrusgui -> qt/walletframe -> qt/tapyrusgui"
    "qt/tapyrusgui -> qt/walletview -> qt/tapyrusgui"
    "qt/transactiontablemodel -> qt/walletmodel -> qt/transactiontablemodel"
    "qt/walletmodel -> qt/walletmodeltransaction -> qt/walletmodel"
    "rpc/rawtransaction -> wallet/rpcwallet -> rpc/rawtransaction"
    "txdb -> xfieldhistory -> txdb"
    "txmempool -> validation -> txmempool"
    "wallet/coincontrol -> wallet/wallet -> wallet/coincontrol"
    "wallet/fees -> wallet/wallet -> wallet/fees"
    "wallet/rpcwallet -> wallet/wallet -> wallet/rpcwallet"
    "wallet/wallet -> wallet/walletdb -> wallet/wallet"
)

EXIT_CODE=0

CIRCULAR_DEPENDENCIES=()

IFS=$'\n'
for CIRC in $(cd src && ../contrib/devtools/circular-dependencies.py {*,*/*,*/*/*}.{h,cpp} | sed -e 's/^Circular dependency: //'); do
    CIRCULAR_DEPENDENCIES+=("$CIRC")
    IS_EXPECTED_CIRC=0
    for EXPECTED_CIRC in "${EXPECTED_CIRCULAR_DEPENDENCIES[@]}"; do
        if [[ "${CIRC}" == "${EXPECTED_CIRC}" ]]; then
            IS_EXPECTED_CIRC=1
            break
        fi
    done
    if [[ ${IS_EXPECTED_CIRC} == 0 ]]; then
        echo "A new circular dependency in the form of \"${CIRC}\" appears to have been introduced."
        echo
        EXIT_CODE=1
    fi
done

for EXPECTED_CIRC in "${EXPECTED_CIRCULAR_DEPENDENCIES[@]}"; do
    IS_PRESENT_EXPECTED_CIRC=0
    for CIRC in "${CIRCULAR_DEPENDENCIES[@]}"; do
        if [[ "${CIRC}" == "${EXPECTED_CIRC}" ]]; then
            IS_PRESENT_EXPECTED_CIRC=1
            break
        fi
    done
    if [[ ${IS_PRESENT_EXPECTED_CIRC} == 0 ]]; then
        echo "Good job! The circular dependency \"${EXPECTED_CIRC}\" is no longer present."
        echo "Please remove it from EXPECTED_CIRCULAR_DEPENDENCIES in $0"
        echo "to make sure this circular dependency is not accidentally reintroduced."
        echo
        EXIT_CODE=1
    fi
done

if [ ${EXIT_CODE} -eq 0 ]; then
  echo "✓ lint-circular-dependencies: PASSED"
else
  echo "✗ lint-circular-dependencies: FAILED"
fi
exit ${EXIT_CODE}
