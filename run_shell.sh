#!/bin/bash

RED="\e[31m"
ENDCOLOR="\e[0m"

if test -f shell/shell; then
    ./shell/shell
else
    echo -e ${RED}Shell executable not found. Did you run make?${ENDCOLOR}
fi
