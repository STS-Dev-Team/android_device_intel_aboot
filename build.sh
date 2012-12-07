#!/bin/bash

TOP=$(dirname $0)

function build_recovery () {
    if [[ ! -e $2 ]]; then
        mkdir $2
    fi

    pushd $TOP
        make CUSTOM_BOARD=$1 O=$2
    popd
}

function usage () {
    echo "Usage:"
    echo "    $1 -o [output directory]"
    echo ""
}

# function main () {
    while getopts "o:p:" opt; do
        case $opt in
        p) PRODUCT=$OPTARG ;;
        o) PRODUCT_OUT=$OPTARG ;;
        *) echo "Unknow options $opt";
           usage $0
           exit 1 ;;
        esac
    done

    if [[ -z $PRODUCT_OUT ]]; then
        PRODUCT_OUT=$TOP/out
    fi
    build_recovery $PRODUCT $PRODUCT_OUT
# }
