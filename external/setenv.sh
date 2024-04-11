#!/bin/bash

mkdir -p $PROJECT_OUT
rsync -a --exclude='.git' ./build $PROJECT_OUT/
rsync -a $EXTERNAL/build $PROJECT_OUT/

rsync -a --exclude='.git' ./isp_tuning $PROJECT_OUT/

###################################
# modify cvisetup.sh
###################################
sed -i 's/^TOP_DIR=\$\(.*\)/TOP_DIR=\$PROJECT_OUT/' $PROJECT_OUT/build/cvisetup.sh
echo "INFO: Change TOP_DIR to $PROJECT_OUT in cvisetup.sh "

source $PROJECT_OUT/build/cvisetup.sh

###################################
# overwrite cvisetup.sh functions
###################################

