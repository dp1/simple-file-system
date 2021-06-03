#!/bin/bash

RED="\e[31m"
GREEN="\e[32m"
BOLDGREEN="\e[1;32m"
ENDCOLOR="\e[0m"

for test_file in $(find ./tests/ -executable -type f);
do
    echo -e ${BOLDGREEN}Running $test_file...${ENDCOLOR}
    ./$test_file
done
