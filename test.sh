#!/bin/sh
for file in $(ls tests/)
do
/home/v/dev/binder/tests/$file
rc=$?;
if [[ $rc != 43 ]]
then
echo $file
fi
done
