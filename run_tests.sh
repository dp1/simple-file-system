#!/bin/bash

RED="\e[31m"
GREEN="\e[32m"
BOLDGREEN="\e[1;32m"
ENDCOLOR="\e[0m"

files=$(find ./tests/ -executable -type f)

if [[ ${files[@]} ]]; then

    for test_file in ${files[@]}
    do
        echo -e ${BOLDGREEN}Running $test_file...${ENDCOLOR}
        ./$test_file
    done

else
    echo -e ${RED}No tests found. Did you run make?${ENDCOLOR}
fi
