#!/bin/bash

DIR=${1:-build}

rm -rf "$DIR" && cmake -B "$DIR" . && cmake --build "$DIR" && echo && "${DIR}/exec"
